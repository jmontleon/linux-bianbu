#ifndef __SPACEMIT_PMIC_H__
#define __SPACEMIT_PMIC_H__

#include <linux/regulator/machine.h>
#include <linux/regmap.h>

struct spacemit_pmic {
	struct i2c_client		*i2c;
	struct regmap_irq_chip_data	*irq_data;
	struct regmap			*regmap;
	unsigned char			variant;
	const struct regmap_config	*regmap_cfg;
	const struct regmap_irq_chip	*regmap_irq_chip;
};

/* pmic ID configuration */
#define SPM8821_ID			0x22

/* common regulator defination */
#define SPM8XX_DESC_COM(_id, _match, _supply, _min, _max, _step, _vreg,	\
	_vmask, _ereg, _emask, _enval, _disval, _etime, _ops)		\
	{								\
		.name		= (_match),				\
		.supply_name	= (_supply),				\
		.of_match	= of_match_ptr(_match),			\
		.regulators_node = of_match_ptr("regulators"),		\
		.type		= REGULATOR_VOLTAGE,			\
		.id		= (_id),				\
		.n_voltages	= (((_max) - (_min)) / (_step) + 1),	\
		.owner		= THIS_MODULE,				\
		.min_uV		= (_min) * 1000,			\
		.uV_step	= (_step) * 1000,			\
		.vsel_reg	= (_vreg),				\
		.vsel_mask	= (_vmask),				\
		.enable_reg	= (_ereg),				\
		.enable_mask	= (_emask),				\
		.enable_val     = (_enval),				\
		.disable_val     = (_disval),				\
		.enable_time	= (_etime),				\
		.ops		= _ops,			\
	}

#define SPM8XX_DESC_SWITCH_COM(_id, _match, _supply, _ereg, _emask,	\
	_enval, _disval, _ops)						\
	{								\
		.name		= (_match),				\
		.supply_name	= (_supply),				\
		.of_match	= of_match_ptr(_match),			\
		.regulators_node = of_match_ptr("regulators"),		\
		.type		= REGULATOR_VOLTAGE,			\
		.id		= (_id),				\
		.enable_reg	= (_ereg),				\
		.enable_mask	= (_emask),				\
		.enable_val     = (_enval),				\
		.disable_val     = (_disval),				\
		.owner		= THIS_MODULE,				\
		.ops		= _ops					\
	}

#include "spm8821.h"

#endif /* __SPACEMIT_PMIC_H__ */
