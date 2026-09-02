/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "esp_gmf_err.h"
#include "esp_gmf_pool.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  Register the minimal GMF elements/IO used by the A2DP→BIS bridge
 *
 * @param[in]  pool  GMF pool handle
 *
 * @return
 *       - ESP_GMF_ERR_OK  On success
 *       - Others          On failure
 */
esp_gmf_err_t pool_reg(esp_gmf_pool_handle_t pool);

#ifdef __cplusplus
}
#endif
