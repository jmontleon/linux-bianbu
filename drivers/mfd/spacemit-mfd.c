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

static const struct of_device_id spacemit_pmic_of_match[] = {
	{ .compatible = "spacemit,spm8821" },
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

static int spacemit_pmic_probe(struct i2c_client *client,
		       const struct i2c_device_id *id)
{
	int ret;
	unsigned char pmic_id_reg, pmic_id;
	struct spacemit_pmic *pmic;
	const struct mfd_cell *cells;
	int nr_cells;
	struct device_node *np = client->dev.of_node;

	pmic = devm_kzalloc(&client->dev, sizeof(*pmic), GFP_KERNEL);
	if (!pmic) {
		pr_err("%s:%d, err\n", __func__, __LINE__);
		return -ENOMEM;
	}

	/* read the chip id */
	if (of_device_is_compatible(np, "spacemit,spm8821"))
		pmic_id_reg = SPACEMIT_SPM8821_ID_REG;
	else {
		pr_err("%s:%d, not supported\n", __func__, __LINE__);
		return -EINVAL;
	}

	ret = __spacemit_pmic_read_u8(client, pmic_id_reg, &pmic_id);
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
