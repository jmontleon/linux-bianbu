// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2023 Spacemit Co., Ltd.
 *
 */

#ifndef _SPACEMIT_BOOTLOADER_H_
#define _SPACEMIT_BOOTLOADER_H_

#include <linux/of_reserved_mem.h>

int spacemit_dpu_free_bootloader_mem(struct reserved_mem *rmem);

#endif
