// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2023, spacemit Corporation.
 */

#include <linux/mtd/spi-nor.h>

#include "core.h"

static const struct flash_info fmsh_nor_parts[] = {
	{
		.id = SNOR_ID(0xa1, 0x40, 0x17),
		.name = "FM25Q64AI3",
		.size = SZ_8M,
		.no_sfdp_flags = SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ,
	},
};

const struct spi_nor_manufacturer spi_nor_fmsh = {
	.name = "fmsh",
	.parts = fmsh_nor_parts,
	.nparts = ARRAY_SIZE(fmsh_nor_parts),
};
