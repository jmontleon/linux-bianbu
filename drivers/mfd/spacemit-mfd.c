// SPDX-License-Identifier: GPL-2.0-only

#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/mfd/core.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/regmap.h>
#include <linux/reboot.h>
#include <linux/ioport.h>
#include <linux/mfd/spacemit/spacemit_pmic.h>

SPM8821_REGMAP_CONFIG;
SPM8821_IRQS_DESC;
SPM8821_IRQ_CHIP_DESC;
SPM8821_POWER_KEY_RESOURCES_DESC;
SPM8821_RTC_RESOURCES_DESC;
SPM8821_MFD_CELL;
SPM8821_MFD_MATCH_DATA;

PM853_MFD_CELL;
PM853_REGMAP_CONFIG;
PM853_MFD_MATCH_DATA;

SY8810L_MFD_CELL;
SY8810L_REGMAP_CONFIG;
SY8810L_MFD_MATCH_DATA;

static const struct of_device_id spacemit_pmic_of_match[] = {
	{ .compatible = "spacemit,spm8821" , .data = (void *)&spm8821_mfd_match_data },
	{ .compatible = "spacemit,pm853" , .data = (void *)&pm853_mfd_match_data },
	{ .compatible = "spacemit,sy8810l" , .data = (void *)&sy8810l_mfd_match_data },
	{ },
};
MODULE_DEVICE_TABLE(of, spacemit_pmic_of_match);

static int spacemit_prepare_sub_pmic(struct spacemit_pmic *pmic)
{
	struct i2c_client *client = pmic->i2c;
	struct spacemit_sub_pmic *sub = pmic->sub;

	sub->power_page_addr = pmic->i2c->addr + 1;

	sub->power_page = i2c_new_dummy_device(client->adapter,
			sub->power_page_addr);
	if (sub->power_page == NULL)
		return -ENODEV;

	sub->power_regmap = devm_regmap_init_i2c(sub->power_page,
			pmic->regmap_cfg);
	if (IS_ERR(sub->power_regmap))
		return PTR_ERR(sub->power_regmap);

	regcache_cache_bypass(sub->power_regmap, true);

	i2c_set_clientdata(sub->power_page, pmic);

	return 0;
}

static int spacemit_pmic_probe(struct i2c_client *client,
		       const struct i2c_device_id *id)
{
	int ret;
	int nr_cells;
	struct device_node *np;
	struct spacemit_pmic *pmic;
	const struct mfd_cell *cells;
	const struct of_device_id *of_id;
	struct mfd_match_data *match_data;

	pmic = devm_kzalloc(&client->dev, sizeof(*pmic), GFP_KERNEL);
	if (!pmic) {
		pr_err("%s:%d, err\n", __func__, __LINE__);
		return -ENOMEM;
	}

	of_id = of_match_device(client->dev.driver->of_match_table, &client->dev);
	if (!of_id) {
		pr_err("Unable to match OF ID\n");
		return -ENODEV;
	}

	/* find the property in device node */
	np = of_find_compatible_node(NULL, NULL, of_id->compatible);
	if (!np)
		return 0;

	of_node_put(np);

	match_data = (struct mfd_match_data *)of_id->data;

	pmic->regmap_cfg = match_data->regmap_cfg;
	pmic->regmap_irq_chip = match_data->regmap_irq_chip;
	cells = match_data->mfd_cells;
	nr_cells = match_data->nr_cells;

	if (strcmp(match_data->name, "pm853") == 0) {
		pmic->sub = devm_kzalloc(&client->dev, sizeof(struct spacemit_sub_pmic), GFP_KERNEL);
		if (!pmic->sub)
			return -ENOMEM;

	}

	pmic->i2c = client;

	i2c_set_clientdata(client, pmic);

	pmic->regmap = devm_regmap_init_i2c(client, pmic->regmap_cfg);
	if (IS_ERR(pmic->regmap)) {
		pr_err("%s:%d, regmap initialization failed\n",
				__func__, __LINE__);
		return PTR_ERR(pmic->regmap);
	}

	regcache_cache_bypass(pmic->regmap, true);

	/* prepare sub pmic */
	if (pmic->sub) {
		ret = spacemit_prepare_sub_pmic(pmic);
		if (ret < 0) {
			pr_err("failed to prepare sub pmic %d\n", ret);
			return ret;
		}
	}

	if (!client->irq) {
		pr_warn("%s:%d, No interrupt supported\n",
				__func__, __LINE__);
	} else {
		if (pmic->regmap_irq_chip) {
			ret = regmap_add_irq_chip(pmic->regmap, client->irq, IRQF_ONESHOT, -1,
				pmic->regmap_irq_chip, &pmic->irq_data);
			if (ret) {
				pr_err("failed to add irqchip %d\n", ret);
				return ret;
			}
		}
	}

	ret = devm_mfd_add_devices(&client->dev, PLATFORM_DEVID_NONE,
			      cells, nr_cells, NULL, 0,
			      regmap_irq_get_domain(pmic->irq_data));
	if (ret) {
		pr_err("failed to add MFD devices %d\n", ret);
		return -EINVAL;
	}

	return 0;
}

static void spacemit_pmic_remove(struct i2c_client *client)
{
	/* !TODO */
}

static void spacemit_pmic_shutdown(struct i2c_client *client)
{
	/* !TODO */
}

static struct i2c_driver spacemit_pmic_i2c_driver = {
	.driver = {
		.name = "spacemit-pmic",
		.of_match_table = spacemit_pmic_of_match,
	},
	.probe    = spacemit_pmic_probe,
	.remove   = spacemit_pmic_remove,
	.shutdown = spacemit_pmic_shutdown,
};

module_i2c_driver(spacemit_pmic_i2c_driver);

MODULE_LICENSE("GPL");
