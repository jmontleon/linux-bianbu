// SPDX-License-Identifier: GPL-2.0-only

#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/mfd/core.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/regmap.h>
#include <linux/reboot.h>
#include <linux/mfd/spacemit/spacemit_pmic.h>

SPM8821_MFD_CELL;
SPM8821_REGMAP_CONFIG;

PM853_MFD_CELL;
PM853_REGMAP_CONFIG;

static const struct of_device_id spacemit_pmic_of_match[] = {
	{ .compatible = "spacemit,spm8821" , .data = (void *)SPACEMIT_SPM8821_ID_REG },
	{ .compatible = "spacemit,pm853" , .data = (void *)SPACEMIT_PM853_ID_REG },
	{ },
};
MODULE_DEVICE_TABLE(of, spacemit_pmic_of_match);

static int __spacemit_pmic_read_u8(struct i2c_client *c, u8 reg, u8 *val)
{
	struct i2c_msg msg[2];
	int ret;
	u8 buf;

	msg[0].addr = c->addr;
	msg[0].flags = 0;
	msg[0].len = 1;
	msg[0].buf = &reg;

	msg[1].addr = c->addr;
	msg[1].flags = I2C_M_RD;
	msg[1].len = 1;
	msg[1].buf = (char *)&buf;

	ret = i2c_transfer(c->adapter, msg, 2);
	if (ret < 0) {
		dev_err(&c->dev, "i2c read transfer error: %d\n", ret);
		return ret;
	}

	*val = buf;

	return 0;
}

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

	i2c_set_clientdata(sub->power_page, pmic);

	return 0;
}

static int spacemit_pmic_probe(struct i2c_client *client,
		       const struct i2c_device_id *id)
{
	int ret;
	int nr_cells;
	unsigned char pmic_id;
	unsigned long pmic_id_reg;
	struct spacemit_pmic *pmic;
	const struct mfd_cell *cells;
	const struct of_device_id *of_id;

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

	pmic_id_reg = (unsigned long)of_id->data;

	ret = __spacemit_pmic_read_u8(client, (unsigned char)pmic_id_reg, &pmic_id);
	if (ret) {
		pr_err("%s:%d, read pmic id failed\n", __func__, __LINE__);
		return -EINVAL;
	}

	pmic->variant = pmic_id;

	switch (pmic->variant) {
	case SPM8821_ID:
		pmic->regmap_cfg = &spm8821_regmap_config;
		/* pmic->regmap_irq_chip = ; */
		cells = spm8821;
		nr_cells = ARRAY_SIZE(spm8821);
		break;
	case PM853_ID:
		pmic->regmap_cfg = &pm853_regmap_config;
		cells = pm853;
		nr_cells = ARRAY_SIZE(pm853);
		pmic->sub = devm_kzalloc(&client->dev, sizeof(struct spacemit_sub_pmic), GFP_KERNEL);
		if (!pmic->sub)
			return -ENOMEM;
		break;
	default:
		pr_err("%s:%d, Unsupported SPACEMIT ID :%d\n",
				__func__, __LINE__, pmic->variant);
		return -EINVAL;
	}

	pmic->i2c = client;

	i2c_set_clientdata(client, pmic);

	pmic->regmap = devm_regmap_init_i2c(client, pmic->regmap_cfg);
	if (IS_ERR(pmic->regmap)) {
		pr_err("%s:%d, regmap initialization failed\n",
				__func__, __LINE__);
		return PTR_ERR(pmic->regmap);
	}

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
	}

	ret = devm_mfd_add_devices(&client->dev, PLATFORM_DEVID_NONE,
			      cells, nr_cells, NULL, 0, NULL);
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
