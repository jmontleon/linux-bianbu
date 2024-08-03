// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2023 Spacemit Co., Ltd.
 *
 */

#ifndef __SPACEMIT_PANEL_H__
#define __SPACEMIT_PANEL_H__

#include <linux/notifier.h>

/*  complete the definition of DRM Macros */
enum{
	DRM_PANEL_EARLY_EVENT_BLANK = 0,
	DRM_PANEL_EVENT_BLANK,
	DRM_PANEL_BLANK_UNBLANK,
	DRM_PANEL_BLANK_POWERDOWN,
};

extern int spacemit_drm_register_client(struct notifier_block *nb);
extern int spacemit_drm_unregister_client(struct notifier_block *nb);

#endif
