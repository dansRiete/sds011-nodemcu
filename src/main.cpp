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
int currentReadingIndex = -1;
boolean shiftArray = false;

struct Measure {
    time_t measureTime;
    float pm25;
    float pm10;
};

struct Measure measures[ARRAY_LENGTH];

void printMeasure(Measure measure) {
    time_t currTime = measure.measureTime;
    Serial.printf("%02d", hour(currTime));
    Serial.print(":");
    Serial.printf("%02d", minute(currTime));
    Serial.print(":");
    Serial.printf("%02d", second(currTime));
    Serial.print(" - ");
    Serial.print("PM2.5 = ");
    Serial.printf("%.1f", measure.pm25);
    Serial.print(", ");
    Serial.print("PM10 = ");
    Serial.printf("%.1f", measure.pm10);
    Serial.println();
}

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

    for (auto &reading : measures) {
        reading = (Measure) {-1, -1, -1};
    };
}

void loop() {
    sds.wakeup();
    delay(WORKING_PERIOD); // working 30 seconds

    PmResult pm = sds.queryPm();
    if (pm.isOk()) {

        Serial.println("");
        Serial.println("----------------------");
        Serial.println("");

        if (++currentReadingIndex <= ARRAY_LENGTH - 1 && !shiftArray) {
            measures[currentReadingIndex] = {now(), pm.pm25, pm.pm10};
        } else {
            if (!shiftArray) {
                shiftArray = true;
            }
            for (int i1 = 1, i2 = 0; i1 < ARRAY_LENGTH; i1++, i2++) {
                measures[i2] = measures[i1];
            }
            measures[ARRAY_LENGTH - 1] = {now(), pm.pm25, pm.pm10};
        }

        for (auto &measure : measures) {
            time_t currTime = measure.measureTime;
            if (currTime != -1) {
                printMeasure(measure);
            }
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
        delay(SLEEPING_PERIOD); // wait 1 minute
    }
}
