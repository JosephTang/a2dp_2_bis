/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "esp_console.h"
#include "esp_err.h"
#include "esp_gmf_oal_sys.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"

#include "cmd_reg.h"

static int restart(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    printf("Restarting\n");
    esp_restart();
}

static int free_mem(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    printf("\nFree heap size: internal %u, psram %u\nmin  heap size: internal %u, psram %u\n",
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
           (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
           (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM));
    return 0;
}

static int tasks_info(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    return esp_gmf_oal_sys_get_real_time_stats(1000, false);
}

static bool parse_log_level(const char *level_str, esp_log_level_t *level)
{
    if (strcmp(level_str, "none") == 0 || strcmp(level_str, "0") == 0) {
        *level = ESP_LOG_NONE;
    } else if (strcmp(level_str, "error") == 0 || strcmp(level_str, "err") == 0 ||
               strcmp(level_str, "1") == 0) {
        *level = ESP_LOG_ERROR;
    } else if (strcmp(level_str, "warn") == 0 || strcmp(level_str, "warning") == 0 ||
               strcmp(level_str, "2") == 0) {
        *level = ESP_LOG_WARN;
    } else if (strcmp(level_str, "info") == 0 || strcmp(level_str, "3") == 0) {
        *level = ESP_LOG_INFO;
    } else if (strcmp(level_str, "debug") == 0 || strcmp(level_str, "4") == 0) {
        *level = ESP_LOG_DEBUG;
    } else if (strcmp(level_str, "verbose") == 0 || strcmp(level_str, "5") == 0) {
        *level = ESP_LOG_VERBOSE;
    } else {
        return false;
    }
    return true;
}

static int log_level(int argc, char **argv)
{
    if (argc != 3) {
        printf("Usage: log_level <tag|*> <none|error|warn|info|debug|verbose|0-5>\n");
        printf("Example: log_level A2DP_BIS_BRIDGE debug\n");
        printf("Example: log_level * warn\n");
        return 1;
    }

    esp_log_level_t level = ESP_LOG_NONE;
    if (!parse_log_level(argv[2], &level)) {
        printf("Invalid log level: %s\n", argv[2]);
        return 1;
    }

    esp_log_level_set(argv[1], level);
    printf("Set log level: tag=%s level=%s\n", argv[1], argv[2]);
    return 0;
}

static void cli_register_sys(void)
{
    static const esp_console_cmd_t cmds[] = {
        {
            .command = "restart",
            .help = "Software reset of the chip",
            .hint = NULL,
            .func = &restart,
        },
        {
            .command = "free",
            .help = "Get the current size of free heap memory",
            .hint = NULL,
            .func = &free_mem,
        },
        {
            .command = "tasks",
            .help = "Get information about running tasks",
            .hint = NULL,
            .func = &tasks_info,
        },
        {
            .command = "log_level",
            .help = "Set runtime log level for a tag or *",
            .hint = "<tag|*> <level>",
            .func = &log_level,
        },
    };

    for (int i = 0; i < sizeof(cmds) / sizeof(cmds[0]); i++) {
        ESP_ERROR_CHECK(esp_console_cmd_register(&cmds[i]));
    }
}

void cli_init(void)
{
    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_config.prompt = "A2DP-2-BIS >";
#if CONFIG_ESP_CONSOLE_UART
    esp_console_dev_uart_config_t uart_config = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_uart(&uart_config, &repl_config, &repl));
#elif CONFIG_ESP_CONSOLE_USB_CDC
    esp_console_dev_usb_cdc_config_t cdc_config = ESP_CONSOLE_DEV_CDC_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_usb_cdc(&cdc_config, &repl_config, &repl));
#elif CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
    esp_console_dev_usb_serial_jtag_config_t usbjtag_config = ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_usb_serial_jtag(&usbjtag_config, &repl_config, &repl));
#endif

    cli_register_sys();
    ESP_ERROR_CHECK(esp_console_start_repl(repl));
}
