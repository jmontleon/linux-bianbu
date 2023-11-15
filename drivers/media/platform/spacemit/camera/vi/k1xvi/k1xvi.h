// SPDX-License-Identifier: GPL-2.0
/*
 * k1xvi.h - k1xisp vi platform device driver
 *
 * Copyright(C) 2023 SPACEMIT Micro Limited
 */

#ifndef _SPACEMIT_K1XISP_H_
#define _SPACEMIT_K1XISP_H_
#include <media/media-entity.h>
#include "../cam_block.h"
#include "../mlink.h"

enum {
	DMA = 0,
	MIPI,
	FE_ISP,
	GRP_MAX,
};

enum {
	//for MIPI
	SENSOR = 0,
	CSI_MAIN,
	CSI_VCDT,
	//for DMA
	AIN = 0,
	AOUT,
	//for FE_ISP
	RAWDUMP = 0,
	OFFLINE_CHANNEL,
	PIPE,
	FORMATTER,
	DWT0,
	DWT1,
	HDR_COMBINE,
	IDI,
	SUB_MAX,
};

#define ID_MAX		(16)

#define AIN_NUM		(2)
#define AOUT_NUM	(14)
#define SENSOR_NUM	(3)

struct k1xvi_platform_data {
	struct media_entity *entities[GRP_MAX][SUB_MAX][ID_MAX];
	struct platform_device *pdev;
	void *isp_ctx;
	struct isp_firm *isp_firm;
	struct spm_camera_sensor_ops *sensor_ops;
};

int k1xvi_register_isp_firmware(struct isp_firm *isp_firm);
int k1xvi_register_sensor_ops(struct spm_camera_sensor_ops *sensor_ops);
int k1xvi_power(int on_off);
struct platform_device *k1xvi_get_platform_device(void);
#endif
