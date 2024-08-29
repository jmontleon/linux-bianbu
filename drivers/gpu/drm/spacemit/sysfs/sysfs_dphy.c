// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2023 Spacemit Co., Ltd.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/platform_device.h>
#include <linux/sysfs.h>

#include "../spacemit_lib.h"
#include "../spacemit_dphy.h"
#include "sysfs_display.h"



int spacemit_dphy_sysfs_init(struct device *dev)
{
	int rc = 0;
/*
	rc = sysfs_create_groups(&dev->kobj, dphy_groups);
	if (rc)
		pr_err("create dphy attr node failed, rc=%d\n", rc);
*/
	return rc;
}
EXPORT_SYMBOL(spacemit_dphy_sysfs_init);


MODULE_DESCRIPTION("Provide mipi dsi phy attribute nodes for userspace");
MODULE_LICENSE("GPL v2");

