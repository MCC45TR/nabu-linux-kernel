// SPDX-License-Identifier: GPL-2.0-only
/*
 * Qualcomm SMP2P application-processor sleep-state notification
 *
 * Some downstream Qualcomm firmware expects bit 12 of the "sleepstate"
 * SMP2P entry to describe whether the application processor is awake.  In
 * particular, the Xiaomi Pad 5 SLPI firmware uses this handshake while
 * preparing its sensor process for system suspend.
 */

#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_wakeup.h>
#include <linux/soc/qcom/smem_state.h>
#include <linux/suspend.h>

#define QCOM_SMP2P_APPS_AWAKE_BIT	BIT(12)

struct qcom_smp2p_sleep_state {
	struct device *dev;
	struct qcom_smem_state *state;
	struct notifier_block pm_nb;
};

static int qcom_smp2p_sleep_state_notify(struct notifier_block *nb,
					 unsigned long event, void *unused)
{
	struct qcom_smp2p_sleep_state *sleep_state =
		container_of(nb, struct qcom_smp2p_sleep_state, pm_nb);
	int ret;

	switch (event) {
	case PM_SUSPEND_PREPARE:
		ret = qcom_smem_state_update_bits(sleep_state->state,
						  QCOM_SMP2P_APPS_AWAKE_BIT, 0);
		if (ret)
			dev_warn(sleep_state->dev,
				 "failed to announce suspend: %d\n", ret);

		/* Xiaomi's downstream implementation allows for SMP2P latency. */
		usleep_range(10000, 10500);
		break;
	case PM_POST_SUSPEND:
		ret = qcom_smem_state_update_bits(sleep_state->state,
						  QCOM_SMP2P_APPS_AWAKE_BIT,
						  QCOM_SMP2P_APPS_AWAKE_BIT);
		if (ret)
			dev_warn(sleep_state->dev,
				 "failed to announce resume: %d\n", ret);
		break;
	default:
		break;
	}

	return NOTIFY_DONE;
}

static irqreturn_t qcom_smp2p_sleep_state_irq(int irq, void *data)
{
	struct qcom_smp2p_sleep_state *sleep_state = data;

	pm_wakeup_event(sleep_state->dev, 200);
	return IRQ_HANDLED;
}

static void qcom_smp2p_sleep_state_unregister(void *data)
{
	struct qcom_smp2p_sleep_state *sleep_state = data;

	unregister_pm_notifier(&sleep_state->pm_nb);
	qcom_smem_state_update_bits(sleep_state->state,
				    QCOM_SMP2P_APPS_AWAKE_BIT,
				    QCOM_SMP2P_APPS_AWAKE_BIT);
}

static int qcom_smp2p_sleep_state_probe(struct platform_device *pdev)
{
	struct qcom_smp2p_sleep_state *sleep_state;
	unsigned int unused_bit;
	int irq;
	int ret;

	sleep_state = devm_kzalloc(&pdev->dev, sizeof(*sleep_state), GFP_KERNEL);
	if (!sleep_state)
		return -ENOMEM;

	sleep_state->dev = &pdev->dev;
	sleep_state->state = devm_qcom_smem_state_get(&pdev->dev, NULL,
						      &unused_bit);
	if (IS_ERR(sleep_state->state))
		return dev_err_probe(&pdev->dev, PTR_ERR(sleep_state->state),
				     "failed to acquire sleep-state entry\n");

	ret = qcom_smem_state_update_bits(sleep_state->state,
					  QCOM_SMP2P_APPS_AWAKE_BIT,
					  QCOM_SMP2P_APPS_AWAKE_BIT);
	if (ret)
		return dev_err_probe(&pdev->dev, ret,
				     "failed to announce awake state\n");

	sleep_state->pm_nb.notifier_call = qcom_smp2p_sleep_state_notify;
	sleep_state->pm_nb.priority = INT_MAX;
	ret = register_pm_notifier(&sleep_state->pm_nb);
	if (ret)
		return dev_err_probe(&pdev->dev, ret,
				     "failed to register PM notifier\n");

	ret = devm_add_action_or_reset(&pdev->dev,
				       qcom_smp2p_sleep_state_unregister,
				       sleep_state);
	if (ret)
		return ret;

	irq = platform_get_irq_byname(pdev, "state-change");
	if (irq < 0)
		return irq;

	ret = devm_request_threaded_irq(&pdev->dev, irq, NULL,
					qcom_smp2p_sleep_state_irq,
					IRQF_ONESHOT | IRQF_TRIGGER_RISING,
					dev_name(&pdev->dev), sleep_state);
	if (ret)
		return dev_err_probe(&pdev->dev, ret,
				     "failed to request state-change IRQ\n");

	ret = devm_device_init_wakeup(&pdev->dev);
	if (ret)
		return dev_err_probe(&pdev->dev, ret,
				     "failed to initialize wakeup support\n");

	dev_info(&pdev->dev, "application processor awake handshake enabled\n");

	return 0;
}

static const struct of_device_id qcom_smp2p_sleep_state_of_match[] = {
	{ .compatible = "qcom,smp2p-sleep-state" },
	{ }
};
MODULE_DEVICE_TABLE(of, qcom_smp2p_sleep_state_of_match);

static struct platform_driver qcom_smp2p_sleep_state_driver = {
	.probe = qcom_smp2p_sleep_state_probe,
	.driver = {
		.name = "qcom-smp2p-sleep-state",
		.of_match_table = qcom_smp2p_sleep_state_of_match,
	},
};
module_platform_driver(qcom_smp2p_sleep_state_driver);

MODULE_DESCRIPTION("Qualcomm SMP2P sleep-state notifier");
MODULE_LICENSE("GPL");
