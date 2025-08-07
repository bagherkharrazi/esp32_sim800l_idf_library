/*
 * sim800l.c
 *
 *  Created on: Aug 7, 2025
 *      Author: BagherKharrazi
 */

#include "sim800l.h"
#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
static const char *TAG = "SIM800L";
static sim800l_config_t s_config;
static sim800l_event_handler_t s_event_handler = NULL;
static esp_err_t sim800l_send_cmd(const char *cmd, const char *expect, int timeout_ms) {
    uart_flush(s_config.uart_port);
    uart_write_bytes(s_config.uart_port, cmd, strlen(cmd));
    uart_write_bytes(s_config.uart_port, "\r\n", 2);
    char resp[SIM800L_RESPONSE_BUFFER_SIZE] = {0};
    int len = uart_read_bytes(s_config.uart_port, (uint8_t *)resp, sizeof(resp) - 1, pdMS_TO_TICKS(timeout_ms));
    if (len > 0) {
        resp[len] = 0;
        ESP_LOGD(TAG, "RESP: %s", resp);
        if (expect && strstr(resp, expect)) return ESP_OK;
    }
    return ESP_FAIL;
}

esp_err_t sim800l_init(sim800l_config_t *config) {
    s_config = *config;
    uart_config_t uart_conf = {
        .baud_rate = config->baud_rate,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
    };
    uart_param_config(config->uart_port, &uart_conf);
    uart_set_pin(config->uart_port, config->tx_pin, config->rx_pin, config->rts_pin, config->cts_pin);
    uart_driver_install(config->uart_port, config->uart_buffer_size, 0, 0, NULL, 0);
    vTaskDelay(pdMS_TO_TICKS(1000)); // wait for SIM800L
    if (sim800l_send_cmd("AT", "OK", 1000) != ESP_OK) return ESP_FAIL;
    if (sim800l_send_cmd("ATE0", "OK", 1000) != ESP_OK) return ESP_FAIL; // disable echo
    if (sim800l_send_cmd("AT+CMGF=1", "OK", 1000) != ESP_OK) return ESP_FAIL; // text mode for SMS

    return ESP_OK;
}

esp_err_t sim800l_wait_for_network(int timeout_ms) {
    int elapsed = 0;
    while (elapsed < timeout_ms) {
        if (sim800l_send_cmd("AT+CREG?", "+CREG: 0,1", 1000) == ESP_OK ||
            sim800l_send_cmd("AT+CREG?", "+CREG: 0,5", 1000) == ESP_OK) {
            return ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
        elapsed += 1000;
    }
    return ESP_ERR_TIMEOUT;
}

esp_err_t sim800l_get_signal_quality(int *rssi) {
    uart_flush(s_config.uart_port);
    uart_write_bytes(s_config.uart_port, "AT+CSQ\r\n", 8);
    char resp[64] = {0};
    int len = uart_read_bytes(s_config.uart_port, (uint8_t *)resp, sizeof(resp) - 1, pdMS_TO_TICKS(1000));
    if (len > 0 && strstr(resp, "+CSQ:")) {
        int r;
        if (sscanf(resp, "+CSQ: %d", &r) == 1) {
            *rssi = r;
            return ESP_OK;
        }
    }
    return ESP_FAIL;
}

esp_err_t sim800l_send_sms(const char *number, const char *message) {
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "AT+CMGS=\"%s\"", number);
    if (sim800l_send_cmd(cmd, ">", 2000) != ESP_OK) return ESP_FAIL;
    uart_write_bytes(s_config.uart_port, message, strlen(message));
    uart_write_bytes(s_config.uart_port, "\x1A", 1); // Ctrl+Z
    vTaskDelay(pdMS_TO_TICKS(5000)); // wait for send
    char resp[SIM800L_RESPONSE_BUFFER_SIZE] = {0};
    int len = uart_read_bytes(s_config.uart_port, (uint8_t *)resp, sizeof(resp) - 1, pdMS_TO_TICKS(3000));
    if (len > 0 && strstr(resp, "OK")) return ESP_OK;

    return ESP_FAIL;
}

esp_err_t sim800l_read_sms(int index, char *out_message, size_t max_len) {
    char cmd[32];
    snprintf(cmd, sizeof(cmd), "AT+CMGR=%d", index);
    uart_flush(s_config.uart_port);
    uart_write_bytes(s_config.uart_port, cmd, strlen(cmd));
    uart_write_bytes(s_config.uart_port, "\r\n", 2);

    int len = uart_read_bytes(s_config.uart_port, (uint8_t *)out_message, max_len - 1, pdMS_TO_TICKS(2000));
    if (len > 0) {
        out_message[len] = '\0';
        if (strstr(out_message, "+CMGR:")) return ESP_OK;
    }
    return ESP_FAIL;
}

esp_err_t sim800l_delete_sms(int index) {
    char cmd[32];
    snprintf(cmd, sizeof(cmd), "AT+CMGD=%d", index);
    return sim800l_send_cmd(cmd, "OK", 1000);
}

esp_err_t sim800l_call_number(const char *number) {
    char cmd[32];
    snprintf(cmd, sizeof(cmd), "ATD%s;", number);
    return sim800l_send_cmd(cmd, "OK", 1000);
}

esp_err_t sim800l_answer_call(void) {
    return sim800l_send_cmd("ATA", "OK", 1000);
}

esp_err_t sim800l_hangup_call(void) {
    return sim800l_send_cmd("ATH", "OK", 1000);
}
void sim800l_register_event_handler(sim800l_event_handler_t handler) {
    s_event_handler = handler;
}
static void sim800l_event_task(void *arg) {
    char buf[SIM800L_RESPONSE_BUFFER_SIZE];

    while (1) {
        int len = uart_read_bytes(s_config.uart_port, (uint8_t *)buf, sizeof(buf)-1, pdMS_TO_TICKS(100));
        if (len > 0) {
            buf[len] = '\0';

            // پیامک جدید؟
            if (strstr(buf, "+CMT:")) {
                char *p = strstr(buf, "+CMT: ");
                if (p) {
                    char sender[24] = {0};
                    char message[160] = {0};

                    sscanf(p, "+CMT: \"%23[^\"]\"", sender);

                    char *msg_start = strchr(p, '\n');
                    if (msg_start) {
                        strncpy(message, msg_start + 1, sizeof(message)-1);
                    }

                    if (s_event_handler) {
                        sim800l_event_t ev = {
                            .type = SIM800L_EVENT_SMS_RECEIVED
                        };
                        strncpy(ev.sms.sender, sender, sizeof(ev.sms.sender)-1);
                        strncpy(ev.sms.message, message, sizeof(ev.sms.message)-1);
                        s_event_handler(&ev);
                    }
                }
            }
            if (strstr(buf, "RING")) {
                char *clcc = strstr(buf, "+CLIP:");
                if (clcc) {
                    char number[24] = {0};
                    sscanf(clcc, "+CLIP: \"%23[^\"]\"", number);
                    if (s_event_handler) {
                        sim800l_event_t ev = {
                            .type = SIM800L_EVENT_CALL_INCOMING
                        };
                        strncpy(ev.call.number, number, sizeof(ev.call.number) - 1);
                        s_event_handler(&ev);
                    }
                } else {
                    vTaskDelay(pdMS_TO_TICKS(500));
                    int l2 = uart_read_bytes(s_config.uart_port, (uint8_t *)buf, sizeof(buf) - 1, pdMS_TO_TICKS(1000));
                    if (l2 > 0) {
                        buf[l2] = '\0';
                        clcc = strstr(buf, "+CLIP:");
                        if (clcc) {
                            char number[24] = {0};
                            sscanf(clcc, "+CLIP: \"%23[^\"]\"", number);
                            if (s_event_handler) {
                                sim800l_event_t ev = {
                                    .type = SIM800L_EVENT_CALL_INCOMING
                                };
                                strncpy(ev.call.number, number, sizeof(ev.call.number) - 1);
                                s_event_handler(&ev);
                            }
                        }
                    }
                }
            }
        }
    }
}
static void utf8_to_ucs2_hex(const char *utf8, char *ucs2_hex, size_t max_len) {
    uint16_t ucs2;
    size_t i = 0;
    while (*utf8 && i + 4 < max_len) {
        uint8_t c = *utf8++;
        if (c < 0x80) {
            ucs2 = c;
        } else if ((c & 0xE0) == 0xC0) {
            ucs2 = ((c & 0x1F) << 6) | (*utf8++ & 0x3F);
        } else if ((c & 0xF0) == 0xE0) {
            ucs2 = ((c & 0x0F) << 12) | ((utf8[0] & 0x3F) << 6) | (utf8[1] & 0x3F);
            utf8 += 2;
        } else {
            continue;
        }
        snprintf(ucs2_hex + i, 5, "%04X", ucs2);
        i += 4;
    }
    ucs2_hex[i] = '\0';
}
esp_err_t sim800l_prepare_for_ucs2_sms(void) {
    esp_err_t ret1 = sim800l_send_cmd("AT+CSCS=\"UCS2\"", "OK", 1000);      // Set character set to UCS2
    esp_err_t ret2 = sim800l_send_cmd("AT+CSMP=17,167,0,8", "OK", 1000);    // Set SMS text mode params (DCS=8 => UCS2)
    if (ret1 == ESP_OK && ret2 == ESP_OK) return ESP_OK;
    return ESP_FAIL;
}
esp_err_t sim800l_restore_default_sms_mode(void) {
    esp_err_t ret1 = sim800l_send_cmd("AT+CSCS=\"GSM\"", "OK", 1000);     // Back to GSM charset
    esp_err_t ret2 = sim800l_send_cmd("AT+CSMP=17,167,0,0", "OK", 1000);  // Back to 7-bit GSM mode
    if (ret1 == ESP_OK && ret2 == ESP_OK) return ESP_OK;
    return ESP_FAIL;
}
esp_err_t sim800l_send_sms_utf8(const char *utf8_number, const char *utf8_message) {
    esp_err_t ret = ESP_FAIL;
    sim800l_send_cmd("AT+CSCS?", "+CSCS:", 1000);
    if (sim800l_send_cmd("AT+CSCS=\"UCS2\"", "OK", 1000) != ESP_OK) return ESP_FAIL;
    char number_ucs2[64] = {0};
    char message_ucs2[512] = {0};
    utf8_to_ucs2_hex(utf8_number, number_ucs2, sizeof(number_ucs2));
    utf8_to_ucs2_hex(utf8_message, message_ucs2, sizeof(message_ucs2));
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "AT+CMGS=\"%s\"", number_ucs2);
    if (sim800l_send_cmd(cmd, ">", 2000) != ESP_OK) goto cleanup;
    uart_write_bytes(s_config.uart_port, message_ucs2, strlen(message_ucs2));
    uart_write_bytes(s_config.uart_port, "\x1A", 1); // Ctrl+Z
    vTaskDelay(pdMS_TO_TICKS(5000));

    char resp[SIM800L_RESPONSE_BUFFER_SIZE] = {0};
    int len = uart_read_bytes(s_config.uart_port, (uint8_t *)resp, sizeof(resp)-1, pdMS_TO_TICKS(3000));
    if (len > 0 && strstr(resp, "OK")) {
        ret = ESP_OK;
    }

cleanup:
    sim800l_send_cmd("AT+CSCS=\"GSM\"", "OK", 1000);
    return ret;
}

void sim800l_start_event_loop(void) {
    sim800l_send_cmd("AT+CNMI=2,2,0,0,0", "OK", 1000); 
    sim800l_send_cmd("AT+CLIP=1", "OK", 1000);        

    xTaskCreate(sim800l_event_task, "sim800l_evt", 4096, NULL, 5, NULL);
}



