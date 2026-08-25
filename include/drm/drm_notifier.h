/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _DRM_NOTIFIER_H_
#define _DRM_NOTIFIER_H_

#include <linux/notifier.h>

#define MI_DRM_EVENT_BLANK		0x01
#define MI_DRM_EARLY_EVENT_BLANK	0x02

enum drm_notifier_data {
	MI_DRM_BLANK_UNBLANK,
	MI_DRM_BLANK_POWERDOWN,
};

int mi_drm_register_client(struct notifier_block *nb);
int mi_drm_unregister_client(struct notifier_block *nb);
int mi_drm_notifier_call_chain(unsigned long val, void *v);

#endif
