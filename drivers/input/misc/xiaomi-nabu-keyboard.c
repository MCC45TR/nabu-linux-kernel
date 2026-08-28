// SPDX-License-Identifier: GPL-2.0-only
/* Xiaomi Pad 5 pogo keyboard-cover power, presence and mode controller. */

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/mutex.h>
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
	struct mutex lock;
	int irq;
	bool connected;
	bool computer_mode;
	bool wake_enabled;
};

static void nabu_keyboard_publish_state(struct nabu_keyboard *keyboard,
					bool connected, bool force)
{
	char connected_state[40];
	char mode_state[40];
	char *envp[] = { connected_state, mode_state, NULL };
	bool computer_mode;

	mutex_lock(&keyboard->lock);
	if (!force && keyboard->connected == connected) {
		mutex_unlock(&keyboard->lock);
		return;
	}

	keyboard->connected = connected;
	if (!connected)
		keyboard->computer_mode = false;
	computer_mode = keyboard->computer_mode;
	xiaomi_nabu_keyboard_update_mode(keyboard->connected,
					  computer_mode);
	snprintf(connected_state, sizeof(connected_state),
		 "XIAOMI_KEYBOARD_CONNECTED=%u", keyboard->connected);
	snprintf(mode_state, sizeof(mode_state),
		 "XIAOMI_KEYBOARD_COMPUTER_MODE=%u", computer_mode);
	mutex_unlock(&keyboard->lock);

	kobject_uevent_env(&keyboard->dev->kobj, KOBJ_CHANGE, envp);
	dev_info(keyboard->dev, "keyboard cover %s; computer mode %s\n",
		 connected ? "attached" : "detached",
		 computer_mode ? "enabled" : "disabled");
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

	/*
	 * A cover transition while the system is still awake must not leave a
	 * wakeup event pending.  In particular, an attach/detach event during
	 * the desktop's pre-suspend screen blank otherwise makes the freezer
	 * abort the subsequent system suspend.
	 *
	 * Once the device PM suspend callback has armed the detect IRQ as a
	 * wake source, keep the event long enough for userspace to finish the
	 * resume path.  The IRQ itself remains enabled in both states so cover
	 * presence reporting is unaffected.
	 */
	if (READ_ONCE(keyboard->wake_enabled))
		pm_wakeup_event(keyboard->dev, 500);
	mod_delayed_work(system_wq, &keyboard->detect_work,
			 msecs_to_jiffies(NABU_KEYBOARD_DEBOUNCE_MS));
	return IRQ_HANDLED;
}

static ssize_t connected_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct nabu_keyboard *keyboard = dev_get_drvdata(dev);
	bool connected;

	mutex_lock(&keyboard->lock);
	connected = keyboard->connected;
	mutex_unlock(&keyboard->lock);

	return sysfs_emit(buf, "%u\n", connected);
}
static DEVICE_ATTR_RO(connected);

static ssize_t computer_mode_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct nabu_keyboard *keyboard = dev_get_drvdata(dev);
	bool computer_mode;

	mutex_lock(&keyboard->lock);
	computer_mode = keyboard->computer_mode;
	mutex_unlock(&keyboard->lock);

	return sysfs_emit(buf, "%u\n", computer_mode);
}

static ssize_t computer_mode_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t count)
{
	struct nabu_keyboard *keyboard = dev_get_drvdata(dev);
	char mode_state[40];
	char *envp[] = { mode_state, NULL };
	bool computer_mode;
	int ret;

	ret = kstrtobool(buf, &computer_mode);
	if (ret)
		return ret;

	mutex_lock(&keyboard->lock);
	if (computer_mode && !keyboard->connected) {
		mutex_unlock(&keyboard->lock);
		return -ENODEV;
	}
	if (keyboard->computer_mode == computer_mode) {
		mutex_unlock(&keyboard->lock);
		return count;
	}

	keyboard->computer_mode = computer_mode;
	xiaomi_nabu_keyboard_update_mode(keyboard->connected,
					  keyboard->computer_mode);
	snprintf(mode_state, sizeof(mode_state),
		 "XIAOMI_KEYBOARD_COMPUTER_MODE=%u", keyboard->computer_mode);
	mutex_unlock(&keyboard->lock);

	kobject_uevent_env(&keyboard->dev->kobj, KOBJ_CHANGE, envp);
	dev_info(keyboard->dev, "computer mode %s\n",
		 computer_mode ? "enabled" : "disabled");
	return count;
}
static DEVICE_ATTR_RW(computer_mode);

static struct attribute *nabu_keyboard_attrs[] = {
	&dev_attr_connected.attr,
	&dev_attr_computer_mode.attr,
	NULL,
};
ATTRIBUTE_GROUPS(nabu_keyboard);

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
	mutex_init(&keyboard->lock);
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

	device_init_wakeup(dev,
			   of_property_read_bool(dev->of_node, "wakeup-source"));
	connected = gpiod_get_value_cansleep(keyboard->detect);
	if (connected < 0) {
		ret = connected;
		goto err_power_off;
	}
	nabu_keyboard_publish_state(keyboard, connected, true);

	return 0;

err_power_off:
	gpiod_set_value_cansleep(keyboard->reset, 1);
	gpiod_set_value_cansleep(keyboard->vdd, 0);
	return ret;
}

static void nabu_keyboard_remove(struct platform_device *pdev)
{
	struct nabu_keyboard *keyboard = platform_get_drvdata(pdev);

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
		WRITE_ONCE(keyboard->wake_enabled, true);
	return ret;
}

static int nabu_keyboard_resume(struct device *dev)
{
	struct nabu_keyboard *keyboard = dev_get_drvdata(dev);

	if (keyboard->wake_enabled) {
		disable_irq_wake(keyboard->irq);
		WRITE_ONCE(keyboard->wake_enabled, false);
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
		.dev_groups = nabu_keyboard_groups,
	},
};
module_platform_driver(nabu_keyboard_driver);

MODULE_DESCRIPTION("Xiaomi Pad 5 pogo keyboard-cover controller");
MODULE_LICENSE("GPL");
