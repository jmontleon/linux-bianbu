/******************************************************************************
 *
 * Copyright(c) 2021 Realtek Corporation.
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
 *****************************************************************************/
#define _PHL_TXPWR_C_
#include "phl_headers.h"

const char *rtw_phl_get_pw_lmt_regu_type_str(void *phl, enum band_type band)
{
	struct phl_info_t *phl_info = phl;

	return rtw_hal_get_pw_lmt_regu_type_str(phl_info->hal, band);
}

bool rtw_phl_get_pwr_lmt_en(void *phl, u8 band_idx)
{
	struct phl_info_t *phl_info = phl;

	return rtw_hal_get_pwr_lmt_en(phl_info->hal, band_idx);
}

static u16 phl_get_pwr_constraint(struct phl_info_t *phl_info, u8 band_idx)
{
	return rtw_hal_get_pwr_constraint(phl_info->hal, band_idx);
}

static enum rtw_phl_status phl_set_pwr_constraint(struct phl_info_t *phl_info, u8 band_idx, u16 mb)
{
	enum rtw_hal_status hstatus = RTW_HAL_STATUS_FAILURE;

	hstatus = rtw_hal_set_pwr_constraint(phl_info->hal, band_idx, mb);
	if (hstatus != RTW_HAL_STATUS_SUCCESS)
		PHL_ERR("%s rtw_hal_set_pwr_constraint: statuts = %u\n", __func__, hstatus);

	return hstatus == RTW_HAL_STATUS_SUCCESS ? RTW_PHL_STATUS_SUCCESS : RTW_PHL_STATUS_FAILURE;
}

enum rtw_phl_status rtw_phl_set_tx_power(void *phl, u8 band_idx)
{
	struct phl_info_t *phl_info = phl;
	enum rtw_hal_status hstatus = RTW_HAL_STATUS_FAILURE;

	hstatus = rtw_hal_set_tx_power(phl_info->hal, band_idx, PWR_BY_RATE | PWR_LIMIT | PWR_LIMIT_RU);
	if (hstatus != RTW_HAL_STATUS_SUCCESS)
		PHL_ERR("%s rtw_hal_set_tx_power: statuts = %u\n", __func__, hstatus);

	return hstatus == RTW_HAL_STATUS_SUCCESS ? RTW_PHL_STATUS_SUCCESS : RTW_PHL_STATUS_FAILURE;
}

enum rtw_phl_status
phl_cmd_txpwr_ctl_hdl(void *phl, u8 *param_buf)
{
	struct phl_info_t *phl_info = phl;
	struct txpwr_ctl_param *param = (struct txpwr_ctl_param *)param_buf;
	u8 band_idx = param->band_idx;
	bool write_txpwr = false;
	enum rtw_phl_status psts = RTW_PHL_STATUS_SUCCESS;

	if (param->constraint_mb >= 0) {
		u16 constraint_mb = (u16)param->constraint_mb;

		if (constraint_mb != phl_get_pwr_constraint(phl_info, band_idx)) {
			psts = phl_set_pwr_constraint(phl_info, band_idx, constraint_mb);
			if (psts != RTW_PHL_STATUS_SUCCESS)
				goto exit;
			PHL_INFO("%s constraint_mb:%u is set\n", __func__, constraint_mb);
			write_txpwr = true;
		}
	}

	if (param->force_write_txpwr || write_txpwr)
		psts = rtw_phl_set_tx_power(phl_info, band_idx);

exit:
	return psts;
}

static void phl_txpwr_ctl_done(void *drv_priv, u8 *cmd, u32 cmd_len, enum rtw_phl_status status)
{
	if (cmd) {
		_os_kmem_free(drv_priv, cmd, cmd_len);
		cmd = NULL;
	}
}

enum rtw_phl_status
rtw_phl_cmd_txpwr_ctl(void *phl, struct txpwr_ctl_param *args
	, enum phl_cmd_type cmd_type, u32 cmd_timeout)
{
#ifdef CONFIG_CMD_DISP
	struct phl_info_t *phl_info = phl;
	void *drv = phl_info->phl_com->drv_priv;
	enum rtw_phl_status psts = RTW_PHL_STATUS_FAILURE;
	struct txpwr_ctl_param *param = NULL;
	u32 param_len;

	param_len = sizeof(*param);
	param = _os_kmem_alloc(drv, param_len);
	if (param == NULL) {
		PHL_ERR("%s: alloc param failed!\n", __func__);
		goto _exit;
	}

	param->band_idx = args->band_idx;
	param->force_write_txpwr = args->force_write_txpwr;
	param->constraint_mb = args->constraint_mb;

	if (cmd_type == PHL_CMD_DIRECTLY) {
		psts = phl_cmd_txpwr_ctl_hdl(phl_info, (u8 *)param);
		phl_txpwr_ctl_done(drv, (u8 *)param, param_len, psts);
		goto _exit;
	}

	psts = phl_cmd_enqueue(phl_info,
			args->band_idx,
			MSG_EVT_TXPWR_SETUP,
			(u8 *)param, param_len,
			phl_txpwr_ctl_done,
			cmd_type, cmd_timeout);

	if (is_cmd_failure(psts)) {
		/* Send cmd success, but wait cmd fail*/
		psts = RTW_PHL_STATUS_FAILURE;
	} else if (psts != RTW_PHL_STATUS_SUCCESS) {
		/* Send cmd fail */
		_os_kmem_free(drv, param, param_len);
		psts = RTW_PHL_STATUS_FAILURE;
	}

_exit:
	return psts;
#else
	PHL_ERR("%s(), CONFIG_CMD_DISP need to be enabled for MSG_EVT_TXPWR_SETUP \n",__func__);
	return RTW_PHL_STATUS_FAILURE;
#endif
}

enum rtw_phl_status
rtw_phl_cmd_set_tx_power_constraint(void *phl, enum phl_band_idx band_idx, u16 mb
	, enum phl_cmd_type cmd_type, u32 cmd_timeout)
{
	struct txpwr_ctl_param args;

	txpwr_ctl_param_init(&args);
	args.band_idx = band_idx;
	args.constraint_mb = mb;

	return rtw_phl_cmd_txpwr_ctl(phl, &args
		, cmd_type, cmd_timeout);
}

enum rtw_phl_status
rtw_phl_cmd_set_tx_power(void *phl, enum phl_band_idx band_idx
	, enum phl_cmd_type cmd_type, u32 cmd_timeout)
{
	struct txpwr_ctl_param args;

	txpwr_ctl_param_init(&args);
	args.band_idx = band_idx;
	args.force_write_txpwr = true;

	return rtw_phl_cmd_txpwr_ctl(phl, &args
		, cmd_type, cmd_timeout);
}

enum rtw_phl_status rtw_phl_get_txinfo_pwr(void *phl, s16 *pwr_dbm)
{
	struct phl_info_t *phl_info = phl;
	enum rtw_hal_status hstatus = RTW_HAL_STATUS_FAILURE;
	s16 power_dbm = 0;

	hstatus = rtw_hal_get_txinfo_power(phl_info->hal, &power_dbm);
	*pwr_dbm = power_dbm;

	return hstatus == RTW_HAL_STATUS_SUCCESS ? RTW_PHL_STATUS_SUCCESS : RTW_PHL_STATUS_FAILURE;
}

#ifdef CONFIG_CMD_DISP
enum rtw_phl_status
rtw_phl_cmd_get_txinfo_pwr(void *phl, s16 *pwr_dbm,
				enum phl_band_idx band_idx,
				bool direct) /* if caller already in cmd/msg, use direct = true */
{
	struct phl_info_t *phl_info = (struct phl_info_t *)phl;
	enum rtw_phl_status psts = RTW_PHL_STATUS_FAILURE;

	if (direct) {
		psts = rtw_phl_get_txinfo_pwr(phl, pwr_dbm);
		goto exit;
	}

	psts = phl_cmd_enqueue(phl_info,
				band_idx,
				MSG_EVT_GET_TX_PWR_DBM,
				(u8*)pwr_dbm,
				sizeof(s16),
				NULL,
				PHL_CMD_WAIT,
				0);
	if (is_cmd_failure(psts)) {
		/* Send cmd success, but wait cmd fail */
		psts = RTW_PHL_STATUS_FAILURE;
	} else if (psts != RTW_PHL_STATUS_SUCCESS) {
		/* Send cmd fail */
		psts = RTW_PHL_STATUS_FAILURE;
	}

exit:
	return psts;
}
#else
enum rtw_phl_status
rtw_phl_cmd_get_txinfo_pwr(void *phl, s16 *pwr_dbm,
				enum phl_band_idx band_idx,
				bool direct)
{
	struct phl_info_t *phl_info = (struct phl_info_t *)phl;

	return rtw_phl_get_txinfo_pwr(phl, pwr_dbm);
}
#endif

