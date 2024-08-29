// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2023 Spacemit Co., Ltd.
 *
 */

#ifndef _SPACEMIT_MW_H_
#define _SPACEMIT_MW_H_

#include <linux/of.h>
#include <linux/device.h>
#include <video/videomode.h>

#include <drm/drm_print.h>
#include <drm/drm_writeback.h>
#include <drm/drm_encoder.h>
#include <drm/drm_connector.h>
#include <drm/drm_bridge.h>

#include "spacemit_lib.h"

enum spacemit_wb_loc {
	SPACEMIT_WB_COMP0 = 0,
	SPACEMIT_WB_COMP1,
	SPACEMIT_WB_COMP2,
	SPACEMIT_WB_COMP3,
	SPACEMIT_WB_COMP4,
	SPACEMIT_WB_RCH0 = 8,
	SPACEMIT_WB_RCH1,
	SPACEMIT_WB_RCH2,
	SPACEMIT_WB_RCH3,
	SPACEMIT_WB_RCH4,
	SPACEMIT_WB_RCH5,
	SPACEMIT_WB_RCH6,
	SPACEMIT_WB_RCH7,
	SPACEMIT_WB_RCH8,
	SPACEMIT_WB_RCH9,
	SPACEMIT_WB_RCH10,
	SPACEMIT_WB_RCH11,
	SPACEMIT_WB_POST0 = 24,
	SPACEMIT_WB_POST1,
	SPACEMIT_WB_POST2,
};

struct spacemit_wb_device {
	uint32_t id;
	struct videomode vm;
	int status;
};

struct spacemit_wb {
	struct device dev;
	struct drm_encoder encoder;
	struct spacemit_wb_device ctx;
};

void saturn_wb_config(struct spacemit_dpu *dpu);

#endif
