#ifndef __SMP8821_H__
#define __SMP8821_H__

enum SPM8821_reg {
	SPM8821_ID_DCDC1,
	SPM8821_ID_DCDC2,
	SPM8821_ID_DCDC3,
	SPM8821_ID_DCDC4,
	SPM8821_ID_DCDC5,
	SPM8821_ID_DCDC6,
	SPM8821_ID_ALDO1,
	SPM8821_ID_ALDO2,
	SPM8821_ID_ALDO3,
	SPM8821_ID_ALDO4,
	SPM8821_ID_DLDO1,
	SPM8821_ID_DLDO2,
	SPM8821_ID_DLDO3,
	SPM8821_ID_DLDO4,
	SPM8821_ID_DLDO5,
	SPM8821_ID_DLDO6,
	SPM8821_ID_DLDO7,
	SPM8821_ID_SWITCH1,
};

#define SPACEMIT_SPM8821_MAX_REG	0xA8

#define SPM8821_BUCK_VSEL_MASK		0xff
#define SMP8821_BUCK_EN_MASK		0x1

#define SPM8821_BUCK1_CTRL_REG		0x47
#define SPM8821_BUCK2_CTRL_REG		0x4a
#define SPM8821_BUCK3_CTRL_REG		0x4d
#define SPM8821_BUCK4_CTRL_REG		0x50
#define SPM8821_BUCK5_CTRL_REG		0x53
#define SPM8821_BUCK6_CTRL_REG		0x56

#define SPM8821_BUCK1_VSEL_REG		0x48
#define SPM8821_BUCK2_VSEL_REG		0x4b
#define SPM8821_BUCK3_VSEL_REG		0x4e
#define SPM8821_BUCK4_VSEL_REG		0x51
#define SPM8821_BUCK5_VSEL_REG		0x54
#define SPM8821_BUCK6_VSEL_REG		0x57

#define SPM8821_ALDO1_CTRL_REG		0x5b
#define SPM8821_ALDO2_CTRL_REG		0x5e
#define SPM8821_ALDO3_CTRL_REG		0x61
#define SPM8821_ALDO4_CTRL_REG		0x64

#define SPM8821_ALDO1_VOLT_REG		0x5c
#define SPM8821_ALDO2_VOLT_REG		0x5f
#define SPM8821_ALDO3_VOLT_REG		0x62
#define SPM8821_ALDO4_VOLT_REG		0x65

#define SPM8821_ALDO_EN_MASK		0x1
#define SPM8821_ALDO_VSEL_MASK		0x3f

#define SPM8821_DLDO1_CTRL_REG		0x67
#define SPM8821_DLDO2_CTRL_REG		0x6a
#define SPM8821_DLDO3_CTRL_REG		0x6d
#define SPM8821_DLDO4_CTRL_REG		0x70
#define SPM8821_DLDO5_CTRL_REG		0x73
#define SPM8821_DLDO6_CTRL_REG		0x76
#define SPM8821_DLDO7_CTRL_REG		0x79

#define SPM8821_DLDO1_VOLT_REG		0x68
#define SPM8821_DLDO2_VOLT_REG		0x6b
#define SPM8821_DLDO3_VOLT_REG		0x6e
#define SPM8821_DLDO4_VOLT_REG		0x71
#define SPM8821_DLDO5_VOLT_REG		0x74
#define SPM8821_DLDO6_VOLT_REG		0x77
#define SPM8821_DLDO7_VOLT_REG		0x7a

#define SPM8821_DLDO_EN_MASK		0x1
#define SPM8821_DLDO_VSEL_MASK		0x3f

#define SPM8821_SWITCH_CTRL_REG		0x59
#define SPM8821_SWTICH_EN_MASK		0x1

#define SPM8821_CHIP_ID_REG							\
	static const struct chip_id_reg spm8821_id_reg = {			\
		.reg_num = 3,						\
		.device_id_reg = 0xa0,					\
		.version_id_reg = 0xa1,					\
		.user_id_reg = 0xa2,					\
	};

/* mfd configuration */
#define SPM8821_MFD_CELL	\
	static const struct mfd_cell spm8821[] = {				\
		{								\
			.name = "spacemit-regulator",				\
		},								\
		{ 								\
			.of_compatible = "spacemit,pmic,spm8821-pinctrl",	\
			.name = "spacemit-pmic-pinctrl",			\
		},								\
	};

#define SPM8821_REGMAP_CONFIG	\
	static const struct regmap_config spm8821_regmap_config = {	\
		.reg_bits = 8,	\
		.val_bits = 8,	\
		.max_register = SPACEMIT_SPM8821_MAX_REG,	\
		.cache_type = REGCACHE_RBTREE,	\
	};

/* regulator configuration */
#define SPM8821_DESC(_id, _match, _supply, _nv, _vr, _vm, _er, _em, _lr)	\
	SPM8XX_DESC_COMMON(_id, _match, _supply, _nv, _vr, _vm, _er, _em, _lr,	\
			&pmic_dcdc_ldo_ops)

#define SPM8821_DESC_SWITCH(_id, _match, _supply, _ereg, _emask)	\
	SPM8XX_DESC_SWITCH_COM(_id, _match, _supply, _ereg, _emask,	\
	0, 0, &pmic_switch_ops)


#define SPM8821_BUCK_LINER_RANGE					\
static const struct linear_range spm8821_buck_ranges[] = {		\
        REGULATOR_LINEAR_RANGE(500000, 0x0, 0xaa, 5000),		\
        REGULATOR_LINEAR_RANGE(1375000, 0xab, 0xfe, 25000),		\
};


#define SPM8821_LDO_LINER_RANGE						\
static const struct linear_range spm8821_ldo_ranges[] = {		\
        REGULATOR_LINEAR_RANGE(500000, 0xb, 0x7f, 25000),		\
};

#define SPM8821_REGULATOR_DESC		\
static const struct regulator_desc spm8821_reg[] = {	\
	/* BUCK */		\
	SPM8821_DESC(SPM8821_ID_DCDC1, "DCDC_REG1", "vcc_sys",		\
			255, SPM8821_BUCK1_VSEL_REG, SPM8821_BUCK_VSEL_MASK,	\
			SPM8821_BUCK1_CTRL_REG, SMP8821_BUCK_EN_MASK,		\
			spm8821_buck_ranges),	\
	\
	SPM8821_DESC(SPM8821_ID_DCDC2, "DCDC_REG2", "vcc_sys",		\
			255, SPM8821_BUCK2_VSEL_REG, SPM8821_BUCK_VSEL_MASK,	\
			SPM8821_BUCK2_CTRL_REG, SMP8821_BUCK_EN_MASK,	\
			spm8821_buck_ranges),	\
	\
	SPM8821_DESC(SPM8821_ID_DCDC3, "DCDC_REG3", "vcc_sys",		\
			255, SPM8821_BUCK3_VSEL_REG, SPM8821_BUCK_VSEL_MASK,	\
			SPM8821_BUCK3_CTRL_REG, SMP8821_BUCK_EN_MASK,	\
			spm8821_buck_ranges),	\
	\
	SPM8821_DESC(SPM8821_ID_DCDC4, "DCDC_REG4", "vcc_sys",	\
			255, SPM8821_BUCK4_VSEL_REG, SPM8821_BUCK_VSEL_MASK,	\
			SPM8821_BUCK4_CTRL_REG, SMP8821_BUCK_EN_MASK,	\
			spm8821_buck_ranges),	\
	\
	SPM8821_DESC(SPM8821_ID_DCDC5, "DCDC_REG5", "vcc_sys",		\
			255, SPM8821_BUCK5_VSEL_REG, SPM8821_BUCK_VSEL_MASK,	\
			SPM8821_BUCK5_CTRL_REG, SMP8821_BUCK_EN_MASK,	\
			spm8821_buck_ranges),	\
	\
	SPM8821_DESC(SPM8821_ID_DCDC6, "DCDC_REG6", "vcc_sys",	\
			255, SPM8821_BUCK6_VSEL_REG, SPM8821_BUCK_VSEL_MASK,	\
			SPM8821_BUCK6_CTRL_REG, SMP8821_BUCK_EN_MASK,	\
			spm8821_buck_ranges),	\
	/* ALDO */	\
	SPM8821_DESC(SPM8821_ID_ALDO1, "ALDO_REG1", "vcc_sys",	\
			128, SPM8821_ALDO1_VOLT_REG, SPM8821_ALDO_VSEL_MASK,	\
			SPM8821_ALDO1_CTRL_REG, SPM8821_ALDO_EN_MASK, spm8821_ldo_ranges),	\
	\
	SPM8821_DESC(SPM8821_ID_ALDO2, "ALDO_REG2", "vcc_sys",	\
			128, SPM8821_ALDO2_VOLT_REG, SPM8821_ALDO_VSEL_MASK,	\
			SPM8821_ALDO2_CTRL_REG, SPM8821_ALDO_EN_MASK, spm8821_ldo_ranges),	\
	\
	SPM8821_DESC(SPM8821_ID_ALDO3, "ALDO_REG3", "vcc_sys",	\
			128, SPM8821_ALDO3_VOLT_REG, SPM8821_ALDO_VSEL_MASK,	\
			SPM8821_ALDO3_CTRL_REG, SPM8821_ALDO_EN_MASK, spm8821_ldo_ranges),	\
	\
	SPM8821_DESC(SPM8821_ID_ALDO4, "ALDO_REG4", "vcc_sys",	\
			128, SPM8821_ALDO4_VOLT_REG, SPM8821_ALDO_VSEL_MASK,	\
			SPM8821_ALDO4_CTRL_REG, SPM8821_ALDO_EN_MASK, spm8821_ldo_ranges),	\
	\
	/* DLDO */	\
	SPM8821_DESC(SPM8821_ID_DLDO1, "DLDO_REG1", "dcdc5",		\
			128, SPM8821_DLDO1_VOLT_REG, SPM8821_DLDO_VSEL_MASK,	\
			SPM8821_DLDO1_CTRL_REG, SPM8821_DLDO_EN_MASK, spm8821_ldo_ranges),	\
	\
	SPM8821_DESC(SPM8821_ID_DLDO2, "DLDO_REG2", "dcdc5",	\
			128, SPM8821_DLDO2_VOLT_REG, SPM8821_DLDO_VSEL_MASK,	\
			SPM8821_DLDO2_CTRL_REG, SPM8821_DLDO_EN_MASK, spm8821_ldo_ranges),	\
	\
	SPM8821_DESC(SPM8821_ID_DLDO3, "DLDO_REG3", "dcdc5",		\
			128, SPM8821_DLDO3_VOLT_REG, SPM8821_DLDO_VSEL_MASK,	\
			SPM8821_DLDO3_CTRL_REG, SPM8821_DLDO_EN_MASK, spm8821_ldo_ranges),	\
	\
	SPM8821_DESC(SPM8821_ID_DLDO4, "DLDO_REG4", "dcdc5",		\
			128, SPM8821_DLDO4_VOLT_REG, SPM8821_DLDO_VSEL_MASK,	\
			SPM8821_DLDO4_CTRL_REG, SPM8821_DLDO_EN_MASK, spm8821_ldo_ranges),	\
	\
	SPM8821_DESC(SPM8821_ID_DLDO5, "DLDO_REG5", "dcdc5",		\
			128, SPM8821_DLDO5_VOLT_REG, SPM8821_DLDO_VSEL_MASK,	\
			SPM8821_DLDO5_CTRL_REG, SPM8821_DLDO_EN_MASK, spm8821_ldo_ranges),	\
	\
	SPM8821_DESC(SPM8821_ID_DLDO6, "DLDO_REG6", "dcdc5",		\
			128, SPM8821_DLDO6_VOLT_REG, SPM8821_DLDO_VSEL_MASK,	\
			SPM8821_DLDO6_CTRL_REG, SPM8821_DLDO_EN_MASK, spm8821_ldo_ranges),	\
	\
	SPM8821_DESC(SPM8821_ID_DLDO7, "DLDO_REG7", "dcdc5",		\
			128, SPM8821_DLDO7_VOLT_REG, SPM8821_DLDO_VSEL_MASK,	\
			SPM8821_DLDO7_CTRL_REG, SPM8821_DLDO_EN_MASK, spm8821_ldo_ranges),	\
	\
	/* PWR SWITCH */	\
	SPM8821_DESC_SWITCH(SPM8821_ID_SWITCH1, "SWITCH_REG1", "dcdc4", SPM8821_SWITCH_CTRL_REG, SPM8821_SWTICH_EN_MASK),		\
};

/* gpio set */
#define SPM8821_PINMUX_DESC		\
const char* spm8821_pinmux_functions[] = {	\
	"gpioin", "gpioout", "exten", "pwrctrl",	\
	"sleep", "nreset", "adcin"			\
};

#define SPM8821_PINFUNC_DESC	\
static const struct pin_func_desc spm8821_pinfunc_desc[] = {	\
	/* PIN0 gpioin */				\
	SPM8XX_DESC_PIN_FUNC_COM(0, "gpioin", 0x8, 0x3, 0, 0, 0, 0, 0),				\
	/* PIN0 gpioout*/					\
	SPM8XX_DESC_PIN_FUNC_COM(0, "gpioout", 0x8, 0x3, 1, 0, 0, 0, 0),			\
	/* PIN0 exten */					\
	SPM8XX_DESC_PIN_FUNC_COM(0, "exten", 0x8, 0x3, 0x3, 1, 0xa, 0x7, 0x0),			\
	/* PIN0 pwrctrl */					\
	SPM8XX_DESC_PIN_FUNC_COM(0, "pwrctrl", 0x8, 0x3, 0x3, 1, 0xa, 0x7, 0x1),		\
	/* PIN0 sleep */					\
	SPM8XX_DESC_PIN_FUNC_COM(0, "sleep", 0x8, 0x3, 0x3, 1, 0xa, 0x7, 0x2),			\
	/* PIN0 nreset */					\
	SPM8XX_DESC_PIN_FUNC_COM(0, "nreset", 0x8, 0x3, 0x3, 1, 0xa, 0x7, 0x3),		\
	/* PIN0 adcin */					\
	SPM8XX_DESC_PIN_FUNC_COM(0, "adcin", 0x8, 0x3, 0x3, 1, 0xa, 0x7, 0x4),			\
	/* PIN1 gpioin */				\
	SPM8XX_DESC_PIN_FUNC_COM(1, "gpioin", 0x8, 0xc, 0, 0, 0, 0, 0),				\
	/* PIN1 gpioout*/					\
	SPM8XX_DESC_PIN_FUNC_COM(1, "gpioout", 0x8, 0xc, 1, 0, 0, 0, 0),			\
	/* PIN1 exten */					\
	SPM8XX_DESC_PIN_FUNC_COM(1, "exten", 0x8, 0xc, 0x3, 1, 0xa, 0x38, 0x0),		\
	/* PIN1 pwrctrl */					\
	SPM8XX_DESC_PIN_FUNC_COM(1, "pwrctrl", 0x8, 0xc, 0x3, 1, 0xa, 0x38, 0x1),		\
	/* PIN1 sleep */					\
	SPM8XX_DESC_PIN_FUNC_COM(1, "sleep", 0x8, 0xc, 0x3, 1, 0xa, 0x38, 0x2),		\
	/* PIN1 nreset */					\
	SPM8XX_DESC_PIN_FUNC_COM(1, "nreset", 0x8, 0xc, 0x3, 1, 0xa, 0x38, 0x3),		\
	/* PIN1 adcin */					\
	SPM8XX_DESC_PIN_FUNC_COM(1, "adcin", 0x8, 0xc, 0x3, 1, 0xa, 0x38, 0x4),		\
	/* PIN2 gpioin */				\
	SPM8XX_DESC_PIN_FUNC_COM(2, "gpioin", 0x8, 0x30, 0, 0, 0, 0, 0),			\
	/* PIN2 gpioout*/					\
	SPM8XX_DESC_PIN_FUNC_COM(2, "gpioout", 0x8, 0x30, 1, 0, 0, 0, 0),			\
	/* PIN2 exten */					\
	SPM8XX_DESC_PIN_FUNC_COM(2, "exten", 0x8, 0x30, 0x3, 1, 0xb, 0x7, 0x0),		\
	/* PIN2 pwrctrl */					\
	SPM8XX_DESC_PIN_FUNC_COM(2, "pwrctrl", 0x8, 0x30, 0x3, 1, 0xb, 0x7, 0x1),		\
	/* PIN2 sleep */					\
	SPM8XX_DESC_PIN_FUNC_COM(2, "sleep", 0x8, 0x30, 0x3, 1, 0xb, 0x7, 0x2),		\
	/* PIN2 nreset */					\
	SPM8XX_DESC_PIN_FUNC_COM(2, "nreset", 0x8, 0x30, 0x3, 1, 0xb, 0x7, 0x3),		\
	/* PIN2 adcin */					\
	SPM8XX_DESC_PIN_FUNC_COM(2, "adcin", 0x8, 0x30, 0x3, 1, 0xb, 0x7, 0x4),		\
	/* PIN3 gpioin */				\
	SPM8XX_DESC_PIN_FUNC_COM(3, "gpioin", 0x9, 0x3, 0, 0, 0, 0, 0),			\
	/* PIN3 gpioout*/					\
	SPM8XX_DESC_PIN_FUNC_COM(3, "gpioout", 0x9, 0x3, 1, 0, 0, 0, 0),			\
	/* PIN3 exten */					\
	SPM8XX_DESC_PIN_FUNC_COM(3, "exten", 0x9, 0x3, 0x3, 1, 0xb, 0x38, 0x0),		\
	/* PIN3 pwrctrl */					\
	SPM8XX_DESC_PIN_FUNC_COM(3, "pwrctrl", 0x9, 0x3, 0x3, 1, 0xb, 0x38, 0x1),		\
	/* PIN3 sleep */					\
	SPM8XX_DESC_PIN_FUNC_COM(3, "sleep", 0x9, 0x3, 0x3, 1, 0xb, 0x38, 0x2),		\
	/* PIN3 nreset */					\
	SPM8XX_DESC_PIN_FUNC_COM(3, "nreset", 0x9, 0x3, 0x3, 1, 0xb, 0x38, 0x3),		\
	/* PIN3 adcin */					\
	SPM8XX_DESC_PIN_FUNC_COM(3, "adcin", 0x9, 0x3, 0x3, 1, 0xb, 0x38, 0x4),		\
	/* PIN4 gpioin */				\
	SPM8XX_DESC_PIN_FUNC_COM(4, "gpioin", 0x9, 0xc, 0, 0, 0, 0, 0),			\
	/* PIN4 gpioout*/					\
	SPM8XX_DESC_PIN_FUNC_COM(4, "gpioout", 0x9, 0xc, 1, 0, 0, 0, 0),			\
	/* PIN4 exten */					\
	SPM8XX_DESC_PIN_FUNC_COM(4, "exten", 0x9, 0xc, 0x3, 1, 0xc, 0x7, 0x0),		\
	/* PIN4 pwrctrl */					\
	SPM8XX_DESC_PIN_FUNC_COM(4, "pwrctrl", 0x9, 0xc, 0x3, 1, 0xc, 0x7, 0x1),		\
	/* PIN4 sleep */					\
	SPM8XX_DESC_PIN_FUNC_COM(4, "sleep", 0x9, 0xc, 0x3, 1, 0xc, 0x7, 0x2),		\
	/* PIN4 nreset */					\
	SPM8XX_DESC_PIN_FUNC_COM(4, "nreset", 0x9, 0xc, 0x3, 1, 0xc, 0x7, 0x3),		\
	/* PIN4 adcin */					\
	SPM8XX_DESC_PIN_FUNC_COM(4, "adcin", 0x9, 0xc, 0x3, 1, 0xc, 0x7, 0x4),		\
	/* PIN5 gpioin */				\
	SPM8XX_DESC_PIN_FUNC_COM(5, "gpioin", 0x9, 0x30, 0, 0, 0, 0, 0),			\
	/* PIN5 gpioout*/					\
	SPM8XX_DESC_PIN_FUNC_COM(5, "gpioout", 0x9, 0x30, 1, 0, 0, 0, 0),			\
	/* PIN5 exten */					\
	SPM8XX_DESC_PIN_FUNC_COM(5, "exten", 0x9, 0x30, 0x3, 1, 0xc, 0x38, 0x0),		\
	/* PIN5 pwrctrl */					\
	SPM8XX_DESC_PIN_FUNC_COM(5, "pwrctrl", 0x9, 0x30, 0x3, 1, 0xc, 0x38, 0x1),		\
	/* PIN5 sleep */					\
	SPM8XX_DESC_PIN_FUNC_COM(5, "sleep", 0x9, 0x30, 0x3, 1, 0xc, 0x38, 0x2),		\
	/* PIN5 nreset */					\
	SPM8XX_DESC_PIN_FUNC_COM(5, "nreset", 0x9, 0x30, 0x3, 1, 0xc, 0x38, 0x3),		\
	/* PIN5 adcin */					\
	SPM8XX_DESC_PIN_FUNC_COM(5, "adcin", 0x9, 0x30, 0x3, 1, 0xc, 0x38, 0x4),		\
};

#define SPM8821_PIN_CINFIG_DESC				\
static const struct pin_config_desc spm8821_pinconfig_desc[] = \
{												\
	SPM8XX_DESC_PIN_CONFIG_COM(0, 0x0, 0x1, 0x1, 0x1, 0x2, 0x3, 0x4, 0xc0, 0x1, 0x5, 0x1, 0x6, 0x3),		\
	SPM8XX_DESC_PIN_CONFIG_COM(1, 0x0, 0x2, 0x1, 0x2, 0x2, 0xC, 0x4, 0xc0, 0x2, 0x5, 0x2, 0x6, 0xC),		\
	SPM8XX_DESC_PIN_CONFIG_COM(2, 0x0, 0x4, 0x1, 0x4, 0x2, 0x30, 0x4, 0xc0, 0x4, 0x5, 0x4, 0x6, 0x30),		\
	SPM8XX_DESC_PIN_CONFIG_COM(3, 0x0, 0x8, 0x1, 0x8, 0x3, 0x3, 0x4, 0xc0, 0x8, 0x5, 0x8, 0x7, 0x3),		\
	SPM8XX_DESC_PIN_CONFIG_COM(4, 0x0, 0x10, 0x1, 0x10, 0x3, 0xc, 0x4, 0xc0, 0x10, 0x5, 0x10, 0x7, 0xc),		\
	SPM8XX_DESC_PIN_CONFIG_COM(5, 0x0, 0x20, 0x1, 0x20, 0x3, 0x30, 0x4, 0xc0, 0x20, 0x5, 0x20, 0x7, 0x30),		\
};

#endif /* __SPM8821_H__ */
