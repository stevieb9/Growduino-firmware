#include "GrowduinoFirmware.h"

#include <DHT22.h>

#include <Wire.h>
#include <Time.h>
#include <stdio.h>

#include <aJSON.h>

#include <SD.h>

#include <SPI.h>
#include <Ethernet.h>
#include <string.h>
#include <stdio.h>
#include <DS1307RTC.h>

int ether = 1;


// DHT22 temp and humidity sensor. Treated as main temp and humidity source
#define DHT22_PIN 23
DHT22 myDHT22(DHT22_PIN);

//profiling
unsigned long t_loop_start;
unsigned long t_1;
unsigned long t_2;
unsigned long t_3;

Logger dht22_temp = Logger("Temp1");

Logger dht22_humidity = Logger("Humidity");
// light sensor on analog A15
#define LIGHT_SENSOR_PIN 15
Logger light_sensor = Logger("Light");

Config config;

aJsonStream serial_stream(&Serial);

EthernetServer server(80);

Logger * loggers[] = {&dht22_humidity, &dht22_temp, &light_sensor};

int loggers_no = 3;

void setup(void) {
    // start serial port
    Serial.begin(115200);
    Serial.println("Grow!");
    delay(1000);
    // serial_buffer = (char*) malloc (1024 * sizeof(int));
    Serial.println("SDcard init");
    sdcard_init();
    char index[] = "/INDEX.HTM";
    if (SD.exists(index)) {
        Serial.println("Card ok");
    } else {
        Serial.println("INDEX.HTM not found. Not going on");
        while (true) {
            digitalWrite(13, 1 - digitalRead(13));
            delay(300);
            }
    }
    Serial.println("Initialising eth");

    // load config from sdcard
    aJsonObject * cfile = file_read("", "config.jso");
    if (cfile != NULL) {
        Serial.println("Loading config");
        config = Config(cfile);
        aJson.deleteItem(cfile);
    } else {
        Serial.println("Default config");
        config = Config();
        config.save();
    }
    byte mac[] = {0xDE, 0xAD, 0xBE, 0xEF, 0x55, 0x44};
    if (config.use_dhcp == 1) {
        Ethernet.begin(mac);
    } else {
        Ethernet.begin(mac, config.ip, config.gateway, config.netmask);
    }
    server.begin();
    Serial.print("server is at ");
    Serial.println(Ethernet.localIP());

    // start real time clock
    Serial.println("Initialising clock");
    daytime_init();

    //load data from sd card
    dht22_temp.load();
    dht22_humidity.load();
    light_sensor.load();

    Serial.println("Wasting time (2s)");
    delay(2000);
    // init temp/humidity logger 
    myDHT22.readData();
    Serial.print("DHT22 Sensor - temp: ");
    Serial.print(myDHT22.getTemperatureC());
    Serial.print(" humidity: ");
    Serial.println(myDHT22.getHumidity());
    Serial.print("Light sensor: ");
    Serial.println(analogRead(LIGHT_SENSOR_PIN));
}

void worker(){

    //cteni ze sensoru
    pinMode(13, OUTPUT);
    myDHT22.readData();
    dht22_temp.timed_log(myDHT22.getTemperatureCInt());
    dht22_humidity.timed_log(myDHT22.getHumidityInt());
    light_sensor.timed_log(analogRead(LIGHT_SENSOR_PIN));

    Serial.print("dht22_temp=");
    aJsonObject *msg = dht22_temp.json();
    aJson.print(msg, &serial_stream);
    aJson.deleteItem(msg);
    Serial.println();
    Serial.print("dht22_humidity=");
    msg = dht22_humidity.json();
    aJson.print(msg, &serial_stream);
    aJson.deleteItem(msg);
    Serial.println();
    Serial.print("light_sensor=");
    msg = light_sensor.json();
    aJson.print(msg, &serial_stream);
    aJson.deleteItem(msg);
    Serial.println();
}

const char * get_filename_ext(const char *filename){
    const char *dot = strrchr(filename, '.');
    if (!dot || dot == filename) {
        return "";
    }
    return dot + 1;
};


const char * getContentType(char * filename) {
    //odhadne mimetype
    char ext[4];

    strncpy(ext, get_filename_ext(filename), 4);
    ext[3] = '\0';

    //preved priponu na mala
    for (char *p = ext ; *p; ++p) *p = tolower(*p);

    if (strcmp(ext, "htm") == 0)
        return "Content-Type: text/html";
    if (strcmp(ext, "jso") == 0)
        return "Content-Type: application/json";
    if (strcmp(ext, "js") == 0)
        return "Content-Type: text/javascript";
    if (strcmp(ext, "css") == 0)
        return "Content-Type: text/css";
    if (strcmp(ext, "png") == 0)
        return "Content-Type: image/png";
    if (strcmp(ext, "jpg") == 0)
        return "Content-Type: image/jpeg";
    return "Content-Type: text/plain";
}

void responseNum(EthernetClient client, int code){
    Serial.print("Returning ");
    switch (code){
        case 200:
            client.println("HTTP/1.1 200 OK");
            Serial.println(200);
            break;
        case 404:
            Serial.println(404);
            client.println("HTTP/1.1 404 Not Found");
            client.println("Content-Type: text/html");
            client.println();
            client.println("<h2>File Not Found!</h2>");
            break;
    }

}

const char * extract_filename(char * line, int action = 1){
    // http://www.ladyada.net/learn/arduino/ethfiles.html ripoff
    if (strstr(line, "GET / ") != 0) {
        return "index.htm";
    }
    if (strstr(line, "GET /") != 0) {
        char *filename;
        filename = line + 5; // 'GET /' is 5 chars
        // convert space before HTTP to null, trimming the string
        (strstr(line, " HTTP"))[0] = 0;
        return filename;
    }
    return NULL;
}

int senddata(EthernetClient client, char * request, char * clientline){
    // Send response
    bool found = false;
    Serial.print("Request: ");
    Serial.println(request);
    if (strncasecmp(request, "sensors", 7) == 0) {
        Serial.println("Sensor area");
        for (int i = 0; i < loggers_no; i++) {
            if (loggers[i]->match(request)){
                found = true;
                responseNum(client, 200);
                client.println(getContentType(request));
                client.println("Access-Control-Allow-Origin: *");
                client.println("cache-control: max-age=30");
                client.println("Connection: close");
                client.println();
                aJsonStream eth_stream(&client);
                aJsonObject *msg = loggers[i]->json_dynamic();
                aJson.print(msg, &eth_stream);
                aJson.deleteItem(msg);
                return 1;
            }
        }
        if (!found)
            responseNum(client, 404);
        return 0;
    }
    Serial.println("File area");
    // Find the file
    if (!SD.exists(request)) {
        responseNum(client, 404);
        return 0;
    }

    // Now we know file exists, so serve it
    responseNum(client, 200);

    client.println(getContentType(request));
    client.println("Access-Control-Allow-Origin: *");
    // client.println("cache-control: max-age=86400");
    client.println("cache-control: max-age=600");
    client.println("Connection: close");  // the connection will be closed after completion of the response
    client.println();
    File dataFile = SD.open(request, FILE_READ);
    // abuse clientline as sd buffer
    int remain = 0;
    while ((remain = dataFile.available())) {
        remain = min(remain, BUFSIZ -1);
        dataFile.read(clientline, remain);
        clientline[remain] = 0;
        client.write(clientline);
    };
    dataFile.close();
    return 1;
}

void pageServe(EthernetClient client){
    char request[32];
    char clientline[BUFSIZ];
    int index;
    int action;
    int linesize;
    bool is_url;

    index = 0;
    action = 0;
    request[0] = 0;

    t_1 = millis();
    while (client.connected()) {
        if (client.available()) {
            linesize = client.readBytesUntil('\n', clientline, BUFSIZ-1);
            clientline[linesize] = '\0';

            // the line is blank, the http request has ended,
            // so you can send a reply
            if (linesize <= 1) {
                t_2 = millis();
                senddata(client, request, clientline);
                t_3 = millis();

                break;
            }
            is_url = false;
            if (strstr(clientline, "GET /") != 0) {
                action = GET;
                is_url = true;
            } else if (strstr(clientline, "POST /") != 0) {
                action = POST;
                is_url = true;
            };
            if (is_url) {
                strcpy(request, extract_filename(clientline));
            }
        }
    }
    // give the web browser time to receive the data
    delay(1);
    // close the connection:
    client.stop();
}


void loop(void){
    t_loop_start = millis();
    if (dht22_temp.available()) {
        worker();
    }
    EthernetClient client = server.available();
    if (client) {
        pageServe(client);
        Serial.println("times:");
        Serial.println(t_1 - t_loop_start);
        Serial.println(t_2 - t_loop_start);
        Serial.println(t_3 - t_loop_start);
        Serial.println(millis() - t_loop_start);

    }
    delay(5);
}
