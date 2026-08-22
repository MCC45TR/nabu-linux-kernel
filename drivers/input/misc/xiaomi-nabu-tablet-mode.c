// SPDX-License-Identifier: GPL-2.0-only
/*
 * Xiaomi Pad 5 permanent tablet-mode input switch.
 *
 * Nabu has no convertible hinge: it is always a tablet. Exposing that fact
 * through the standard SW_TABLET_MODE interface keeps desktop touch mode and
 * automatic rotation active when a USB mouse or recovery bridge is attached.
 */

#include <linux/input.h>
#include <linux/module.h>
#include <linux/of.h>

static struct input_dev *nabu_tablet_mode_input;

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

	nabu_tablet_mode_input = input;
	input_report_switch(input, SW_TABLET_MODE, 1);
	input_sync(input);

	return 0;
}
module_init(nabu_tablet_mode_init);

static void __exit nabu_tablet_mode_exit(void)
{
	if (nabu_tablet_mode_input)
		input_unregister_device(nabu_tablet_mode_input);
}
module_exit(nabu_tablet_mode_exit);

MODULE_DESCRIPTION("Xiaomi Pad 5 permanent tablet-mode switch");
MODULE_LICENSE("GPL");
