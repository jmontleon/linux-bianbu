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
#define _HAL_TXPWR_C_
#include "hal_headers.h"

const char *rtw_hal_get_pw_lmt_regu_type_str(void *hal, enum band_type band)
{
	struct hal_info_t *hal_info = (struct hal_info_t *)hal;

	return rtw_hal_rf_get_pw_lmt_regu_type_str(hal_info, band);
}

bool rtw_hal_get_pwr_lmt_en(void *hal, u8 band_idx)
{
	struct hal_info_t *hal_info = (struct hal_info_t *)hal;

	return rtw_hal_mac_get_pwr_lmt_en_val(hal_info->hal_com, band_idx);
}

u16 rtw_hal_get_pwr_constraint(void *hal, u8 band_idx)
{
	struct hal_info_t *hal_info = hal;
	struct rtw_tpu_info *tpu;

	if (band_idx >= MAX_BAND_NUM) {
		_os_warn_on(1);
		return 0;
	}

	tpu = &hal_info->hal_com->band[band_idx].rtw_tpu_i;
	return tpu->pwr_constraint_mb;
}

enum rtw_hal_status rtw_hal_set_pwr_constraint(void *hal, u8 band_idx, u16 mb)
{
	struct hal_info_t *hal_info = hal;
	struct rtw_tpu_info *tpu;

	if (band_idx >= MAX_BAND_NUM)
		return RTW_HAL_STATUS_FAILURE;

	tpu = &hal_info->hal_com->band[band_idx].rtw_tpu_i;

	if (tpu->pwr_constraint_mb != mb) {
		enum phl_phy_idx phy_idx = rtw_hal_hw_band_to_phy_idx(band_idx);

		/* software configuration only, no need to check for hwband ready */
		if (rtw_hal_rf_set_power_constraint(hal_info, phy_idx, mb) ==  RTW_HAL_STATUS_SUCCESS) {
			tpu->pwr_constraint_mb = mb;
			return RTW_HAL_STATUS_SUCCESS;
		}
		return RTW_HAL_STATUS_FAILURE;
	}

	return RTW_HAL_STATUS_SUCCESS;
}

enum rtw_hal_status rtw_hal_set_tx_power(void *hal, u8 band_idx,
					enum phl_pwr_table pwr_table)
{
	struct hal_info_t *hal_info = (struct hal_info_t *)hal;

	if (hal_info->hal_com->dbcc_en || band_idx == HW_BAND_0) {
		enum phl_phy_idx phy_idx = rtw_hal_hw_band_to_phy_idx(band_idx);

		return rtw_hal_rf_set_power(hal_info, phy_idx, pwr_table);
	}

	return RTW_HAL_STATUS_SUCCESS;
}

enum rtw_hal_status rtw_hal_get_txinfo_power(void *hal,
					s16 *txinfo_power_dbm)
{
	struct hal_info_t *hal_info = (struct hal_info_t *)hal;
	enum rtw_hal_status hal_status = RTW_HAL_STATUS_SUCCESS;
	s16 power_dbm = 0;

	hal_status = rtw_hal_bb_get_txinfo_power(hal_info, &power_dbm);
	*txinfo_power_dbm = power_dbm;

	return hal_status;
}

