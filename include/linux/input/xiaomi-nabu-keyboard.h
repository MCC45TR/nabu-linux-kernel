/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _LINUX_INPUT_XIAOMI_NABU_KEYBOARD_H
#define _LINUX_INPUT_XIAOMI_NABU_KEYBOARD_H

#include <linux/types.h>

#if IS_ENABLED(CONFIG_INPUT_XIAOMI_NABU_TABLET_MODE)
void xiaomi_nabu_keyboard_update_mode(bool attached, bool computer_mode);
#else
static inline void xiaomi_nabu_keyboard_update_mode(bool attached,
					     bool computer_mode) { }
#endif

#endif
