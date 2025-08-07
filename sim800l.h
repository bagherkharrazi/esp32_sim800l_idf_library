/*
 * sim800l.h
 *
 *  Created on: Aug 7, 2025
 *      Author: BagherKharrazi
 */

#ifndef INC_SIM800L_H_
#define INC_SIM800L_H_

#pragma once

#include "driver/uart.h"
#include "esp_err.h"
#include "esp_log.h"

#define SIM800L_DEFAULT_TIMEOUT_MS 5000
#define SIM800L_RESPONSE_BUFFER_SIZE 512

typedef struct {
    uart_port_t uart_port;
    int tx_pin;
    int rx_pin;
    int baud_rate;
    int rts_pin;
    int cts_pin;
    int uart_buffer_size;
} sim800l_config_t;

typedef enum {
    SIM800L_EVENT_SMS_RECEIVED,
    SIM800L_EVENT_CALL_INCOMING
} sim800l_event_type_t;

typedef struct {
    sim800l_event_type_t type;
    union {
        struct {
            char sender[24];
            char message[160];
        } sms;
        struct {
            char number[24];
        } call;
    };
} sim800l_event_t;
typedef void (*sim800l_event_handler_t)(sim800l_event_t *event);
esp_err_t sim800l_init(sim800l_config_t *config);
esp_err_t sim800l_wait_for_network(int timeout_ms);
esp_err_t sim800l_get_signal_quality(int *rssi);
esp_err_t sim800l_send_sms(const char *number, const char *message);
esp_err_t sim800l_read_sms(int index, char *out_message, size_t max_len);
esp_err_t sim800l_delete_sms(int index);
esp_err_t sim800l_call_number(const char *number);
esp_err_t sim800l_answer_call(void);
esp_err_t sim800l_hangup_call(void);
void sim800l_register_event_handler(sim800l_event_handler_t handler);
esp_err_t sim800l_prepare_for_ucs2_sms(void);
esp_err_t sim800l_restore_default_sms_mode(void);
esp_err_t sim800l_send_sms_utf8(const char *utf8_number, const char *utf8_message);
void sim800l_start_event_loop(void);





#endif /* INC_SIM800L_H_ */
