// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2024 Spacemit Co., Ltd.
 *
 */

#ifndef __SPACEMIT_MISC_H__
#define __SPACEMIT_MISC_H__

#include <linux/cpufreq.h>

struct private_data;

extern void dwc3_spacemit_clear_disconnect(struct device *dev);

extern void remove_boost_sysfs_file(void);
extern void remove_policy_boost_sysfs_file(struct cpufreq_policy *policy);

extern void spacemit_sdio_detect_change(int enable_scan);

extern void cpufreq_dt_add_data(struct private_data *priv);
extern struct private_data *cpufreq_dt_find_data(int cpu);
extern int spacmeit_cpufreq_veritfy(struct cpufreq_policy_data *policy);
extern void spacemit_cpufreq_ready(struct cpufreq_policy *policy);

#endif //__SPACEMIT_MISC_H__
