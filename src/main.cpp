#include "../lib/Nova_Fitness_Sds_dust_sensors_library/src/SdsDustSensor.h"
#include <ESP8266HTTPClient.h>
#include <ESP8266WiFi.h>
#include <../lib/Time-master/TimeLib.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>

#define ARRAY_LENGTH 5
#define WORKING_PERIOD 30 * 1000
#define SLEEPING_PERIOD 30 * 1000
const int SENSOR_RX_PIN = 4;
const int SENSOR_DX_PIN = 5;

SdsDustSensor sds(SENSOR_RX_PIN, SENSOR_DX_PIN);
int currentReadingIndex = -1;
int step = 1;
unsigned long currentTimeMillis = 0;
boolean firstPass = true;

MDNSResponder mdns;
ESP8266WebServer server(80);

struct Measure {
    time_t measureTime;
    float pm25;
    float pm10;
};

struct Measure measures[ARRAY_LENGTH];

String getTimeString(time_t time) {
    char measureString[10];
    snprintf(measureString, 10, "%02d:%02d:%02d", hour(time), minute(time), second(time));
    return String(measureString);
}

String measureToString(Measure measure) {
    char measureString[50];
    snprintf(measureString, 50, "%02d:%02d:%02d - PM2.5 = %.1f, PM10 = %.1f\n",
             hour(measure.measureTime),
             minute(measure.measureTime),
             second(measure.measureTime),
             measure.pm25, measure.pm10
    );
    return String(measureString);
}

String measuresToString(boolean html) {
    String measuresString = "";
    for (auto &measure : measures) {
        time_t currTime = measure.measureTime;
        if (currTime != -1) {
            measuresString += measureToString(measure);
            if (html) {
                measuresString += "<br>";
            }
        }
    }
    return measuresString;
}

void setup() {
    Serial.begin(115200);
    sds.begin(9600);

    Serial.println(sds.queryFirmwareVersion().toString()); // prints firmware version
    Serial.println(sds.setQueryReportingMode().toString()); // ensures sensor is in 'query' reporting mode

    const char *ssid = "ALEKSNET";
    WiFi.begin(ssid, "ekvatorthebest");   //WiFi connection

    while (WiFi.status() != WL_CONNECTED) {  //Wait for the WiFI connection completion
        delay(500);
        Serial.println("Waiting for connection");
    }

    Serial.println("");
    Serial.print("Connected to ");  //  "Подключились к "
    Serial.println(ssid);
    Serial.print("IP address: ");  //  "IP-адрес: "
    Serial.println(WiFi.localIP());

    if (mdns.begin("esp8266", WiFi.localIP())) {
        Serial.println("MDNS responder started");
        //  "Запущен MDNSresponder"
    }

    server.on("/", []() {
        server.send(200, "text/html", measuresToString(true));
    });
    server.begin();

    for (auto &reading : measures) {
        reading = (Measure) {-1, -1, -1};
    }
    sds.wakeup();
    currentTimeMillis = millis();
    Serial.print(getTimeString(now()));
    Serial.println(" - The sensor should be woken now");
}

void printMeasures() {
    Serial.println(measuresToString(false));
}

void loop() {
    server.handleClient();
    if (millis() - currentTimeMillis > WORKING_PERIOD && step == 1) {
        PmResult pm = sds.queryPm();
        Serial.print(getTimeString(now()));
        Serial.println(" - Checking the sensor");

        if (pm.isOk()) {

            Serial.println();
            Serial.println("----------------------");
            Serial.println();

            if (++currentReadingIndex <= ARRAY_LENGTH - 1 && firstPass) {
                measures[currentReadingIndex] = {now(), pm.pm25, pm.pm10};
            } else {
                for (int i1 = 1, i2 = 0; i1 < ARRAY_LENGTH; i1++, i2++) {
                    // Shift all the array's content on one position
                    measures[i2] = measures[i1];
                }
                measures[ARRAY_LENGTH - 1] = {now(), pm.pm25, pm.pm10};
                if (firstPass) {
                    firstPass = false;
                }
            }

            printMeasures();

        } else {
            Serial.print("Could not read values from sensor, reason: ");
            Serial.println(pm.statusToString());
        }

        WorkingStateResult state = sds.sleep();
        Serial.print(getTimeString(now()));
        Serial.println(" - The sensor should be sleeping now");

        if (state.isWorking()) {
            Serial.println("Problem with sleeping the sensor.");
        }
        currentTimeMillis = millis();
        step = 2;
    }
    if (millis() - currentTimeMillis > SLEEPING_PERIOD && step == 2) {
        currentTimeMillis = millis();
        step = 1;
        sds.wakeup();
        Serial.print(getTimeString(now()));
        Serial.println(" - The sensor should be woken now");
    }
}
