// SPDX-License-Identifier: GPL-2.0-only
/* Xiaomi Pad 5 pogo keyboard-cover power and presence controller. */

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_wakeup.h>
#include <linux/workqueue.h>

#include <linux/input/xiaomi-nabu-keyboard.h>

#define NABU_KEYBOARD_DEBOUNCE_MS 35

struct nabu_keyboard {
	struct device *dev;
	struct gpio_desc *vdd;
	struct gpio_desc *reset;
	struct gpio_desc *detect;
	struct delayed_work detect_work;
	int irq;
	bool connected;
	bool wake_enabled;
};

static void nabu_keyboard_publish_state(struct nabu_keyboard *keyboard,
					bool connected, bool force)
{
	char state[40];
	char *envp[2];

	if (!force && keyboard->connected == connected)
		return;

	keyboard->connected = connected;
	xiaomi_nabu_keyboard_set_attached(connected);
	snprintf(state, sizeof(state), "XIAOMI_KEYBOARD_CONNECTED=%u",
		 connected);
	envp[0] = state;
	envp[1] = NULL;
	kobject_uevent_env(&keyboard->dev->kobj, KOBJ_CHANGE, envp);
	dev_info(keyboard->dev, "keyboard cover %s\n",
		 connected ? "attached" : "detached");
}

static void nabu_keyboard_detect_work(struct work_struct *work)
{
	struct nabu_keyboard *keyboard;
	int value;

	keyboard = container_of(to_delayed_work(work), struct nabu_keyboard,
				detect_work);
	value = gpiod_get_value_cansleep(keyboard->detect);
	if (value < 0) {
		dev_err_ratelimited(keyboard->dev,
				    "failed to read keyboard detect GPIO: %d\n",
				    value);
		return;
	}

	nabu_keyboard_publish_state(keyboard, value, false);
}

static irqreturn_t nabu_keyboard_detect_irq(int irq, void *data)
{
	struct nabu_keyboard *keyboard = data;

	pm_wakeup_event(keyboard->dev, 500);
	mod_delayed_work(system_wq, &keyboard->detect_work,
			 msecs_to_jiffies(NABU_KEYBOARD_DEBOUNCE_MS));
	return IRQ_HANDLED;
}

static ssize_t connected_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct nabu_keyboard *keyboard = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%u\n", keyboard->connected);
}
static DEVICE_ATTR_RO(connected);

static int nabu_keyboard_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct nabu_keyboard *keyboard;
	int connected;
	int ret;

	keyboard = devm_kzalloc(dev, sizeof(*keyboard), GFP_KERNEL);
	if (!keyboard)
		return -ENOMEM;

	keyboard->dev = dev;
	keyboard->vdd = devm_gpiod_get(dev, "vdd", GPIOD_OUT_LOW);
	if (IS_ERR(keyboard->vdd))
		return dev_err_probe(dev, PTR_ERR(keyboard->vdd),
				     "failed to acquire VDD GPIO\n");

	keyboard->reset = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(keyboard->reset))
		return dev_err_probe(dev, PTR_ERR(keyboard->reset),
				     "failed to acquire reset GPIO\n");

	keyboard->detect = devm_gpiod_get(dev, "detect", GPIOD_IN);
	if (IS_ERR(keyboard->detect))
		return dev_err_probe(dev, PTR_ERR(keyboard->detect),
				     "failed to acquire detect GPIO\n");

	platform_set_drvdata(pdev, keyboard);
	INIT_DELAYED_WORK(&keyboard->detect_work, nabu_keyboard_detect_work);

	/* Power the internal USB controller and release its active-low reset. */
	gpiod_set_value_cansleep(keyboard->vdd, 1);
	usleep_range(1000, 1500);
	gpiod_set_value_cansleep(keyboard->reset, 0);
	usleep_range(2000, 3000);

	keyboard->irq = gpiod_to_irq(keyboard->detect);
	if (keyboard->irq < 0) {
		ret = keyboard->irq;
		goto err_power_off;
	}

	ret = devm_request_threaded_irq(dev, keyboard->irq, NULL,
					nabu_keyboard_detect_irq,
					IRQF_TRIGGER_RISING |
					IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
					dev_name(dev), keyboard);
	if (ret)
		goto err_power_off;

	ret = device_create_file(dev, &dev_attr_connected);
	if (ret)
		goto err_power_off;

	device_init_wakeup(dev,
			   of_property_read_bool(dev->of_node, "wakeup-source"));
	connected = gpiod_get_value_cansleep(keyboard->detect);
	if (connected < 0) {
		ret = connected;
		goto err_remove_file;
	}
	nabu_keyboard_publish_state(keyboard, connected, true);

	return 0;

err_remove_file:
	device_remove_file(dev, &dev_attr_connected);
err_power_off:
	gpiod_set_value_cansleep(keyboard->reset, 1);
	gpiod_set_value_cansleep(keyboard->vdd, 0);
	return ret;
}

static void nabu_keyboard_remove(struct platform_device *pdev)
{
	struct nabu_keyboard *keyboard = platform_get_drvdata(pdev);

	device_remove_file(keyboard->dev, &dev_attr_connected);
	cancel_delayed_work_sync(&keyboard->detect_work);
	nabu_keyboard_publish_state(keyboard, false, true);
	gpiod_set_value_cansleep(keyboard->reset, 1);
	gpiod_set_value_cansleep(keyboard->vdd, 0);
}

static int nabu_keyboard_suspend(struct device *dev)
{
	struct nabu_keyboard *keyboard = dev_get_drvdata(dev);
	int ret;

	cancel_delayed_work_sync(&keyboard->detect_work);
	if (!device_may_wakeup(dev))
		return 0;

	ret = enable_irq_wake(keyboard->irq);
	if (!ret)
		keyboard->wake_enabled = true;
	return ret;
}

static int nabu_keyboard_resume(struct device *dev)
{
	struct nabu_keyboard *keyboard = dev_get_drvdata(dev);

	if (keyboard->wake_enabled) {
		disable_irq_wake(keyboard->irq);
		keyboard->wake_enabled = false;
	}
	mod_delayed_work(system_wq, &keyboard->detect_work,
			 msecs_to_jiffies(NABU_KEYBOARD_DEBOUNCE_MS));
	return 0;
}

static DEFINE_SIMPLE_DEV_PM_OPS(nabu_keyboard_pm_ops,
				nabu_keyboard_suspend, nabu_keyboard_resume);

static const struct of_device_id nabu_keyboard_of_match[] = {
	{ .compatible = "xiaomi,nabu-keyboard" },
	{ }
};
MODULE_DEVICE_TABLE(of, nabu_keyboard_of_match);

static struct platform_driver nabu_keyboard_driver = {
	.probe = nabu_keyboard_probe,
	.remove = nabu_keyboard_remove,
	.driver = {
		.name = "xiaomi-nabu-keyboard",
		.of_match_table = nabu_keyboard_of_match,
		.pm = pm_sleep_ptr(&nabu_keyboard_pm_ops),
	},
};
module_platform_driver(nabu_keyboard_driver);

MODULE_DESCRIPTION("Xiaomi Pad 5 pogo keyboard-cover controller");
MODULE_LICENSE("GPL");
