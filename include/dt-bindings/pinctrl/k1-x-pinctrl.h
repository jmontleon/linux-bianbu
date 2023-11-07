/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef __DT_BINDINGS_K1X_PINCTRL_H
#define __DT_BINDINGS_K1X_PINCTRL_H

/* pin offset */
#define PIN_OFFSET(x)	((x) * 4)

#define GPIO_00  PIN_OFFSET(1)
#define GPIO_01  PIN_OFFSET(2)
#define GPIO_02  PIN_OFFSET(3)
#define GPIO_03  PIN_OFFSET(4)
#define GPIO_04  PIN_OFFSET(5)
#define GPIO_05  PIN_OFFSET(6)
#define GPIO_06  PIN_OFFSET(7)
#define GPIO_07  PIN_OFFSET(8)
#define GPIO_08  PIN_OFFSET(9)
#define GPIO_09  PIN_OFFSET(10)
#define GPIO_10  PIN_OFFSET(11)
#define GPIO_11  PIN_OFFSET(12)
#define GPIO_12  PIN_OFFSET(13)
#define GPIO_13  PIN_OFFSET(14)
#define GPIO_14  PIN_OFFSET(15)
#define GPIO_15  PIN_OFFSET(16)
#define GPIO_16  PIN_OFFSET(17)
#define GPIO_17  PIN_OFFSET(18)
#define GPIO_18  PIN_OFFSET(19)
#define GPIO_19  PIN_OFFSET(20)
#define GPIO_20  PIN_OFFSET(21)
#define GPIO_21  PIN_OFFSET(22)
#define GPIO_22  PIN_OFFSET(23)
#define GPIO_23  PIN_OFFSET(24)
#define GPIO_24  PIN_OFFSET(25)
#define GPIO_25  PIN_OFFSET(26)
#define GPIO_26  PIN_OFFSET(27)
#define GPIO_27  PIN_OFFSET(28)
#define GPIO_28  PIN_OFFSET(29)
#define GPIO_29  PIN_OFFSET(30)
#define GPIO_30  PIN_OFFSET(31)
#define GPIO_31  PIN_OFFSET(32)

#define GPIO_32  PIN_OFFSET(33)
#define GPIO_33  PIN_OFFSET(34)
#define GPIO_34  PIN_OFFSET(35)
#define GPIO_35  PIN_OFFSET(36)
#define GPIO_36  PIN_OFFSET(37)
#define GPIO_37  PIN_OFFSET(38)
#define GPIO_38  PIN_OFFSET(39)
#define GPIO_39  PIN_OFFSET(40)
#define GPIO_40  PIN_OFFSET(41)
#define GPIO_41  PIN_OFFSET(42)
#define GPIO_42  PIN_OFFSET(43)
#define GPIO_43  PIN_OFFSET(44)
#define GPIO_44  PIN_OFFSET(45)
#define GPIO_45  PIN_OFFSET(46)
#define GPIO_46  PIN_OFFSET(47)
#define GPIO_47  PIN_OFFSET(48)
#define GPIO_48  PIN_OFFSET(49)
#define GPIO_49  PIN_OFFSET(50)
#define GPIO_50  PIN_OFFSET(51)
#define GPIO_51  PIN_OFFSET(52)
#define GPIO_52  PIN_OFFSET(53)
#define GPIO_53  PIN_OFFSET(54)
#define GPIO_54  PIN_OFFSET(55)
#define GPIO_55  PIN_OFFSET(56)
#define GPIO_56  PIN_OFFSET(57)
#define GPIO_57  PIN_OFFSET(58)
#define GPIO_58  PIN_OFFSET(59)
#define GPIO_59  PIN_OFFSET(60)
#define GPIO_60  PIN_OFFSET(61)
#define GPIO_61  PIN_OFFSET(62)
#define GPIO_62  PIN_OFFSET(63)
#define GPIO_63  PIN_OFFSET(64)

#define GPIO_64  PIN_OFFSET(65)
#define GPIO_65  PIN_OFFSET(66)
#define GPIO_66  PIN_OFFSET(67)
#define GPIO_67  PIN_OFFSET(68)
#define GPIO_68  PIN_OFFSET(69)
#define GPIO_69  PIN_OFFSET(70)
#define PRI_TDI  PIN_OFFSET(71)
#define PRI_TMS  PIN_OFFSET(72)
#define PRI_TCK  PIN_OFFSET(73)
#define PRI_TDO  PIN_OFFSET(74)
#define GPIO_74  PIN_OFFSET(75)
#define GPIO_75  PIN_OFFSET(76)
#define GPIO_76  PIN_OFFSET(77)
#define GPIO_77  PIN_OFFSET(78)
#define GPIO_78  PIN_OFFSET(79)
#define GPIO_79  PIN_OFFSET(80)
#define GPIO_80  PIN_OFFSET(81)
#define GPIO_81  PIN_OFFSET(82)
#define GPIO_82  PIN_OFFSET(83)
#define GPIO_83  PIN_OFFSET(84)
#define GPIO_84  PIN_OFFSET(85)
#define GPIO_85  PIN_OFFSET(86)

#define QSPI_DAT0   PIN_OFFSET(90)
#define QSPI_DAT1   PIN_OFFSET(91)
#define QSPI_DAT2   PIN_OFFSET(92)
#define QSPI_DAT3   PIN_OFFSET(93)
#define QSPI_CSI    PIN_OFFSET(94)
#define QSPI_CLK    PIN_OFFSET(95)

#define MMC1_DAT3   PIN_OFFSET(110)
#define MMC1_DAT2   PIN_OFFSET(111)
#define MMC1_DAT1   PIN_OFFSET(112)
#define MMC1_DAT0   PIN_OFFSET(113)
#define MMC1_CMD    PIN_OFFSET(114)
#define MMC1_CLK    PIN_OFFSET(115)
#define GPIO_110    PIN_OFFSET(116)
#define PWR_SCL     PIN_OFFSET(117)
#define PWR_SDA     PIN_OFFSET(118)
#define VCXO_EN     PIN_OFFSET(119)
#define DVL0        PIN_OFFSET(120)
#define DVL1        PIN_OFFSET(121)
#define PMIC_INT_N  PIN_OFFSET(122)
#define GPIO_86     PIN_OFFSET(123)
#define GPIO_87     PIN_OFFSET(124)
#define GPIO_88     PIN_OFFSET(125)
#define GPIO_89     PIN_OFFSET(126)
#define GPIO_90     PIN_OFFSET(127)
#define GPIO_91     PIN_OFFSET(128)
#define GPIO_92     PIN_OFFSET(129)

#define GPIO_111    PIN_OFFSET(131)
#define GPIO_112    PIN_OFFSET(132)
#define GPIO_113    PIN_OFFSET(133)
#define GPIO_114    PIN_OFFSET(134)
#define GPIO_115    PIN_OFFSET(135)
#define GPIO_116    PIN_OFFSET(136)
#define GPIO_117    PIN_OFFSET(137)
#define GPIO_118    PIN_OFFSET(138)
#define GPIO_119    PIN_OFFSET(139)
#define GPIO_120    PIN_OFFSET(140)
#define GPIO_121    PIN_OFFSET(141)
#define GPIO_122    PIN_OFFSET(142)
#define GPIO_123    PIN_OFFSET(143)
#define GPIO_124    PIN_OFFSET(144)
#define GPIO_125    PIN_OFFSET(145)
#define GPIO_126    PIN_OFFSET(146)
#define GPIO_127    PIN_OFFSET(147)

/* pin mux */
#define MUX_MODE0       0
#define MUX_MODE1       1
#define MUX_MODE2       2
#define MUX_MODE3       3
#define MUX_MODE4       4
#define MUX_MODE5       5
#define MUX_MODE6       6
#define MUX_MODE7       7

/* strong pull resistor */
#define SPU_EN          (1 << 3)

/* edge detect */
#define EDGE_NONE       (1 << 6)
#define EDGE_RISE       (1 << 4)
#define EDGE_FALL       (1 << 5)
#define EDGE_BOTH       (3 << 4)

/* slew rate output control */
#define SLE_EN          (1 << 7)

/* schmitter trigger input threshhold */
#define ST00            (0 << 8)
#define ST01            (1 << 8)
#define ST02            (2 << 8)
#define ST03            (3 << 8)

/* driver strength*/
#define PAD_1V8_DS0     (0 << 11)
#define PAD_1V8_DS1     (1 << 11)
#define PAD_1V8_DS2     (2 << 11)
#define PAD_1V8_DS3     (3 << 11)

/*
 * notice: !!!
 * ds2 ---> bit10, ds1 ----> bit12, ds0 ----> bit11
*/
#define PAD_3V_DS0      (0 << 10)     /* bit[12:10] 000 */
#define PAD_3V_DS1      (2 << 10)     /* bit[12:10] 010 */
#define PAD_3V_DS2      (4 << 10)     /* bit[12:10] 100 */
#define PAD_3V_DS3      (6 << 10)     /* bit[12:10] 110 */
#define PAD_3V_DS4      (1 << 10)     /* bit[12:10] 001 */
#define PAD_3V_DS5      (3 << 10)     /* bit[12:10] 011 */
#define PAD_3V_DS6      (5 << 10)     /* bit[12:10] 101 */
#define PAD_3V_DS7      (7 << 10)     /* bit[12:10] 111 */

/* pull up/down */
#define PULL_DIS        (0 << 13)     /* bit[15:13] 000 */
#define PULL_UP         (6 << 13)     /* bit[15:13] 110 */
#define PULL_DOWN       (5 << 13)     /* bit[15:13] 101 */

#define K1X_PADCONF(offset, conf, mux)	(offset) (conf) (mux)

#endif /* __DT_BINDINGS_K1PRO_PINCTRL_H */
