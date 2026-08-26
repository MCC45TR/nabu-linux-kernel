// SPDX-License-Identifier: GPL-2.0-only
/*
 * Xiaomi Pad 5 dynamic tablet-mode input switch.
 *
 * Nabu is a tablet unless its keyboard cover is physically attached. Exposing
 * that state through SW_TABLET_MODE lets desktop environments switch between
 * touch-first tablet mode and keyboard-first computer mode without mistaking
 * an unrelated USB pointer for a convertible keyboard.
 */

#include <linux/input.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>

#include <linux/input/xiaomi-nabu-keyboard.h>

static struct input_dev *nabu_tablet_mode_input;
static DEFINE_MUTEX(nabu_tablet_mode_lock);
static bool nabu_keyboard_attached;

void xiaomi_nabu_keyboard_set_attached(bool attached)
{
	mutex_lock(&nabu_tablet_mode_lock);
	nabu_keyboard_attached = attached;
	if (nabu_tablet_mode_input) {
		input_report_switch(nabu_tablet_mode_input, SW_TABLET_MODE,
				    !attached);
		input_sync(nabu_tablet_mode_input);
	}
	mutex_unlock(&nabu_tablet_mode_lock);
}
EXPORT_SYMBOL_GPL(xiaomi_nabu_keyboard_set_attached);

static int __init nabu_tablet_mode_init(void)
{
	struct input_dev *input;
	int ret;

	if (!of_machine_is_compatible("xiaomi,nabu"))
		return 0;

	input = input_allocate_device();
	if (!input)
		return -ENOMEM;

	input->name = "Xiaomi Pad 5 Tablet Mode Switch";
	input->phys = "nabu/tablet-mode";
	input->id.bustype = BUS_HOST;
	input_set_capability(input, EV_SW, SW_TABLET_MODE);

	ret = input_register_device(input);
	if (ret) {
		input_free_device(input);
		return ret;
	}

	mutex_lock(&nabu_tablet_mode_lock);
	nabu_tablet_mode_input = input;
	input_report_switch(input, SW_TABLET_MODE, !nabu_keyboard_attached);
	input_sync(input);
	mutex_unlock(&nabu_tablet_mode_lock);

	return 0;
}
module_init(nabu_tablet_mode_init);

static void __exit nabu_tablet_mode_exit(void)
{
	struct input_dev *input;

	mutex_lock(&nabu_tablet_mode_lock);
	input = nabu_tablet_mode_input;
	nabu_tablet_mode_input = NULL;
	mutex_unlock(&nabu_tablet_mode_lock);

	if (input)
		input_unregister_device(input);
}
module_exit(nabu_tablet_mode_exit);

MODULE_DESCRIPTION("Xiaomi Pad 5 keyboard-aware tablet-mode switch");
MODULE_LICENSE("GPL");
