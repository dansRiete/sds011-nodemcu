#include "../lib/Nova_Fitness_Sds_dust_sensors_library/src/SdsDustSensor.h"
#include <ESP8266HTTPClient.h>
#include <ESP8266WiFi.h>
#include <../lib/Time-master/TimeLib.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <math.h>

#define MEASURES_NUMBER_TO_STORE 1080
#define LAST_MEASURES_NUMBER_TO_STORE 15
#define WORKING_PERIOD 5*1000
#define SLEEPING_PERIOD 54*1000
const int SENSOR_RX_PIN = 4;
const int SENSOR_DX_PIN = 5;

SdsDustSensor sds(SENSOR_RX_PIN, SENSOR_DX_PIN);
int currentLastReadingIndex = -1;
int currentReadingIndex = -1;
int step = 1;
unsigned long currentTimeMillis = 0;
boolean firstPass = true;
boolean thereIsMore = false;
int thereIsMoreCounter = 0;

MDNSResponder mdns;
ESP8266WebServer server(80);

struct Measure {
    time_t measureTime;
    int pm25;
    int pm10;
};

struct Measure measures[MEASURES_NUMBER_TO_STORE];
struct Measure lastMeasures[LAST_MEASURES_NUMBER_TO_STORE];

String getTimeString(time_t time) {
    char measureString[10];
    snprintf(measureString, 10, "%02d:%02d:%02d", hour(time), minute(time), second(time));
    return String(measureString);
}

String measureToString(Measure measure) {
    char measureString[60];
    snprintf(measureString, 60, "%dD %02d:%02d:%02d - PM2.5 = %*.*s, PM10 = %*.*s, Total = %*.*s\n",
             day(measure.measureTime),
             hour(measure.measureTime),
             minute(measure.measureTime),
             second(measure.measureTime),
             4, 4, String(measure.pm25).c_str(),
             4, 4, String(measure.pm10).c_str(),
             4, 4, String(measure.pm25 + measure.pm10).c_str()
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
    for (auto &measure : lastMeasures) {
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
    sds.setCustomWorkingPeriod(0);

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
        server.setContentLength(CONTENT_LENGTH_UNKNOWN);
        String content = measuresToString(true);
        Serial.print("content length: ");
        Serial.println(content.length());
        server.send(200, "text/html", content);
        while(thereIsMore){
            content = measuresToString(true);
            server.sendContent(content);
        }
        thereIsMore = false;
        server.client().stop();
    });
    server.begin();

    for (auto &reading : measures) {
        reading = (Measure) {-1, -1, -1};
    }
    for (auto &reading : lastMeasures) {
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
            int pm25 = (int) round((double) pm.pm25);
            int pm10 = (int) round((double) pm.pm10);
            Measure currentMeasure = {now(), pm25, pm10};

            if (++currentLastReadingIndex > LAST_MEASURES_NUMBER_TO_STORE - 1) {
                double pm25Summ = 0;
                double pm10Summ = 0;
                int number = 0;
                time_t lastTime;
                for (auto &reading : lastMeasures) {
                    number++;
                    pm25Summ += reading.pm25;
                    pm10Summ += reading.pm10;
                    lastTime = reading.measureTime;
                    reading = (Measure) {-1, -1, -1};
                }
                currentLastReadingIndex = 0;
                Measure thisMeasure = {lastTime, (int) round(pm25Summ/number), (int) round(pm10Summ/number)};

                if (++currentReadingIndex <= MEASURES_NUMBER_TO_STORE - 1 && firstPass) {
                    measures[currentReadingIndex] = thisMeasure;
                } else {
                    for (int i1 = 1, i2 = 0; i1 < MEASURES_NUMBER_TO_STORE; i1++, i2++) {
                        // Shift all the array's content on one position
                        measures[i2] = measures[i1];
                    }
                    measures[MEASURES_NUMBER_TO_STORE - 1] = thisMeasure;
                    if (firstPass) {
                        firstPass = false;
                    }
                }
            }

            lastMeasures[currentLastReadingIndex] = currentMeasure;

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
