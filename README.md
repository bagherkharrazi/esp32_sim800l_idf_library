# esp32_sim800l_idf_library
sim800l sms and call management library for idf
it supports also persian sms send
manage your actions in cases
enjoy
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
