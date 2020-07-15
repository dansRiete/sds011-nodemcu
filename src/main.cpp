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
#define SLEEPING_PERIOD 55*1000
#define HTTP_RESPONSE_CHUNKS_SIZE 200
//#define SLEEPING_PERIOD 0
const boolean DEBUG = true;
#define DHTPIN 0
#define DHTTYPE           DHT11
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
    float rh;
};

Measure nullMeasure = {0, -1, -1, -1, -1};

#define EVERY_MEASURES_NUMBER 900
struct Measure everyMeasures[EVERY_MEASURES_NUMBER];
int everyMeasureIndex = -1;
boolean everyMeasureFirstPass = true;

#define EVERY_15_MINUTES_MEASURES_NUMBER 4*24
struct Measure every15minutesMeasures[EVERY_15_MINUTES_MEASURES_NUMBER];
int every15MinutesMeasureIndex = -1;
boolean every15MinutesMeasureFirstPass = true;
int last15MinuteAverageMinute = -1;

#define EVERY_HOUR_MEASURES_NUMBER 3*24
struct Measure everyHourMeasures[EVERY_HOUR_MEASURES_NUMBER];
int everyHourMeasureIndex = -1;
boolean everyHourMeasureFirstPass = true;
int last1HourAverageMinute = -1;

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

float calculateAbsoluteHumidity(float temp, float rh) {
    return 6.112 * pow(2.71828, 17.67 * temp / (243.5 + temp)) * rh * 2.1674 / (273.15 + temp);
}

String measureToString(Measure measure) {
    char measureString[90];
    snprintf(measureString, 90, "%02d/%02d/%d %02d:%02d:%02d - PM2.5 = %.1f, PM10 = %.1f, temp = %.1fC, RH = %.0f%%, AH = %.1fg/m3",
             day(measure.measureTime),
             month(measure.measureTime),
             year(measure.measureTime),
             hour(measure.measureTime),
             minute(measure.measureTime),
             second(measure.measureTime),
             measure.pm25,
             measure.pm10,
             measure.temp,
             measure.rh,
             measure.temp == -1 || measure.rh == -1 ? -1 : calculateAbsoluteHumidity(measure.temp, measure.rh)
    );
    return String(measureString);
}

String timeToString(time_t t){
    char measureString[20];
    snprintf(measureString, 60, "%02d/%02d/%d-%02d:%02d:%02d",        //todo add handling weekdays
             day(t),
             month(t),
             year(t),
             hour(t),
             minute(t),
             second(t));
    return String(measureString);
}

String measureToCsvString(Measure measure) {
    char measureString[60];
    snprintf(measureString, 60, "%02d/%02d/%d-%02d:%02d:%02d, %.1f, %.1f, %.1f, %.0f, %.1f\n",
             day(measure.measureTime),
             month(measure.measureTime),
             year(measure.measureTime),
             hour(measure.measureTime),
             minute(measure.measureTime),
             second(measure.measureTime),
             measure.pm25,
             measure.pm10,
             measure.temp,
             measure.rh,
             measure.temp == -1 || measure.rh == -1 ? -1 : calculateAbsoluteHumidity(measure.temp, measure.rh)
    );
    return String(measureString);
}

String measuresToString(boolean html, Measure measuresToPrint[], int length) {
    String measuresString = "";
    int i1 = 0;
    for (int i = thereIsMore ? thereIsMoreCounter : length - 1; i >= 0; i--) {
        if (i1 > HTTP_RESPONSE_CHUNKS_SIZE) {
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
    thereIsMore = i1 > HTTP_RESPONSE_CHUNKS_SIZE;
    return measuresString;
}

String measuresToCsv(Measure measuresToPrint[], int length) {
    String measuresString = thereIsMore  ? "" : "date, pm2.5, pm10, temp, RH, AH\n";
    int i1 = 0;
    for (int i = thereIsMore ? thereIsMoreCounter : length - 1; i >= 0; i--) {
        if (i1 > HTTP_RESPONSE_CHUNKS_SIZE) {
            thereIsMoreCounter = i;
            break;
        }
        Measure measure = measuresToPrint[i];
        time_t currTime = measure.measureTime;
        if (currTime != 0) {
            measuresString += measureToCsvString(measure);
            i1++;
        }
    }
    thereIsMore = i1 > HTTP_RESPONSE_CHUNKS_SIZE;
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

bool isInIntervalOfSeconds(time_t currTime, time_t comparedTime, long seconds) {
    if (comparedTime == 0) {
        return false;
    }
    return (long) currTime - (long) comparedTime <= seconds;
}

Measure calculateMinuteAverage(time_t currentTime, int seconds, Measure measuresSource[]) {
    if (DEBUG) {
        Serial.println();
        Serial.print(getTimeString(currentTime));
        Serial.print(" - Calculating ");
        Serial.print(seconds);
        Serial.print(" seconds average from next values:");
        Serial.println();
    }
    float pm25Summ = 0, pm10Summ = 0, tempSumm = 0, rhSumm = 0;
    int pm25Counter = 0, pm10Counter = 0, tempCounter = 0, rhCounter = 0;
    for (int i = 0; i < EVERY_MEASURES_NUMBER; i++) {
        Measure measure = measuresSource[i];
        if (isInIntervalOfSeconds(currentTime, measure.measureTime, seconds)) {
            if (measure.pm25 != -1) {
//            if(DEBUG){ logAverage(measure); }
                pm25Summ += measure.pm25;
                pm25Counter++;
            }
            if (measure.pm10 != -1) {
//            if(DEBUG){ logAverage(measure); }
                pm10Summ += measure.pm10;
                pm10Counter++;
            }
            if (measure.temp != -1) {
                if(DEBUG){ logAverage(measure); }
                tempSumm += measure.temp;
                tempCounter++;
            }
            if (measure.rh != -1) {
//            if(DEBUG){ logAverage(measure); }
                rhSumm += measure.rh;
                rhCounter++;
            }

            /*if (b && seconds == 15) {
                totalAveragedCounter++;
            }*/
        }
    }
    Measure result = {
            currentTime,
            pm25Counter == 0 ? -1 : static_cast<float>(round(pm25Summ / pm25Counter * 10) / 10),
            pm10Counter == 0 ? -1 : static_cast<float>(round(pm10Summ / pm10Counter * 10) / 10),
            tempCounter == 0 ? -1 : static_cast<float>(round(tempSumm / tempCounter * 10) / 10),
            rhCounter == 0 ? -1 : static_cast<float>(round(rhSumm / rhCounter * 10) / 10)
    };

    if (DEBUG) {
        Serial.println();
        Serial.print("There were ");
        Serial.print(pm25Counter);
        Serial.print(" elements averaged.");
        Serial.print(" Total averaged: ");
        Serial.println(totalAveragedCounter);
        Serial.print("Averaged measure: ");
        Serial.println(measureToString(result));
        Serial.println();
    }

    return result;
}

void logAverage(const Measure &measure) {
    Serial.print("\t");
    Serial.print(getTimeString(measure.measureTime));
    Serial.print(" - PM2.5 = " );
    Serial.print(measure.pm25);
    Serial.print(", PM10 = ");
    Serial.print(measure.pm10);
    Serial.print(", temp = ");
    Serial.print(measure.temp);
    Serial.print("C, rh = ");
    Serial.print(measure.rh);
    Serial.println("%");
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

time_t rtcTime() {
    Time t = rtc.time();
    return makeTime({t.sec, t.min, t.hr, 1, t.date, t.mon, static_cast<uint8_t>(t.yr - 1970)});
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
//    Time t1(2020, 12, 31, 23, 56, 00, Time::kMonday);
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

    Serial.println(""); Serial.print("Connected to "); Serial.println(ssid);
    Serial.print("IP address: "); Serial.println(WiFi.localIP());

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

    server.on("/csv/1", []() {
        server.setContentLength(CONTENT_LENGTH_UNKNOWN);
        server.send(200, "text/csv", "");
        do {
            String content = measuresToCsv(everyMeasures, EVERY_MEASURES_NUMBER);
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

    server.on("/csv/15", []() {
        server.setContentLength(CONTENT_LENGTH_UNKNOWN);
        server.send(200, "text/csv", "");
        do {
            String content = measuresToCsv(every15minutesMeasures, EVERY_15_MINUTES_MEASURES_NUMBER);
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

    server.on("/csv/60", []() {
        server.setContentLength(CONTENT_LENGTH_UNKNOWN);
        server.send(200, "text/csv", "");
        do {
            String content = measuresToCsv(everyHourMeasures, EVERY_HOUR_MEASURES_NUMBER);
            server.sendContent(content);
        } while(thereIsMore);
        server.sendContent("");
        server.client().stop();
    });

    server.on("/setTime", HTTP_POST, []() {
        String message = "";
        int year = server.arg("year").toInt();
        int month = server.arg("month").toInt();
        int day = server.arg("day").toInt();
        int hour = server.arg("hour").toInt();
        int minute = server.arg("minute").toInt();
        int second = server.arg("second").toInt();
        //todo add handling weekdays
        Time t1(year, month, day, hour, minute, second, Time::kMonday);
        rtc.time(t1);
        server.send(200, "text/plain", timeToString(rtcTime()));
    });

    server.begin();

    for (auto &reading : everyMeasures) {
        reading = nullMeasure;
    }
    for (auto &reading : every15minutesMeasures) {
        reading = nullMeasure;
    }
    for (auto &reading : everyHourMeasures) {
        reading = nullMeasure;
    }
    if (DEBUG) {
        Serial.print(getTimeString(rtcTime())); Serial.println(" - The sensor should be woken now");
        Serial.print("WORKING_PERIOD is ");Serial.println(WORKING_PERIOD);Serial.print("SLEEPING_PERIOD is ");
        Serial.println(SLEEPING_PERIOD);
    }
}

void loop() {

    delay(20);

    server.handleClient();
    time_t currentTime = rtcTime();
    long currentTimestamp = (long) currentTime;

    if (currentTimestamp % (3 * 60) == 0 && last15MinuteAverageMinute != minute(currentTime)) {
        last15MinuteAverageMinute = minute(currentTime);
        putEvery15MinuteMeasure(calculateMinuteAverage(currentTime, 3 * 60, everyMeasures));
    }

    if (currentTimestamp % (15 * 60) == 0 && last1HourAverageMinute != minute(currentTime)) {
        last1HourAverageMinute = minute(currentTime);
        putEveryHourMeasure(calculateMinuteAverage(currentTime, 15 * 60, every15minutesMeasures));
    }

    //  Step 1
    if (millis() - currentTimeMillis > WORKING_PERIOD && step == 1) {
        currentTimeMillis = millis();

        sensors_event_t tempEvent;
        sensors_event_t humidEvent;
        dht.temperature().getEvent(&tempEvent);
        dht.humidity().getEvent(&humidEvent);

        PmResult pm = sds.queryPm();
        float pm25 = -1;
        float pm10 = -1;
        if (pm.isOk()) {
            pm25 = round(pm.pm25*10)/10;
            pm10 = round(pm.pm10*10)/10;
        }

        Measure currentMeasure = {
                currentTime,
                pm25,
                pm10,
                isnan(tempEvent.temperature) ? -1 : tempEvent.temperature,
                isnan(humidEvent.relative_humidity) ? -1 : humidEvent.relative_humidity
        };

        if (DEBUG) {
            Serial.print(++totalCounter); Serial.print(". Got a measure: "); printMeasure(currentMeasure); }

        putEveryMeasure(currentMeasure);


        if (SLEEPING_PERIOD > 0) {
            WorkingStateResult state = sds.sleep();
            if (state.isWorking()) {
                Serial.println("Problem with sleeping the sensor.");
            }
            step = 2;
        }
    }

    //  Step 2
    if (SLEEPING_PERIOD > 0 && millis() - currentTimeMillis > SLEEPING_PERIOD && step == 2) {
        currentTimeMillis = millis();
        step = 1;
        sds.wakeup();
    }
}
