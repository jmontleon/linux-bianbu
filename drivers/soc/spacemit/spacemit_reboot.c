// SPDX-License-Identifier: GPL-2.0
/*
 * Spacemit k1x soc fastboot mode reboot
 */

#include <linux/device.h>
#include <linux/errno.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/printk.h>
#include <linux/module.h>
#include <linux/reboot.h>
#include <linux/io.h>

#define RESET_REG_VALUE 0x55a
#define RESET_REG_VALUE1 0x55f
static char *rebootcmd = "fastboot";
static char *shellcmd = "uboot";

struct spacemit_reboot_ctrl {
	void __iomem *base;
	struct notifier_block reset_handler;
};

static int k1x_reset_handler(struct notifier_block *this, unsigned long mode,
		void *cmd)
{
	struct spacemit_reboot_ctrl *info = container_of(this,struct spacemit_reboot_ctrl,
			reset_handler);

	if(cmd != NULL && !strcmp(cmd, rebootcmd))
		writel(RESET_REG_VALUE, info->base);

	if(cmd != NULL && !strcmp(cmd, shellcmd))
                writel(RESET_REG_VALUE1, info->base);

	return NOTIFY_DONE;
}

static const struct of_device_id spacemit_reboot_of_match[] = {
	{.compatible = "spacemit,k1x-reboot"},
	{},
};
MODULE_DEVICE_TABLE(of, spacemit_reboot_of_match);

static int spacemit_reboot_probe(struct platform_device *pdev)
{
	struct spacemit_reboot_ctrl *info;
	int ret;

	info = devm_kzalloc(&pdev->dev, sizeof(struct spacemit_reboot_ctrl), GFP_KERNEL);
	if(info == NULL)
		return -ENOMEM;

	info->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(info->base))
		return PTR_ERR(info->base);

	platform_set_drvdata(pdev, info);

	info->reset_handler.notifier_call = k1x_reset_handler;
	info->reset_handler.priority = 128;
	ret = register_restart_handler(&info->reset_handler);
	if (ret) {
		dev_warn(&pdev->dev, "cannot register restart handler: %d\n",
			 ret);
	}

	return 0;
}

static int spacemit_reboot_remove(struct platform_device *pdev)
{
	struct spacemit_reboot_ctrl *info = platform_get_drvdata(pdev);

	unregister_restart_handler(&info->reset_handler);
	return 0;
}

static struct platform_driver spacemit_reboot_driver = {
	.driver = {
		.name = "spacemit-reboot",
		.of_match_table = of_match_ptr(spacemit_reboot_of_match),
	},
	.probe = spacemit_reboot_probe,
	.remove = spacemit_reboot_remove,
};

module_platform_driver(spacemit_reboot_driver);
MODULE_DESCRIPTION("K1x fastboot mode reboot");
MODULE_AUTHOR("Spacemit");
MODULE_LICENSE("GPL v2");
