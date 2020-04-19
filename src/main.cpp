#include "../lib/Nova_Fitness_Sds_dust_sensors_library/src/SdsDustSensor.h"
#include <ESP8266HTTPClient.h>
#include <ESP8266WiFi.h>
#include <../lib/Time-master/TimeLib.h>

#define ARRAY_LENGTH 5
#define STRING_LENGTH 50
#define WORKING_PERIOD 30000
#define SLEEPING_PERIOD 30000
#define DEBUG false

int rxPin = 4;
int txPin = 5;
SdsDustSensor sds(rxPin, txPin);
char readings[ARRAY_LENGTH][STRING_LENGTH];
int currentReadingIndex = -1;
boolean shiftArray = false;


void setup() {
    Serial.begin(115200);
    sds.begin(9600);

    Serial.println(sds.queryFirmwareVersion().toString()); // prints firmware version
    Serial.println(sds.setQueryReportingMode().toString()); // ensures sensor is in 'query' reporting mode

    WiFi.begin("ALEKSNET", "ekvatorthebest");   //WiFi connection

    while (WiFi.status() != WL_CONNECTED) {  //Wait for the WiFI connection completion
        delay(500);
        Serial.println("Waiting for connection");
    }

    for (auto &reading : readings) {
        strcpy(reading, "");
    }
}

void loop() {
    sds.wakeup();
    delay(WORKING_PERIOD); // working 30 seconds

    PmResult pm = sds.queryPm();
    if (pm.isOk()) {

        Serial.println("");
        Serial.println("----------------------");
        Serial.println("");

        char url[] = "";
        snprintf(url, STRING_LENGTH, "%lu - %s %.1f, %s %.1f", millis(), "PM2.5 =", pm.pm25, "PM10 =", pm.pm10);

        if (++currentReadingIndex <= ARRAY_LENGTH - 1 && !shiftArray) {
            if (DEBUG) {
                Serial.print("Putting to array with index ");
                Serial.println(currentReadingIndex);
            }
            strcpy(readings[currentReadingIndex], url);
        } else {
            if (!shiftArray) {
                shiftArray = true;
            }

            for (int i1 = 1, i2 = 0; i1 < ARRAY_LENGTH; i1++, i2++) {
                if (DEBUG) {
                    Serial.print("Shifting to array with index ");
                    Serial.println(i2);
                }
                strcpy(readings[i2], readings[i1]);
            }
            if (DEBUG) {
                Serial.print("Putting to array with index ");
                Serial.println(ARRAY_LENGTH - 1);
            }
            strcpy(readings[ARRAY_LENGTH - 1], url);
        }

        for (int i = 0; i < ARRAY_LENGTH; i++) {
            Serial.print(i);
            Serial.print(". ");
            Serial.println(readings[i]);
        }

//        HTTPClient http;    //Declare object of class HTTPClient
//        http.begin(url);
//        http.end();  //Close connection


    } else {
        Serial.print("Could not read values from sensor, reason: ");
        Serial.println(pm.statusToString());
    }

    WorkingStateResult state = sds.sleep();
    if (state.isWorking()) {
        Serial.println("Problem with sleeping the sensor.");
    } else {
        if (DEBUG) {
            Serial.println("Sensor is sleeping");
        }
        delay(SLEEPING_PERIOD); // wait 1 minute
    }
}