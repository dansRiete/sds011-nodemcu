#include <../lib/Time-master/TimeLib.h>
#include <math.h>
#include <Arduino.h>
#include <../lib/arduino-ds1302-master/DS1302.h>
#include "../lib/Nova_Fitness_Sds_dust_sensors_library/src/SdsDustSensor.h"
#include <ESP8266HTTPClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <../lib/Adafruit_Sensor-master/Adafruit_Sensor.h>
#include <../lib/DHT-sensor-library-master/DHT.h>
#include <../lib/DHT-sensor-library-master/DHT_U.h>

#define WORKING_PERIOD 5*1000
#define SLEEPING_PERIOD 54*1000
#define DHTPIN 0
#define DHTTYPE           DHT11
//#define SLEEPING_PERIOD 0
const boolean DEBUG = true;
const int SENSOR_RX_PIN = 4;
const int SENSOR_DX_PIN = 5;
const int CLK_PIN = 14;
const int DAT_PIN = 12;
const int RESX_PIN = 13;
unsigned long currentTimeMillis = 0;
int step = 1;

DS1302 rtc(RESX_PIN, DAT_PIN, CLK_PIN);
MDNSResponder mdns;
ESP8266WebServer server(80);
SdsDustSensor sds(SENSOR_RX_PIN, SENSOR_DX_PIN);
DHT_Unified dht(DHTPIN, DHTTYPE);

struct Measure {
    time_t measureTime;
    float pm25;
    float pm10;
    float temp;
    float humid;
};

Measure nullMeasure = {0, -1, -1, -1, -1};

#define EVERY_MEASURES_NUMBER 60
struct Measure everyMeasures[EVERY_MEASURES_NUMBER];
int everyMeasureIndex = -1;
boolean everyMeasureFirstPass = true;

#define EVERY_15_MINUTES_MEASURES_NUMBER 4*24
struct Measure every15minutesMeasures[EVERY_15_MINUTES_MEASURES_NUMBER];
int every15MinutesMeasureIndex = -1;
boolean every15MinutesMeasureFirstPass = true;
time_t every15minuteTimer;

#define EVERY_HOUR_MEASURES_NUMBER 3*24
struct Measure everyHourMeasures[EVERY_HOUR_MEASURES_NUMBER];
int everyHourMeasureIndex = -1;
boolean everyHourMeasureFirstPass = true;
time_t everyHourTimer;

boolean thereIsMore = false;
int thereIsMoreCounter = 0;
int totalCounter = 0;
int totalAveragedCounter = 0;

void logAverage(const Measure &measure);

String getTimeString(time_t time) {
    char measureString[10];
    snprintf(measureString, 10, "%02d:%02d:%02d", hour(time), minute(time), second(time));
    return String(measureString);
}

String measureToString(Measure measure) {
    char measureString[80];
    snprintf(measureString, 80, "%02d/%02d/%d %02d:%02d:%02d - PM2.5 = %.1f, PM10 = %.1f, temp = %.1fC, humid = %.0f%%",
             day(measure.measureTime),
             month(measure.measureTime),
             year(measure.measureTime),
             hour(measure.measureTime),
             minute(measure.measureTime),
             second(measure.measureTime),
             measure.pm25,
             measure.pm10,
             measure.temp,
             measure.humid
    );
    return String(measureString);
}

String measuresToString(boolean html, Measure measuresToPrint[], int length) {
    String measuresString = "";
    int i1 = 0;
    for (int i = thereIsMore ? thereIsMoreCounter : length - 1; i >= 0; i--) {
        if (i1 > 25) {
            thereIsMoreCounter = i;
            break;
        }
        Measure measure = measuresToPrint[i];
        time_t currTime = measure.measureTime;
        if (currTime != 0) {
            measuresString += measureToString(measure);
            if (html) {
                measuresString += "<br>";
            }
            i1++;
        }
    }
    thereIsMore = i1 > 25;
    return measuresString;
}

void printAllMeasures(Measure measures[], int length) {
    for(int i = 0; i < length; i++){
        if (measures[i].pm10 != -1) {
            Serial.print(getTimeString((measures[i].measureTime)));
            Serial.print(" PM2.5 = ");
            Serial.print(measures[i].pm25);
            Serial.print("; PM10 = ");
            Serial.println(measures[i].pm10);
        }
    }
}

void printMeasure(Measure measure) {
    Serial.println(measureToString(measure));
}

void putEveryMeasure(Measure measure){
    if (++everyMeasureIndex <= EVERY_MEASURES_NUMBER - 1 && everyMeasureFirstPass) {
        everyMeasures[everyMeasureIndex] = measure;
    } else {
        for (int i1 = 1, i2 = 0; i1 < EVERY_MEASURES_NUMBER; i1++, i2++) {
            // Shift all the array's content on one position
            everyMeasures[i2] = everyMeasures[i1];
        }
        everyMeasures[EVERY_MEASURES_NUMBER - 1] = measure;
        if (everyMeasureFirstPass) {
            everyMeasureFirstPass = false;
        }
    }
}

void putEvery15MinuteMeasure(Measure measure){
    if (++every15MinutesMeasureIndex <= EVERY_15_MINUTES_MEASURES_NUMBER - 1 && every15MinutesMeasureFirstPass) {
        every15minutesMeasures[every15MinutesMeasureIndex] = measure;
    } else {
        for (int i1 = 1, i2 = 0; i1 < EVERY_15_MINUTES_MEASURES_NUMBER; i1++, i2++) {
            // Shift all the array's content on one position
            every15minutesMeasures[i2] = every15minutesMeasures[i1];
        }
        every15minutesMeasures[EVERY_15_MINUTES_MEASURES_NUMBER - 1] = measure;
        if (every15MinutesMeasureFirstPass) {
            every15MinutesMeasureFirstPass = false;
        }
    }
}

void putEveryHourMeasure(Measure measure){
    if (++everyHourMeasureIndex <= EVERY_HOUR_MEASURES_NUMBER - 1 && everyHourMeasureFirstPass) {
        everyHourMeasures[everyHourMeasureIndex] = measure;
    } else {
        for (int i1 = 1, i2 = 0; i1 < EVERY_HOUR_MEASURES_NUMBER; i1++, i2++) {
            // Shift all the array's content on one position
            everyHourMeasures[i2] = everyHourMeasures[i1];
        }
        everyHourMeasures[EVERY_HOUR_MEASURES_NUMBER - 1] = measure;
        if (everyHourMeasureFirstPass) {
            everyHourMeasureFirstPass = false;
        }
    }
}

bool anHourElapsed(time_t startTime, time_t endTime) {
    return hour(startTime - endTime) >= 1;
}

bool fifteenMinutesElapsed(time_t startTime, time_t endTime) {
    return minute(startTime - endTime) >= 15;
}

bool inAnHourInterval(time_t startTime, time_t endTime) {
    return  year(startTime - endTime) == 1970 &&
            month(startTime - endTime) == 1 &&
            day(startTime - endTime) == 1 &&
            !anHourElapsed(startTime, endTime);
}

bool inFifteenMinutesInterval(time_t startTime, time_t endTime) {
    return  year(startTime - endTime) == 1970 &&
            month(startTime - endTime) == 1 &&
            day(startTime - endTime) == 1 &&
            hour(startTime - endTime) == 0 &&
            !fifteenMinutesElapsed(startTime, endTime);
}

Measure calculate15minuteAverage(time_t currentTime) {
    if( DEBUG){
        Serial.println();
        Serial.print(getTimeString(currentTime));
        Serial.print(" - Calculating 15 minutes average ... ");
    }
    float pm25Summ = 0;
    float pm10Summ = 0;
    float tempSumm = 0;
    float humidSumm = 0;
    int counter = 0;
    time_t lastTime = 0;
    for(int i = 0; i < EVERY_MEASURES_NUMBER; i++) {
        Measure measure = everyMeasures[i];
        if(measure.pm25 != -1 && inFifteenMinutesInterval(currentTime, measure.measureTime)){
            if(DEBUG){ logAverage(measure); }
            pm25Summ += measure.pm25;
            pm10Summ += measure.pm10;
            tempSumm += measure.temp;
            humidSumm += measure.humid;
            lastTime = measure.measureTime;
            counter++;
            totalAveragedCounter++;
        }
    }
    Measure result;

    if(counter != 0){
        result = {
                lastTime,
                static_cast<float>(round(pm25Summ/counter*10)/10),
                static_cast<float>(round(pm10Summ/counter*10)/10),
                static_cast<float>(round(tempSumm/counter*10)/10),
                static_cast<float>(round(humidSumm/counter*10)/10),
        };
    } else {
        result = nullMeasure;
    }

    if(DEBUG){
        Serial.println();
        Serial.print("There were ");
        Serial.print(counter);
        Serial.print(" elements averaged.");
        Serial.print(" Total averaged: ");
        Serial.println(totalAveragedCounter);
        Serial.print("Averaged measure: ");
        Serial.println(measureToString(result));
        Serial.println();
    }
    return result;
}

Measure calculate1HourAverage(time_t currentTime) {
    if(DEBUG){
        Serial.println();
        Serial.print(getTimeString(currentTime));
        Serial.print(" - Calculating 1 hour average ... ");
    }
    float pm25Summ = 0;
    float pm10Summ = 0;
    float tempSumm = 0;
    float humidSumm = 0;
    int counter = 0;
    time_t lastTime = 0;
    for(int i = 0; i < EVERY_15_MINUTES_MEASURES_NUMBER; i++) {
        Measure measure = every15minutesMeasures[i];
        if(measure.pm25 != -1 && inAnHourInterval(currentTime, measure.measureTime)){
            if(DEBUG){ logAverage(measure); }
            pm25Summ += measure.pm25;
            pm10Summ += measure.pm10;
            tempSumm += measure.temp;
            humidSumm += measure.humid;
            lastTime = measure.measureTime;
            counter++;
        }
    }

    Measure result;

    if(counter != 0){
        result = {
                lastTime,
                static_cast<float>(round(pm25Summ/counter*10)/10),
                static_cast<float>(round(pm10Summ/counter*10)/10),
                static_cast<float>(round(tempSumm/counter*10)/10),
                static_cast<float>(round(humidSumm/counter*10)/10)
        };
    } else {
        result = nullMeasure;
    }

    if(DEBUG){
        Serial.println();
        Serial.print("There were ");
        Serial.print(counter);
        Serial.println(" elements averaged");
        Serial.print("Averaged measure: ");
        Serial.println(measureToString(result));
        Serial.println();
    }
    return result;
}

void logAverage(const Measure &measure) {
    Serial.print("[");
    Serial.print(getTimeString(measure.measureTime));
    Serial.print(" (");
    Serial.print(measure.pm25);
    Serial.print(", ");
    Serial.print(measure.pm10);
    Serial.print(", ");
    Serial.print(measure.temp);
    Serial.print("c, ");
    Serial.print(measure.humid);
    Serial.print("%)], ");
}

void connectToWifi(String ssid, String passPhrase, int maxRetry) {
    WiFi.begin(ssid, passPhrase);   //WiFi connection
    int retries = 0;
    while (WiFi.status() != WL_CONNECTED && retries < maxRetry) {  //Wait for the WiFI connection completion
        delay(500);
        Serial.println("Waiting for connection");
        retries++;
    }
}

void setup() {
    Serial.begin(115200);
    sds.begin(9600);

    // Initialize device.
    dht.begin();
    Serial.println("DHTxx Unified Sensor Example");
    // Print temperature sensor details.
    sensor_t sensor;
    dht.temperature().getSensor(&sensor);
    Serial.println("------------------------------------");
    Serial.println("Temperature");
    Serial.print  ("Sensor:       "); Serial.println(sensor.name);
    Serial.print  ("Driver Ver:   "); Serial.println(sensor.version);
    Serial.print  ("Unique ID:    "); Serial.println(sensor.sensor_id);
    Serial.print  ("Max Value:    "); Serial.print(sensor.max_value); Serial.println(" *C");
    Serial.print  ("Min Value:    "); Serial.print(sensor.min_value); Serial.println(" *C");
    Serial.print  ("Resolution:   "); Serial.print(sensor.resolution); Serial.println(" *C");
    Serial.println("------------------------------------");
    // Print humidity sensor details.
    dht.humidity().getSensor(&sensor);
    Serial.println("------------------------------------");
    Serial.println("Humidity");
    Serial.print  ("Sensor:       "); Serial.println(sensor.name);
    Serial.print  ("Driver Ver:   "); Serial.println(sensor.version);
    Serial.print  ("Unique ID:    "); Serial.println(sensor.sensor_id);
    Serial.print  ("Max Value:    "); Serial.print(sensor.max_value); Serial.println("%");
    Serial.print  ("Min Value:    "); Serial.print(sensor.min_value); Serial.println("%");
    Serial.print  ("Resolution:   "); Serial.print(sensor.resolution); Serial.println("%");
    Serial.println("------------------------------------");


    if(SLEEPING_PERIOD > 0) {
        sds.setQueryReportingMode();
        sds.setCustomWorkingPeriod(0);
        sds.wakeup();
    } else {
        sds.setActiveReportingMode();
        WorkingStateResult status = sds.wakeup();
        if(!status.isWorking()){
            sds.wakeup();
        }

    }

    //SET TIME
    //rtc.writeProtect(false);
    //rtc.halt(false);
//    Time t1(2020, 6, 8, 19, 16, 00, Time::kMonday);
//    rtc.time(t1);

    Serial.println(sds.queryFirmwareVersion().toString()); // prints firmware version
    Serial.println(sds.setQueryReportingMode().toString()); // ensures sensor is in 'query' reporting mode

    const char *ssid = "ALEKSNET";
    connectToWifi(ssid, "ekvatorthebest", 20);
    if(WiFi.status() != WL_CONNECTED){
        ssid = "ALEKSNET-ROOF";
        connectToWifi(ssid, "ekvatorthebest", 20);
    }
    if(WiFi.status() != WL_CONNECTED){
        ssid = "ALEKSNET2";
        connectToWifi(ssid, "ekvatorthebest", 20);
    }

    Serial.println(""); Serial.print("Connected to "); Serial.println(ssid); Serial.print("IP address: "); Serial.println(WiFi.localIP());

    if (mdns.begin("esp8266", WiFi.localIP())) {
        Serial.println("MDNS responder started");
    }

    server.on("/1", []() {
        server.setContentLength(CONTENT_LENGTH_UNKNOWN);
        server.send(200, "text/html", "");
        do {
            String content = measuresToString(true, everyMeasures, EVERY_MEASURES_NUMBER);
            server.sendContent(content);
        } while(thereIsMore);
        server.sendContent("");
        server.client().stop();
    });

    server.on("/15", []() {
        server.setContentLength(CONTENT_LENGTH_UNKNOWN);
        server.send(200, "text/html", "");
        do {
            String content = measuresToString(true, every15minutesMeasures, EVERY_15_MINUTES_MEASURES_NUMBER);
            server.sendContent(content);
        } while(thereIsMore);
        server.sendContent("");
        server.client().stop();
    });

    server.on("/60", []() {
        server.setContentLength(CONTENT_LENGTH_UNKNOWN);
        server.send(200, "text/html", "");
        do {
            String content = measuresToString(true, everyHourMeasures, EVERY_HOUR_MEASURES_NUMBER);
            server.sendContent(content);
        } while(thereIsMore);
        server.sendContent("");
        server.client().stop();
    });

    server.begin();

    Time t = rtc.time();
    time_t currTimeT = makeTime({t.sec, t.min, t.hr, 1, t.date, t.mon, static_cast<uint8_t>(t.yr - 1970)});
    every15minuteTimer = currTimeT;
    everyHourTimer = currTimeT;

    for (auto &reading : everyMeasures) {
        reading = nullMeasure;
    }
    for (auto &reading : every15minutesMeasures) {
        reading = nullMeasure;
    }
    for (auto &reading : everyHourMeasures) {
        reading = nullMeasure;
    }
    delay(500);
    if (DEBUG) {
        Serial.print(getTimeString(currTimeT)); Serial.println(" - The sensor should be woken now");Serial.print("WORKING_PERIOD is ");Serial.println(WORKING_PERIOD);Serial.print("SLEEPING_PERIOD is ");Serial.println(SLEEPING_PERIOD); }
}

void loop() {
    server.handleClient();
    if (millis() - currentTimeMillis > WORKING_PERIOD && step == 1) {

        sensors_event_t tempEvent;
        sensors_event_t humidEvent;
        dht.temperature().getEvent(&tempEvent);
        if (DEBUG && !isnan(tempEvent.temperature)) {
            Serial.print("Temperature: ");
            Serial.print(tempEvent.temperature);
            Serial.println(" *C");
        }
        // Get humidity tempEvent and print its value.
        dht.humidity().getEvent(&humidEvent);
        if (DEBUG && !isnan(humidEvent.relative_humidity)) {
            Serial.print("Humidity: ");
            Serial.print(humidEvent.relative_humidity);
            Serial.println("%");
        }

        PmResult pm = sds.queryPm();

        if (pm.isOk()) {
            Time t = rtc.time();
            time_t currentTime = makeTime({t.sec, t.min, t.hr, 1, t.date, t.mon, static_cast<uint8_t>(t.yr-1970)});
            Measure currentMeasure = {
                    currentTime,
                    static_cast<float>(round(pm.pm25*10)/10),
                    static_cast<float>(round(pm.pm10*10)/10),
                    tempEvent.temperature,
                    humidEvent.relative_humidity
            };

            if (DEBUG) {
                Serial.print(++totalCounter); Serial.print(". Got a measure: "); printMeasure(currentMeasure); }

            putEveryMeasure(currentMeasure);

            if (fifteenMinutesElapsed(currentTime, every15minuteTimer)) {
                every15minuteTimer = currentTime;
                putEvery15MinuteMeasure(calculate15minuteAverage(currentTime));
            }

            if (anHourElapsed(currentTime, everyHourTimer)) {
                everyHourTimer = currentTime;
                putEveryHourMeasure(calculate1HourAverage(currentTime));
            }

        } else {
            Serial.print("Could not read values from sensor, reason: ");
            Serial.println(pm.statusToString());
        }
        currentTimeMillis = millis();
        if(SLEEPING_PERIOD > 0){
            WorkingStateResult state = sds.sleep();
            if (state.isWorking()) {
                Serial.println("Problem with sleeping the sensor.");
            }
            step = 2;
        }
    }
    if (SLEEPING_PERIOD > 0 && millis() - currentTimeMillis > SLEEPING_PERIOD && step == 2) {
        currentTimeMillis = millis();
        step = 1;
        sds.wakeup();
    }
}
