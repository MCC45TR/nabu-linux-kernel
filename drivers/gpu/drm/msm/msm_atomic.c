// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2014 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
 */

#include <drm/drm_atomic_uapi.h>
#include <drm/drm_bridge.h>
#include <drm/drm_connector.h>
#include <drm/drm_vblank.h>

#include <linux/of.h>

#include "msm_atomic_trace.h"
#include "msm_drv.h"
#include "msm_gem.h"
#include "msm_kms.h"
#include "dsi/dsi.h"

/*
 * Helpers to control vblanks while we flush.. basically just to ensure
 * that vblank accounting is switched on, so we get valid seqn/timestamp
 * on pageflip events (if requested)
 */

static void vblank_get(struct msm_kms *kms, unsigned crtc_mask)
{
	struct drm_crtc *crtc;

	for_each_crtc_mask(kms->dev, crtc, crtc_mask) {
		if (!crtc->state->active)
			continue;
		drm_crtc_vblank_get(crtc);
	}
}

static void vblank_put(struct msm_kms *kms, unsigned crtc_mask)
{
	struct drm_crtc *crtc;

	for_each_crtc_mask(kms->dev, crtc, crtc_mask) {
		if (!crtc->state->active)
			continue;
		drm_crtc_vblank_put(crtc);
	}
}

static void lock_crtcs(struct msm_kms *kms, unsigned int crtc_mask)
{
	int crtc_index;
	struct drm_crtc *crtc;

	for_each_crtc_mask(kms->dev, crtc, crtc_mask) {
		crtc_index = drm_crtc_index(crtc);
		mutex_lock_nested(&kms->commit_lock[crtc_index], crtc_index);
	}
}

static void unlock_crtcs(struct msm_kms *kms, unsigned int crtc_mask)
{
	struct drm_crtc *crtc;

	for_each_crtc_mask_reverse(kms->dev, crtc, crtc_mask)
		mutex_unlock(&kms->commit_lock[drm_crtc_index(crtc)]);
}

static void msm_atomic_async_commit(struct msm_kms *kms, int crtc_idx)
{
	unsigned crtc_mask = BIT(crtc_idx);

	trace_msm_atomic_async_commit_start(crtc_mask);

	lock_crtcs(kms, crtc_mask);

	if (!(kms->pending_crtc_mask & crtc_mask)) {
		unlock_crtcs(kms, crtc_mask);
		goto out;
	}

	kms->pending_crtc_mask &= ~crtc_mask;

	kms->funcs->enable_commit(kms);

	vblank_get(kms, crtc_mask);

	/*
	 * Flush hardware updates:
	 */
	trace_msm_atomic_flush_commit(crtc_mask);
	kms->funcs->flush_commit(kms, crtc_mask);

	/*
	 * Wait for flush to complete:
	 */
	trace_msm_atomic_wait_flush_start(crtc_mask);
	kms->funcs->wait_flush(kms, crtc_mask);
	trace_msm_atomic_wait_flush_finish(crtc_mask);

	vblank_put(kms, crtc_mask);

	kms->funcs->complete_commit(kms, crtc_mask);
	unlock_crtcs(kms, crtc_mask);
	kms->funcs->disable_commit(kms);

out:
	trace_msm_atomic_async_commit_finish(crtc_mask);
}

static void msm_atomic_pending_work(struct kthread_work *work)
{
	struct msm_pending_timer *timer = container_of(work,
			struct msm_pending_timer, work.work);

	msm_atomic_async_commit(timer->kms, timer->crtc_idx);
}

int msm_atomic_init_pending_timer(struct msm_pending_timer *timer,
		struct msm_kms *kms, int crtc_idx)
{
	timer->kms = kms;
	timer->crtc_idx = crtc_idx;

	timer->worker = kthread_run_worker(0, "atomic-worker-%d", crtc_idx);
	if (IS_ERR(timer->worker)) {
		int ret = PTR_ERR(timer->worker);
		timer->worker = NULL;
		return ret;
	}
	sched_set_fifo(timer->worker->task);

	msm_hrtimer_work_init(&timer->work, timer->worker,
			      msm_atomic_pending_work,
			      CLOCK_MONOTONIC, HRTIMER_MODE_ABS);

	return 0;
}

void msm_atomic_destroy_pending_timer(struct msm_pending_timer *timer)
{
	if (timer->worker)
		kthread_destroy_worker(timer->worker);
}

static bool can_do_async(struct drm_atomic_commit *state,
		struct drm_crtc **async_crtc)
{
	struct drm_connector_state *connector_state;
	struct drm_connector *connector;
	struct drm_crtc_state *crtc_state;
	struct drm_crtc *crtc;
	int i, num_crtcs = 0;

	if (!(state->legacy_cursor_update || state->async_update))
		return false;

	/* any connector change, means slow path: */
	for_each_new_connector_in_state(state, connector, connector_state, i)
		return false;

	for_each_new_crtc_in_state(state, crtc, crtc_state, i) {
		if (drm_atomic_crtc_needs_modeset(crtc_state))
			return false;
		if (!crtc_state->active)
			return false;
		if (++num_crtcs > 1)
			return false;
		*async_crtc = crtc;
	}

	return true;
}

/* Get bitmask of crtcs that will need to be flushed.  The bitmask
 * can be used with for_each_crtc_mask() iterator, to iterate
 * effected crtcs without needing to preserve the atomic state.
 */
static unsigned get_crtc_mask(struct drm_atomic_commit *state)
{
	struct drm_crtc_state *crtc_state;
	struct drm_crtc *crtc;
	unsigned i, mask = 0;

	for_each_new_crtc_in_state(state, crtc, crtc_state, i)
		mask |= drm_crtc_mask(crtc);

	return mask;
}

static bool msm_dfps_mode_compatible(const struct drm_display_mode *old,
				     const struct drm_display_mode *new)
{
	return old->clock == new->clock &&
		old->hdisplay == new->hdisplay &&
		old->hsync_start == new->hsync_start &&
		old->hsync_end == new->hsync_end &&
		old->htotal == new->htotal &&
		old->vdisplay == new->vdisplay &&
		(old->vsync_end - old->vsync_start) ==
			(new->vsync_end - new->vsync_start) &&
		(old->vtotal - old->vsync_end) ==
			(new->vtotal - new->vsync_end) &&
		old->vsync_start != new->vsync_start;
}

static bool msm_crtc_has_dsi_connector(struct drm_atomic_commit *state,
				       struct drm_crtc *crtc)
{
	struct drm_connector_state *conn_state;
	struct drm_connector *connector;
	struct drm_connector_list_iter conn_iter;
	bool found = false;

	drm_connector_list_iter_begin(state->dev, &conn_iter);
	drm_for_each_connector_iter(connector, &conn_iter) {
		conn_state = drm_atomic_get_new_connector_state(state, connector);
		if (!conn_state)
			conn_state = connector->state;
		if (conn_state->crtc == crtc &&
		    connector->connector_type == DRM_MODE_CONNECTOR_DSI) {
			found = true;
			break;
		}
	}
	drm_connector_list_iter_end(&conn_iter);

	return found;
}

static void msm_atomic_mark_seamless_dfps(struct drm_atomic_commit *state)
{
	struct drm_crtc_state *old_crtc_state, *new_crtc_state;
	struct drm_crtc *crtc;
	int i;

	for_each_oldnew_crtc_in_state(state, crtc, old_crtc_state,
				      new_crtc_state, i) {
		new_crtc_state->seamless_dfps = false;
		if (!old_crtc_state->active || !new_crtc_state->active ||
		    !new_crtc_state->mode_changed ||
		    new_crtc_state->active_changed ||
		    new_crtc_state->connectors_changed ||
		    !msm_crtc_has_dsi_connector(state, crtc) ||
		    !msm_dfps_mode_compatible(&old_crtc_state->adjusted_mode,
					      &new_crtc_state->adjusted_mode))
			continue;

		new_crtc_state->seamless_dfps = true;
	}
}

static bool msm_atomic_has_seamless_dfps(struct drm_atomic_commit *state)
{
	struct drm_crtc_state *new_crtc_state;
	struct drm_crtc *crtc;
	int i;

	for_each_new_crtc_in_state(state, crtc, new_crtc_state, i) {
		if (new_crtc_state->seamless_dfps &&
		    new_crtc_state->mode_changed)
			return true;
	}

	return false;
}

static void msm_atomic_wait_seamless_dfps_vblank(
		struct drm_atomic_commit *state)
{
	struct drm_crtc_state *new_crtc_state;
	struct drm_crtc *crtc;
	int i;

	for_each_new_crtc_in_state(state, crtc, new_crtc_state, i) {
		if (new_crtc_state->seamless_dfps &&
		    new_crtc_state->mode_changed)
			drm_crtc_wait_one_vblank(crtc);
	}
}

static void msm_atomic_mask_seamless_modesets(struct drm_atomic_commit *state,
					       bool mask)
{
	struct drm_crtc_state *new_crtc_state;
	struct drm_crtc *crtc;
	int i;

	for_each_new_crtc_in_state(state, crtc, new_crtc_state, i) {
		if (new_crtc_state->seamless_dfps)
			new_crtc_state->mode_changed = !mask;
	}
}

static void msm_atomic_commit_seamless_modesets(struct drm_atomic_commit *state)
{
	struct drm_connector_state *conn_state;
	struct drm_connector *connector;
	struct drm_connector_list_iter conn_iter;

	drm_connector_list_iter_begin(state->dev, &conn_iter);
	drm_for_each_connector_iter(connector, &conn_iter) {
		const struct drm_encoder_helper_funcs *funcs;
		struct drm_bridge *bridge;
		struct drm_crtc_state *crtc_state;
		struct drm_encoder *encoder;

		conn_state = drm_atomic_get_new_connector_state(state, connector);
		if (!conn_state)
			conn_state = connector->state;

		if (!conn_state->crtc || !conn_state->best_encoder)
			continue;

		crtc_state = drm_atomic_get_new_crtc_state(state,
							 conn_state->crtc);
		if (!crtc_state || !crtc_state->seamless_dfps)
			continue;

		encoder = conn_state->best_encoder;
		funcs = encoder->helper_private;
		if (funcs && funcs->atomic_mode_set)
			funcs->atomic_mode_set(encoder, crtc_state, conn_state);

		/*
		 * Program only the MSM DSI bridge here.  The downstream panel
		 * bridge may send TDDI refresh-rate commands from mode_set().
		 * Sending those commands before the DSI and DPU vertical timing
		 * update is committed leaves a dual-DSI video panel temporarily
		 * running with two different frame periods.  On nabu that can
		 * corrupt the scanout and force the Novatek touch controller to
		 * recover its firmware.
		 */
		bridge = drm_bridge_chain_get_first_bridge(encoder);
		if (bridge && bridge->funcs->mode_set)
			bridge->funcs->mode_set(bridge, &crtc_state->mode,
						&crtc_state->adjusted_mode);
	}
	drm_connector_list_iter_end(&conn_iter);

#if IS_ENABLED(CONFIG_DRM_MSM_DSI)
	/*
	 * Stage the new DSI vertical timing after its bridge has queued the
	 * pending mode.  The DPU interface timing is latched by the following
	 * atomic flush at the same commit boundary.
	 */
	if (msm_dsi_manager_stage_seamless_dfps())
		DRM_ERROR("failed to stage seamless DSI timing update\n");
#endif
}

static void
msm_atomic_commit_seamless_downstream_modesets(struct drm_atomic_commit *state)
{
	struct drm_connector_state *conn_state;
	struct drm_connector *connector;
	struct drm_connector_list_iter conn_iter;

	drm_connector_list_iter_begin(state->dev, &conn_iter);
	drm_for_each_connector_iter(connector, &conn_iter) {
		struct drm_crtc_state *crtc_state;
		struct drm_encoder *encoder;
		struct drm_bridge *bridge;

		conn_state = drm_atomic_get_new_connector_state(state, connector);
		if (!conn_state)
			conn_state = connector->state;

		if (!conn_state->crtc || !conn_state->best_encoder)
			continue;

		crtc_state = drm_atomic_get_new_crtc_state(state,
							 conn_state->crtc);
		if (!crtc_state || !crtc_state->seamless_dfps)
			continue;

		encoder = conn_state->best_encoder;
		bridge = drm_bridge_chain_get_first_bridge(encoder);
		if (!bridge ||
		    list_is_last(&bridge->chain_node, &encoder->bridge_chain))
			continue;

		/*
		 * The DSI timing database has now been released on the new frame
		 * period.  Run panel and any later bridge mode_set callbacks only
		 * now, so their DCS command insertion observes a live video-done
		 * boundary for the new mode.
		 */
		bridge = list_next_entry(bridge, chain_node);
		drm_bridge_chain_mode_set(bridge, &crtc_state->mode,
					  &crtc_state->adjusted_mode);
	}
	drm_connector_list_iter_end(&conn_iter);
}

int msm_atomic_check(struct drm_device *dev, struct drm_atomic_commit *state)
{
	struct msm_drm_private *priv = dev->dev_private;
	struct msm_kms *kms = priv->kms;
	struct drm_crtc_state *old_crtc_state, *new_crtc_state;
	struct drm_crtc *crtc;
	int i, ret = 0;

	/*
	 * FIXME: stop setting allow_modeset and move this check to the DPU
	 * driver.
	 */
	for_each_oldnew_crtc_in_state(state, crtc, old_crtc_state,
				      new_crtc_state, i) {
		if ((old_crtc_state->ctm && !new_crtc_state->ctm) ||
		    (!old_crtc_state->ctm && new_crtc_state->ctm)) {
			/*
			 * Nabu reserves DSPP resources at every modeset.  Its CTM can
			 * therefore be enabled or bypassed through the normal atomic
			 * color-management flush without blanking the dual-DSI panel.
			 */
			if (of_machine_is_compatible("xiaomi,nabu"))
				continue;

			new_crtc_state->mode_changed = true;
			state->allow_modeset = true;
		}
	}

	if (kms && kms->funcs && kms->funcs->check_mode_changed)
		ret = kms->funcs->check_mode_changed(kms, state);
	if (ret)
		return ret;

	ret = drm_atomic_helper_check(dev, state);
	if (!ret)
		msm_atomic_mark_seamless_dfps(state);

	return ret;
}

void msm_atomic_commit_tail(struct drm_atomic_commit *state)
{
	struct drm_device *dev = state->dev;
	struct msm_drm_private *priv = dev->dev_private;
	struct msm_kms *kms = priv->kms;
	struct drm_crtc *async_crtc = NULL;
	unsigned crtc_mask = get_crtc_mask(state);
	bool async = can_do_async(state, &async_crtc);

	trace_msm_atomic_commit_tail_start(async, crtc_mask);

	kms->funcs->enable_commit(kms);

	/*
	 * Ensure any previous (potentially async) commit has
	 * completed:
	 */
	lock_crtcs(kms, crtc_mask);
	trace_msm_atomic_wait_flush_start(crtc_mask);
	kms->funcs->wait_flush(kms, crtc_mask);
	trace_msm_atomic_wait_flush_finish(crtc_mask);

	atomic_set(&kms->fault_snapshot_capture, 0);

	/*
	 * Now that there is no in-progress flush, prepare the
	 * current update:
	 */
	if (kms->funcs->prepare_commit)
		kms->funcs->prepare_commit(kms, state);

	/*
	 * Push atomic updates down to hardware:
	 */
	msm_atomic_mask_seamless_modesets(state, true);
	drm_atomic_helper_commit_modeset_disables(dev, state);
	msm_atomic_mask_seamless_modesets(state, false);
	drm_atomic_helper_commit_planes(dev, state, 0);
	/*
	 * dpu_crtc_atomic_begin(), called by commit_planes(), clears the CTL
	 * pending-flush mask before programming the planes.  Program seamless
	 * interface timing afterwards so its INTF flush bit survives until
	 * dpu_kms_flush_commit() latches the new DPU frame period.
	 */
	msm_atomic_commit_seamless_modesets(state);
	msm_atomic_mask_seamless_modesets(state, true);
	drm_atomic_helper_commit_modeset_enables(dev, state);
	msm_atomic_mask_seamless_modesets(state, false);

	if (async) {
		struct msm_pending_timer *timer =
			&kms->pending_timers[drm_crtc_index(async_crtc)];

		/* async updates are limited to single-crtc updates: */
		WARN_ON(crtc_mask != drm_crtc_mask(async_crtc));

		/*
		 * Start timer if we don't already have an update pending
		 * on this crtc:
		 */
		if (!(kms->pending_crtc_mask & crtc_mask)) {
			ktime_t vsync_time, wakeup_time;

			kms->pending_crtc_mask |= crtc_mask;

			if (drm_crtc_next_vblank_start(async_crtc, &vsync_time))
				goto fallback;

			wakeup_time = ktime_sub(vsync_time, ms_to_ktime(1));

			msm_hrtimer_queue_work(&timer->work, wakeup_time,
					HRTIMER_MODE_ABS);
		}

		kms->funcs->disable_commit(kms);
		unlock_crtcs(kms, crtc_mask);
		/*
		 * At this point, from drm core's perspective, we
		 * are done with the atomic update, so we can just
		 * go ahead and signal that it is done:
		 */
		drm_atomic_helper_commit_hw_done(state);
		drm_atomic_helper_cleanup_planes(dev, state);

		trace_msm_atomic_commit_tail_finish(async, crtc_mask);

		return;
	}

fallback:
	/*
	 * If there is any async flush pending on updated crtcs, fold
	 * them into the current flush.
	 */
	kms->pending_crtc_mask &= ~crtc_mask;

	vblank_get(kms, crtc_mask);

	/*
	 * Flush hardware updates:
	 */
	trace_msm_atomic_flush_commit(crtc_mask);
	kms->funcs->flush_commit(kms, crtc_mask);
	unlock_crtcs(kms, crtc_mask);
	/*
	 * Wait for flush to complete:
	 */
	trace_msm_atomic_wait_flush_start(crtc_mask);
	kms->funcs->wait_flush(kms, crtc_mask);
	trace_msm_atomic_wait_flush_finish(crtc_mask);

#if IS_ENABLED(CONFIG_DRM_MSM_DSI)
	if (msm_atomic_has_seamless_dfps(state)) {
		/*
		 * The DPU interface timing is part of the preceding atomic flush,
		 * while the bonded DSI timing is still held in its shadow database.
		 * Wait for the actual CRTC frame boundary before releasing both DSI
		 * links. A fixed microsecond delay is invalid across 60/90/120 Hz and
		 * can overflow the DPU frame-event queue during repeated DFPS.
		 */
		msm_atomic_wait_seamless_dfps_vblank(state);
		msm_dsi_manager_complete_seamless_dfps();
		msm_atomic_commit_seamless_downstream_modesets(state);
	}
#endif

	vblank_put(kms, crtc_mask);

	lock_crtcs(kms, crtc_mask);
	kms->funcs->complete_commit(kms, crtc_mask);
	unlock_crtcs(kms, crtc_mask);
	kms->funcs->disable_commit(kms);

	drm_atomic_helper_commit_hw_done(state);
	drm_atomic_helper_cleanup_planes(dev, state);

	trace_msm_atomic_commit_tail_finish(async, crtc_mask);
}
