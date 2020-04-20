#include "../lib/Nova_Fitness_Sds_dust_sensors_library/src/SdsDustSensor.h"
#include <ESP8266HTTPClient.h>
#include <ESP8266WiFi.h>
#include <../lib/Time-master/TimeLib.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>

#define MEASURES_NUMBER_TO_STORE 1000
#define WORKING_PERIOD 35*1000
#define SLEEPING_PERIOD 3*60*1000
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

struct Measure measures[MEASURES_NUMBER_TO_STORE];

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

void printAllMeasures() {
    Serial.println(measuresToString(false));
}

void printMeasure(Measure measure) {
    Serial.println(measureToString(measure));
}

void setup() {
    Serial.begin(115200);
    sds.begin(9600);
    sds.setQueryReportingMode();
    sds.setCustomWorkingPeriod(1);

    Serial.println(sds.queryFirmwareVersion().toString()); // prints firmware version
    Serial.println(sds.setQueryReportingMode().toString()); // ensures sensor is in 'query' reporting mode

    const char *ssid = "ALEKSNET";
    WiFi.begin(ssid, "ekvatorthebest");   //WiFi connection

    while (WiFi.status() != WL_CONNECTED) {  //Wait for the WiFI connection completion
        delay(500);
        Serial.println("Waiting for connection");
    }

    Serial.println(""); Serial.print("Connected to "); Serial.println(ssid); Serial.print("IP address: "); Serial.println(WiFi.localIP());

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
//    Serial.print(getTimeString(now()));
//    Serial.println(" - The sensor should be woken now");
}

void loop() {
    server.handleClient();
    if (millis() - currentTimeMillis > WORKING_PERIOD && step == 1) {
        PmResult pm = sds.queryPm();
//        Serial.print(getTimeString(now()));
//        Serial.println(" - Checking the sensor");

        if (pm.isOk()) {
            Measure currentMeasure = {now(), pm.pm25, pm.pm10};
            if (++currentReadingIndex <= MEASURES_NUMBER_TO_STORE - 1 && firstPass) {
                measures[currentReadingIndex] = currentMeasure;
            } else {
                for (int i1 = 1, i2 = 0; i1 < MEASURES_NUMBER_TO_STORE; i1++, i2++) {
                    // Shift all the array's content on one position
                    measures[i2] = measures[i1];
                }
                measures[MEASURES_NUMBER_TO_STORE - 1] = currentMeasure;
                if (firstPass) {
                    firstPass = false;
                }
            }

            printMeasure(currentMeasure);

        } else {
            Serial.print("Could not read values from sensor, reason: ");
            Serial.println(pm.statusToString());
        }

        WorkingStateResult state = sds.sleep();
//        Serial.print(getTimeString(now()));
//        Serial.println(" - The sensor should be sleeping now");

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
//        Serial.print(getTimeString(now()));
//        Serial.println(" - The sensor should be woken now");
    }
}
