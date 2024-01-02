#ifndef __SPACEMIT_PMIC_H__
#define __SPACEMIT_PMIC_H__

#include <linux/regulator/machine.h>
#include <linux/regmap.h>

/**
 * this is only used for pm853
 */
struct spacemit_sub_pmic {
	struct i2c_client *power_page;
	struct regmap *power_regmap;
	unsigned short power_page_addr;
};

struct spacemit_pmic {
	struct i2c_client		*i2c;
	struct regmap_irq_chip_data	*irq_data;
	struct regmap			*regmap;
	unsigned int			variant;
	const struct regmap_config	*regmap_cfg;
	const struct regmap_irq_chip	*regmap_irq_chip;

	/**
	 * this is only used for pm853
	 */
	struct spacemit_sub_pmic *sub;
};

struct pin_func_desc {
	const char *name;
	unsigned char pin_id;
	unsigned char func_reg;
	unsigned char func_mask;
	unsigned char en_val;
	unsigned char ha_sub;
	unsigned char sub_reg;
	unsigned char sub_mask;
	unsigned char sube_val;
};

struct pin_config_desc {
	unsigned int pin_id;
	/* input config desc */
	struct {
		unsigned char reg;
		unsigned char msk;
	} input;

	/* output config desc */
	struct {
		unsigned char reg;
		unsigned char msk;
	} output;

	/* pull-down desc */
	struct {
		unsigned char reg;
		unsigned char msk;
	} pup;

	/* deb */
	struct {
		unsigned char reg;
		unsigned char timemsk;

		struct {
			unsigned char msk;
		} en;
	} deb;

	/* OD */
	struct {
		unsigned char reg;
		unsigned char msk;
	} od;

	struct {
		unsigned char reg;
		unsigned char msk;
	} itype;
};

struct chip_id_reg {
	unsigned char device_id_reg;
	unsigned char version_id_reg;
	unsigned char user_id_reg;
	unsigned char reg_num;
};

/* pmic ID configuration */
#define SPM8821_ID			0x0
#define PM853_ID			0x50

/* common regulator defination */
#define SPM8XX_DESC_COMMON(_id, _match, _supply, _nv, _vr, _vm, _er, _em, _lr, _ops)       \
	{								\
		.name		= (_match),				\
		.supply_name	= (_supply),				\
		.of_match	= of_match_ptr(_match),			\
		.regulators_node = of_match_ptr("regulators"),		\
		.ops		= _ops,			\
		.type		= REGULATOR_VOLTAGE,			\
		.id		= (_id),				\
		.n_voltages     = (_nv),				\
		.owner		= THIS_MODULE,				\
		.vsel_reg       = (_vr),				\
		.vsel_mask      = (_vm),				\
		.enable_reg	= (_er),				\
		.enable_mask	= (_em),				\
		.volt_table	= NULL,					\
		.linear_ranges	= (_lr),				\
		.n_linear_ranges	= ARRAY_SIZE(_lr),		\
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

#define SPM8XX_DESC_PIN_FUNC_COM(_pin_id, _match, _ereg, _emask, 	\
	_enval,	_hsub, _subreg, _submask, _subenval			\
	)								\
	{								\
		.name		= (_match),				\
		.pin_id		= (_pin_id),				\
		.func_reg	= (_ereg),				\
		.func_mask	= (_emask),				\
		.en_val		= (_enval),				\
		.ha_sub		= (_hsub),				\
		.sub_reg	= (_subreg),				\
		.sub_mask	= (_submask),				\
		.sube_val	= (_subenval),				\
	}

#define SPM8XX_DESC_PIN_CONFIG_COM(_pin_id, _ireg, _imsk, _oreg, _omsk,		\
	_pureg, _pumsk, _debreg, _debtmsk, _debemsk, _odreg, _odmsk,		\
	_itypereg, _itypemsk							\
	)							\
	{							\
		.pin_id = (_pin_id),				\
		.input = {					\
			.reg = (_ireg),				\
			.msk = (_imsk),				\
		},						\
		.output = {					\
			.reg = (_oreg),				\
			.msk = (_omsk),				\
		},						\
		.pup = {					\
			.reg = (_pureg),			\
			.msk = (_pumsk),			\
		},						\
		.deb = {					\
			.reg = (_debreg),			\
			.timemsk = (_debtmsk),			\
			.en.msk = (_debemsk)			\
		},						\
		.od = {						\
			.reg = (_odreg),			\
			.msk = (_odmsk),			\
		},						\
		.itype = {					\
			.reg = (_itypereg),			\
			.msk = (_itypemsk),			\
		},						\
	}

#include "spm8821.h"
#include "pm853.h"

#endif /* __SPACEMIT_PMIC_H__ */
