#ifndef _SPACEMIT_PLATFORM_PM_OPS_H
#define _SPACEMIT_PLATFORM_PM_OPS_H

#include <linux/list.h>

struct platfrom_pm_ops {
	struct list_head node;
	int (*prepare_late)(void);
	void (*wake)(void);
};

extern void register_platform_pm_ops(struct platfrom_pm_ops *ops);
extern void unregister_platform_pm_ops(struct platfrom_pm_ops *ops);
#ifdef CONFIG_PM_SLEEP
extern int platform_pm_prepare_late_suspend(void);
extern void platform_pm_resume_wake(void);
#endif

#endif
