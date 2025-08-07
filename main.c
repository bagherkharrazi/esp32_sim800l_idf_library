#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "string.h"
#include "esp_rom_gpio.h"
#include "sim800l.h"

static void sim_event_handler(sim800l_event_t *event) {
    switch (event->type) {
        case SIM800L_EVENT_SMS_RECEIVED:
            printf("📩 SMS from %s: %s\n", event->sms.sender, event->sms.message);
			sim800l_prepare_for_ucs2_sms(); 
			sim800l_send_sms_utf8("+989150471594", "سلام! این یک پیام فارسی تستی است از طرف تراشه .");
			sim800l_restore_default_sms_mode();  
            break;
        case SIM800L_EVENT_CALL_INCOMING:
            printf("📞 Incoming call from: %s\n", event->call.number);
            sim800l_hangup_call();
            break;
    }
}


void app_main(void) {
    sim800l_config_t conf = {
        .uart_port = UART_NUM_1,
        .tx_pin = 17,
        .rx_pin = 16,
        .rts_pin = UART_PIN_NO_CHANGE,
        .cts_pin = UART_PIN_NO_CHANGE,
        .baud_rate = 9600,
        .uart_buffer_size = 1024
    };

    sim800l_init(&conf);
    sim800l_wait_for_network(15000);
    sim800l_register_event_handler(&sim_event_handler);
    sim800l_start_event_loop();
    while (1){
		vTaskDelay(pdMS_TO_TICKS(1000));
	}  	
}

