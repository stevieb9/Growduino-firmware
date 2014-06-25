#include "GrowduinoFirmware.h"
extern Alert alerts[];
extern Output outputs;
extern Trigger triggers[];
extern int ups_level;
extern Config config;

Alert::Alert(){
    on_message = NULL;
    off_message = NULL;
    target = NULL;
    init();
}

void Alert::init(){
    trigger = NONE;
    last_state = NONE;
    idx = NONE;
    trash_strings();
}

void Alert::load(aJsonObject *msg, int index){
    //Init new Alert from aJSON
    //extract the trigger from ajson using
    //aJsonObject* msg = aJson.getObjectItem(root, "trigger");

    //init();
    idx = index;
    int json_strlen;

    aJsonObject * cnfobj = aJson.getObjectItem(msg, "on_message");
    if (cnfobj->type == aJson_String)  {
        json_strlen = strnlen(cnfobj->valuestring, ALARM_STR_MAXSIZE);
        on_message = (char *) malloc(json_strlen + 1);
        if (on_message == NULL) {
            Serial.println(F("OOM on alert load (on_message)"));
        } else {
            strlcpy(on_message, cnfobj->valuestring, json_strlen +1);
        }
    }

    cnfobj = aJson.getObjectItem(msg, "off_message");
    if (cnfobj->type == aJson_String)  {
        json_strlen = strnlen(cnfobj->valuestring, ALARM_STR_MAXSIZE);
        off_message = (char *) malloc(json_strlen + 1);
        if (off_message == NULL) {
            Serial.println(F("OOM on alert load (off_message)"));
        } else {
            strlcpy(off_message, cnfobj->valuestring, json_strlen +1);
        }
    }

    cnfobj = aJson.getObjectItem(msg, "target");
    if (cnfobj->type == aJson_String)  {
        json_strlen = strnlen(cnfobj->valuestring, ALARM_STR_MAXSIZE);
        target = (char *) malloc(json_strlen + 1);
        if (target == NULL) {
            Serial.println(F("OOM on alert load (target)"));
        } else {
            strlcpy(target, cnfobj->valuestring, json_strlen +1);

        }
    }

    cnfobj = aJson.getObjectItem(msg, "trigger");
    if (cnfobj && cnfobj->type == aJson_Int) {
        trigger = cnfobj->valueint;
    }
}

int Alert::process_alert(int trigger_state){
    if (last_state != trigger_state) {

#ifdef DEBUG_ALERTS
        Serial.print(F("Alarm - triger "));
        Serial.print(trigger);
        Serial.print(F(" changed state to: "));
        Serial.println(trigger_state);
#endif
        last_state = trigger_state;
        send_message();
    }
    return last_state;
}

int Alert::send_message() {
        Serial.println(last_state);
    if (target == NULL) {
        //load config from sd
        char fname[] = "XX.jso";

        sprintf(fname, "%i.jso", idx);
        aJsonObject * cfile = file_read("/alerts", fname);
        if (cfile != NULL) {
            load(cfile, idx);
        }
        json(&Serial);
        aJson.deleteItem(cfile);
        Serial.println(last_state);
    }
#ifdef DEBUG_ALERTS
    Serial.print(F("Alarm target "));
    Serial.println(target);
#endif

    if (strchr(target, '@') != NULL) {
#ifdef DEBUG_ALERTS
        Serial.print(F("Sending mail"));
#endif
        //send mail
        int size;
        char subject[32];
        char * body;
        char * line_end;
        Serial.println(last_state);
        if (last_state == S_OFF) {
            body = off_message;
#ifdef DEBUG_ALERTS
        Serial.print(F("Sending off message"));
#endif
        } else {
            body = on_message;
#ifdef DEBUG_ALERTS
        Serial.print(F("Sending on message"));
#endif
        }
        size = 32;
        line_end = strchrnul(body, '\r');
        if ((line_end - body) <  size) size = line_end - body;
        line_end = strchrnul(body, '\n');
        if ((line_end - body) <  size) size = line_end - body;

        strlcpy(subject, body, size);  // copy first line of body to subject
        pFreeRam();
        send_mail(target, subject, body);
        pFreeRam();
        trash_strings();
        return 0;
    }
    trash_strings();
    return 1;
}

int Alert::tick() {
    if (last_state == NONE) {
        if (trigger == -2)
            last_state = ups_level < config.ups_trigger_level;
        else
            last_state = triggers[trigger].state;
        return NONE;
    }
    if (trigger == -2) {
        last_state = process_alert(ups_level < config.ups_trigger_level);
    } else {
        last_state = process_alert(triggers[trigger].state);
    }
    return last_state;
}

void Alert::json(Stream * cnfdata){
    //writes object data to stream

    cnfdata->print(F("{"));
    cnfdata->print(F("\"on_message\":\""));
    cnfdata->print(on_message);
    cnfdata->print(F("\", \"off_message\":\""));
    cnfdata->print(off_message);
    cnfdata->print(F("\", \"target\":\""));
    cnfdata->print(target);
    cnfdata->print(F("\", \"trigger\":"));
    cnfdata->print(trigger, DEC);
    cnfdata->print(F("}"));
}

void Alert::trash_strings(){
    if (on_message != NULL) {
        free(on_message);
        on_message = NULL;
    }
    if (off_message != NULL) {
        free(off_message);
        off_message = NULL;
    }
    if (target != NULL) {
        free(target);
        target = NULL;
    }
}

void alert_load(aJsonObject * cfile, int alert_no) {
    alerts[alert_no].load(cfile, alert_no);
}


void alert_save(Alert alerts[], int idx){
    char fname[] = "XX.jso";

    sprintf(fname, "%i.jso", idx);
    file_for_write("/alerts", fname, &sd_file);

#ifdef DEBUG_ALERTS
    Serial.print(F("Preparing json "));
    Serial.println(idx, DEC);
#endif
    alerts[idx].json(&sd_file);
#ifdef DEBUG_ALERTS
    Serial.println(F("saving"));
#endif
    sd_file.close();
    alerts[idx].trash_strings();
}

/*
void alerts_save(Alert alerts[]){
#ifdef DEBUG_ALERTS
        Serial.println(F("Save alerts"));
#endif
        for (int i=0; i < ALERTS; i++) {
            alert_save(alerts, i);
        }
#ifdef DEBUG_ALERTS
                Serial.println(F("Saved."));
#endif
}
*/

int alerts_load(Alert alerts[]){
    char fname[] = "XX.jso";

    for (int i=0; i < ALERTS; i++) {
        alerts[i].idx = i;
        sprintf(fname, "%i.jso", i);
        aJsonObject * cfile = file_read("/alerts", fname);
        if (cfile != NULL) {
            alert_load(cfile, i);
            aJson.deleteItem(cfile);
            alert_save(alerts, i);
        }
    }
    return ALERTS;
}
