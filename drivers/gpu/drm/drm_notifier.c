// SPDX-License-Identifier: GPL-2.0-only
/*
 * Display blank notifier used by the Xiaomi Nabu touch controller.
 *
 * Copyright (c) 2019, The Linux Foundation. All rights reserved.
 * Copyright (C) 2021 Xiaomi, Inc.
 */

#include <drm/drm_notifier.h>

static BLOCKING_NOTIFIER_HEAD(mi_drm_notifier_list);

int mi_drm_register_client(struct notifier_block *nb)
{
	return blocking_notifier_chain_register(&mi_drm_notifier_list, nb);
}
EXPORT_SYMBOL_GPL(mi_drm_register_client);

int mi_drm_unregister_client(struct notifier_block *nb)
{
	return blocking_notifier_chain_unregister(&mi_drm_notifier_list, nb);
}
EXPORT_SYMBOL_GPL(mi_drm_unregister_client);

int mi_drm_notifier_call_chain(unsigned long val, void *v)
{
	return blocking_notifier_call_chain(&mi_drm_notifier_list, val, v);
}
EXPORT_SYMBOL_GPL(mi_drm_notifier_call_chain);
