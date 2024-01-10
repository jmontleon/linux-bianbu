/******************************************************************************
 *
 * Copyright(c) 2019 Realtek Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 ******************************************************************************/
#include "ftm.h"

#define FWCMD_H2C_FTM_INFO_ASAP_SH 0
#define FWCMD_H2C_FTM_INFO_ASAP_MSK 0xff
#define FWCMD_H2C_FTM_INFO_PKTID_IFTMR_SH 8
#define FWCMD_H2C_FTM_INFO_PKTID_IFTMR_MSK 0xff
#define FWCMD_H2C_FTM_INFO_PKTID_IFTM_SH 16
#define FWCMD_H2C_FTM_INFO_PKTID_IFTM_MSK 0xff
#define FWCMD_H2C_FTM_INFO_TSF_TIMER_OFFSET_SH 24
#define FWCMD_H2C_FTM_INFO_TSF_TIMER_OFFSET_MSK 0xff
#define FWCMD_H2C_FTM_INFO_PARTIAL_TSF_TIMER_SH 0
#define FWCMD_H2C_FTM_INFO_PARTIAL_TSF_TIMER_MSK 0xffff
#define FWCMD_H2C_FTM_INFO_RSP_CH_SH 16
#define FWCMD_H2C_FTM_INFO_RSP_CH_MSK 0xff
#define FWCMD_H2C_FTM_INFO_MODE_SH 24
#define FWCMD_H2C_FTM_INFO_MODE_MSK 0xff

u32 mac_update_ftm_info(struct mac_ax_adapter *adapter,
			struct fwcmd_ftm_info *info)
{
	u32 ret = 0;
	u8 *buf;
	#if MAC_AX_PHL_H2C
	struct rtw_h2c_pkt *h2cb;
	#else
	struct h2c_buf *h2cb;
	#endif
	struct fwcmd_ftm_info *tbl;

	/*h2c access*/
	h2cb = h2cb_alloc(adapter, H2CB_CLASS_CMD);
	if (!h2cb)
		return MACNPTR;

	buf = h2cb_put(h2cb, sizeof(struct fwcmd_ftm_info));
	if (!buf) {
		ret = MACNOBUF;
		goto fail;
	}

	tbl = (struct fwcmd_ftm_info *)buf;

	tbl->dword0 = info->dword0;
	tbl->dword1 = info->dword1;
	tbl->dword2 = info->dword2;
	tbl->dword3 = info->dword3;

	if (adapter->sm.fwdl == MAC_AX_FWDL_INIT_RDY) {
		ret = h2c_pkt_set_hdr(adapter, h2cb,
				      FWCMD_TYPE_H2C,
				      FWCMD_H2C_CAT_MAC,
				      FWCMD_H2C_CL_SEC_CAM,
				      FWCMD_H2C_FUNC_SECCAM_FTM,
				      0,
				      0);

		if (ret != MACSUCCESS)
			goto fail;

		// return MACSUCCESS if h2c aggregation is enabled and enqueued successfully.
		// H2C shall be sent by mac_h2c_agg_tx.
		ret = h2c_agg_enqueue(adapter, h2cb);
		if (ret == MACSUCCESS)
			return MACSUCCESS;

		ret = h2c_pkt_build_txd(adapter, h2cb);
		if (ret != MACSUCCESS)
			goto fail;

		#if MAC_AX_PHL_H2C
		ret = PLTFM_TX(h2cb);
		#else
		ret = PLTFM_TX(h2cb->data, h2cb->len);
		#endif
		if (ret != MACSUCCESS)
			goto fail;

		h2cb_free(adapter, h2cb);
		return MACSUCCESS;
fail:
		h2cb_free(adapter, h2cb);
	} else {
		return MACNOFW;
	}

	return ret;
}

u32 fill_ftm_para(struct mac_ax_adapter *adapter,
		  struct mac_ax_ftm_para *ftm_info,
		  struct fwcmd_ftm_info *ftm_fw_info)
{
	ftm_fw_info->dword0 =
	cpu_to_le32(SET_WORD(ftm_info->asap, FWCMD_H2C_FTM_INFO_ASAP) |
		    SET_WORD(ftm_info->pktid_iftmr, FWCMD_H2C_FTM_INFO_PKTID_IFTMR) |
		    SET_WORD(ftm_info->pktid_ftmr, FWCMD_H2C_FTM_INFO_PKTID_IFTM) |
		    SET_WORD(ftm_info->tsf_timer_offset, FWCMD_H2C_FTM_INFO_TSF_TIMER_OFFSET));

	ftm_fw_info->dword1 =
	cpu_to_le32(SET_WORD(ftm_info->partial_tsf_timer, FWCMD_H2C_FTM_INFO_PARTIAL_TSF_TIMER) |
		    SET_WORD(ftm_info->rsp_ch, FWCMD_H2C_FTM_INFO_RSP_CH) |
		    SET_WORD(ftm_info->mode, FWCMD_H2C_FTM_INFO_MODE));

	ftm_fw_info->dword2 =
	cpu_to_le32(SET_WORD(ftm_info->ch_parm_trg.pri_ch, FWCMD_H2C_CH_SWITCH_PRI_CH) |
		    SET_WORD(ftm_info->ch_parm_trg.central_ch, FWCMD_H2C_CH_SWITCH_CENTRAL_CH) |
		    SET_WORD(ftm_info->ch_parm_trg.bw, FWCMD_H2C_CH_SWITCH_BW) |
		    SET_WORD(ftm_info->ch_parm_trg.ch_band, FWCMD_H2C_CH_SWITCH_CH_BAND) |
		    (ftm_info->ch_parm_trg.band ? FWCMD_H2C_CH_SWITCH_BAND : 0) |
		    (ftm_info->ch_parm_trg.reload_rf ? FWCMD_H2C_CH_SWITCH_RELOAD_RF : 0));

	ftm_fw_info->dword3 =
	cpu_to_le32(SET_WORD(ftm_info->ch_parm_ori.pri_ch, FWCMD_H2C_CH_SWITCH_PRI_CH) |
		    SET_WORD(ftm_info->ch_parm_ori.central_ch, FWCMD_H2C_CH_SWITCH_CENTRAL_CH) |
		    SET_WORD(ftm_info->ch_parm_ori.bw, FWCMD_H2C_CH_SWITCH_BW) |
		    SET_WORD(ftm_info->ch_parm_ori.ch_band, FWCMD_H2C_CH_SWITCH_CH_BAND) |
		    (ftm_info->ch_parm_ori.band ? FWCMD_H2C_CH_SWITCH_BAND : 0) |
		    (ftm_info->ch_parm_ori.reload_rf ? FWCMD_H2C_CH_SWITCH_RELOAD_RF : 0));

	return MACSUCCESS;
}

u32 mac_ista_ftm_proc(struct mac_ax_adapter *adapter,
		      struct mac_ax_ftm_para *ftmr)
{
	u32 ftm_fw_info[4] = {0}, ret = 0;

	ret = fill_ftm_para(adapter, ftmr,
			    (struct fwcmd_ftm_info *)(&ftm_fw_info));
	if (ret != MACSUCCESS)
		return ret;

	ret = (u8)mac_update_ftm_info(adapter,
				      (struct fwcmd_ftm_info *)(&ftm_fw_info));
	if (ret != MACSUCCESS)
		return ret;

	return MACSUCCESS;
}

u32 mac_ista_ftm_enable(struct mac_ax_adapter *adapter,
			u8 macid, bool enable)
{
	u32 val32 = 0, ret = 0;
	struct rtw_hal_mac_ax_cctl_info info, msk = {0};
	struct mac_role_tbl *role;
	struct mac_ax_intf_ops *ops = adapter_to_intf_ops(adapter);

	role = mac_role_srch(adapter, macid);
	if (!role) {
		PLTFM_MSG_ERR("%s: The MACID%d does not exist\n",
			      __func__, macid);
		return MACNOITEM;
	}

	if (enable) {
		ret = check_mac_en(adapter, 0, MAC_AX_CMAC_SEL);
		if (ret != MACSUCCESS)
			return ret;

		// init BB FTM CLK
		val32 = MAC_REG_R32(0x10014);
		val32 |= (BIT5 | BIT6);
		MAC_REG_W32(0x10014, val32);

		val32 = MAC_REG_R32(0x109C4);
		val32 |= (BIT31 | BIT28 | BIT26);
		MAC_REG_W32(0x109C4, val32);

		// init TRX FTM
		val32 = MAC_REG_R32(R_AX_WMAC_FTM_CTL);
		val32 |= (B_AX_FTM_EN | B_AX_RXFTM_EN);
		MAC_REG_W32(R_AX_WMAC_FTM_CTL, val32);

		// init TSF
		val32 = MAC_REG_R32(R_AX_PORT_CFG_P0);
		val32 |= (B_AX_PORT_FUNC_EN_P0);
		MAC_REG_W32(R_AX_PORT_CFG_P0, val32);

		// Trun on FTM report
		msk.acq_rpt_en = 1;
		info.acq_rpt_en = 1;
		msk.mgq_rpt_en = 1;
		info.mgq_rpt_en = 1;
		msk.ulq_rpt_en = 1;
		info.ulq_rpt_en = 1;
		ret = mac_upd_cctl_info(adapter, &info, &msk, macid, 1);
	} else {
		// Turn off BB FTM CLK
		val32 = MAC_REG_R32(0x10014);
		val32 &= ~(BIT5 | BIT6);
		MAC_REG_W32(0x10014, val32);

		val32 = MAC_REG_R32(0x109C4);
		val32 &= ~(BIT31 | BIT28 | BIT26);
		MAC_REG_W32(0x109C4, val32);

		// Turn off TRX FTM
		val32 = MAC_REG_R32(R_AX_WMAC_FTM_CTL);
		val32 &= ~(B_AX_FTM_EN | B_AX_RXFTM_EN);
		MAC_REG_W32(R_AX_WMAC_FTM_CTL, val32);

		// Trun off FTM report
		msk.acq_rpt_en = 1;
		info.acq_rpt_en = 0;
		msk.mgq_rpt_en = 1;
		info.mgq_rpt_en = 0;
		msk.ulq_rpt_en = 1;
		info.ulq_rpt_en = 0;
		ret = mac_upd_cctl_info(adapter, &info, &msk, macid, 1);
	}

	return MACSUCCESS;
}
