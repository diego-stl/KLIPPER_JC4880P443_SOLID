#include "st7701_sequence.h"
#include "board_p4_st7701_init.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "st7701_sequence";

esp_err_t st7701_sequence_send(esp_lcd_panel_io_handle_t io)
{
    ESP_RETURN_ON_FALSE(io != NULL, ESP_ERR_INVALID_ARG, TAG, "IO nulo");
    for (size_t i = 0; i < BOARD_P4_PANEL_INIT_CMDS_SIZE; ++i) {
        const board_p4_panel_init_cmd_t *cmd = &board_p4_panel_init_cmds[i];
        ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, cmd->cmd, cmd->data, cmd->len),
                            TAG, "Fallo en comando %zu (0x%02x)", i, cmd->cmd);
        if (cmd->delay_ms) {
            vTaskDelay(pdMS_TO_TICKS(cmd->delay_ms));
        }
    }
    ESP_LOGI(TAG, "Tabla JC4880P443C enviada: %zu comandos; imagen por validar",
             BOARD_P4_PANEL_INIT_CMDS_SIZE);
    return ESP_OK;
}
