// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/delay.h>
#include <linux/pm_runtime.h>

#include "iris_core.h"
#include "iris_firmware.h"
#include "iris_state.h"
#include "iris_vpu_common.h"

static void iris_core_deinit_locked(struct iris_core *core, bool power_off)
{
	iris_fw_unload(core);
	if (power_off)
		iris_vpu_power_off(core);
	iris_hfi_queues_deinit(core);
	core->state = IRIS_CORE_DEINIT;
}

void iris_core_deinit(struct iris_core *core)
{
	int ret;

	/*
	 * The core starts in DEINIT and remains there while allow_fw_boot is
	 * disabled.  Do not resume power domains, issue PAS shutdown, or touch
	 * VPU registers when no firmware/HFI state was ever initialized.
	 * Qualcomm's current driver has the same state guard.
	 */
	if (READ_ONCE(core->state) == IRIS_CORE_DEINIT)
		return;

	ret = pm_runtime_resume_and_get(core->dev);

	mutex_lock(&core->lock);
	if (core->state != IRIS_CORE_DEINIT)
		iris_core_deinit_locked(core, ret >= 0);
	mutex_unlock(&core->lock);

	if (ret >= 0)
		pm_runtime_put_sync(core->dev);
}

int iris_core_deinit_for_system_suspend(struct iris_core *core)
{
	int ret = 0;

	mutex_lock(&core->lock);
	if (!list_empty(&core->instances)) {
		dev_warn(core->dev,
			 "refusing system suspend with active codec sessions\n");
		ret = -EBUSY;
	} else if (core->state != IRIS_CORE_DEINIT) {
		dev_info(core->dev,
			 "quiescing legacy VPU5 for system suspend\n");
		iris_core_deinit_locked(core, true);
	}
	mutex_unlock(&core->lock);

	return ret;
}

static int iris_wait_for_system_response(struct iris_core *core)
{
	u32 hw_response_timeout_val = core->iris_platform_data->hw_response_timeout;
	int ret;

	if (core->state == IRIS_CORE_ERROR)
		return -EIO;

	ret = wait_for_completion_timeout(&core->core_init_done,
					  msecs_to_jiffies(hw_response_timeout_val));
	if (!ret) {
		if (core->iris_platform_data->legacy_vpu5) {
			dev_err(core->dev,
				"HFI timeout queues: cmd r=%u w=%u, msg r=%u w=%u, dbg r=%u w=%u\n",
				core->command_queue.qhdr->read_idx,
				core->command_queue.qhdr->write_idx,
				core->message_queue.qhdr->read_idx,
				core->message_queue.qhdr->write_idx,
				core->debug_queue.qhdr->read_idx,
				core->debug_queue.qhdr->write_idx);
		}
		core->state = IRIS_CORE_ERROR;
		dev_err(core->dev, "timed out waiting for HFI system response\n");
		return -ETIMEDOUT;
	}

	return 0;
}

int iris_core_init(struct iris_core *core)
{
	int ret;

	mutex_lock(&core->lock);
	if (core->state == IRIS_CORE_INIT) {
		ret = 0;
		goto exit;
	} else if (core->state == IRIS_CORE_ERROR) {
		ret = -EINVAL;
		goto error;
	}

	core->state = IRIS_CORE_INIT;

	dev_info(core->dev, "initializing HFI queues\n");
	ret = iris_hfi_queues_init(core);
	if (ret)
		goto error;

	dev_info(core->dev, "powering on Iris hardware\n");
	ret = iris_vpu_power_on(core);
	if (ret)
		goto error_queue_deinit;

	dev_info(core->dev, "loading Iris firmware\n");
	ret = iris_fw_load(core);
	if (ret)
		goto error_power_off;

	/*
	 * SM8150's secure VPU5 path needs the explicit remote-state resume
	 * used by the legacy Venus driver after PAS authentication and before
	 * the host programs HFI/CTRL_INIT.  The newer Iris platforms do not
	 * require this on their initial boot path.
	 */
	if (core->iris_platform_data->legacy_vpu5) {
		dev_info(core->dev, "resuming secure VPU5 hardware state\n");
		ret = iris_set_hw_state(core, true);
		if (ret) {
			dev_err(core->dev,
				"failed to resume secure VPU5 hardware state: %d\n",
				ret);
			goto error_unload_fw;
		}
		dev_info(core->dev, "secure VPU5 hardware state resumed\n");
	}

	dev_info(core->dev, "booting Iris firmware CPU\n");
	ret = iris_vpu_boot_firmware(core);
	if (ret)
		goto error_unload_fw;

	dev_info(core->dev, "initializing HFI core\n");
	reinit_completion(&core->core_init_done);
	ret = iris_hfi_core_init(core);
	if (ret)
		goto error_unload_fw;

	mutex_unlock(&core->lock);

	return iris_wait_for_system_response(core);

error_unload_fw:
	iris_fw_unload(core);
error_power_off:
	iris_vpu_power_off(core);
error_queue_deinit:
	iris_hfi_queues_deinit(core);
error:
	core->state = IRIS_CORE_DEINIT;
exit:
	mutex_unlock(&core->lock);

	return ret;
}
