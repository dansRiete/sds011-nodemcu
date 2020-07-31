#include <../lib/Time-master/TimeLib.h>
#include <math.h>
#include <Arduino.h>
#include "../lib/Nova_Fitness_Sds_dust_sensors_library/src/SdsDustSensor.h"
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <../lib/Adafruit_Sensor-master/Adafruit_Sensor.h>
#include <../lib/DHT-sensor-library-master/DHT.h>
#include <../lib/DHT-sensor-library-master/DHT_U.h>
#include <ESP8266HTTPClient.h>

#define WIFI_MAX_RETRIES 40
#define DEFAULT_MEASURING_DURATION 5*1000
#define DEFAULT_SLEEPING_PERIOD 55*1000
#define MINUTE_AVERAGE_PERIOD 10
#define HOUR_AVERAGE_PERIOD 1
int logCounter = 0;
int maxMeasuringTime = 0;
unsigned measuringDuration;
unsigned sleepingPeriod;
byte period15m;
byte period1h;
#define HTTP_RESPONSE_CHUNKS_SIZE 20
#define MAX_MEASURES_STRING_LENGTH 150
#define NULL_MEASURE_VALUE -100
const boolean DEBUG = false;
const boolean DEBUG_CASE2 = true;
#define DHT21_ROOF_PIN 12
#define DHT21_WINDOW_PIN 4
#define DHT22_LIVING_ROOM_PIN 13
const byte SENSOR_RX_PIN = 2;
const byte SENSOR_DX_PIN = 0;
unsigned long int currentTimeMillisTimer = 0;
byte step = 1;
enum ContentType {HTML, CSV, TEXT};
MDNSResponder mdns;
ESP8266WebServer server(80);
SdsDustSensor sds(SENSOR_RX_PIN, SENSOR_DX_PIN);
DHT_Unified dht21Roof(DHT21_ROOF_PIN, DHT21);
DHT_Unified dht21Window(DHT21_WINDOW_PIN, DHT21);
DHT_Unified dht22LivingRoom(DHT22_LIVING_ROOM_PIN, DHT22);
const char csvHeader[] = "date, pm2.5, pm10, inTemp, inRH, inAH, outTemp, outRH, outAH\n";

struct Measure {
    time_t measureTime;
    float pm25;
    float pm10;
    float outTemp;
    float outRh;
    float inTemp;
    float inRh;
    boolean window;
    char serviceInfo[20];
};

const Measure nullMeasure = {0, NULL_MEASURE_VALUE, NULL_MEASURE_VALUE, NULL_MEASURE_VALUE, NULL_MEASURE_VALUE,
                       NULL_MEASURE_VALUE, NULL_MEASURE_VALUE, false, "null"};

#define EVERY_MEASURES_NUMBER 180
struct Measure everyMeasures[EVERY_MEASURES_NUMBER];
int everyMeasureIndex = -1;
boolean everyMeasureFirstPass = true;

#define EVERY_15_MINUTES_MEASURES_NUMBER 4*24
struct Measure every15minutesMeasures[EVERY_15_MINUTES_MEASURES_NUMBER];
int every15MinutesMeasureIndex = -1;
boolean every15MinutesMeasureFirstPass = true;
byte last15MinuteAverageMinute = NULL_MEASURE_VALUE;

#define EVERY_HOUR_MEASURES_NUMBER 5*24
struct Measure everyHourMeasures[EVERY_HOUR_MEASURES_NUMBER];
int everyHourMeasureIndex = -1;
boolean everyHourMeasureFirstPass = true;
byte last1HourAverageHour = NULL_MEASURE_VALUE;
byte lastLogMinute = NULL_MEASURE_VALUE;

int numberOfEveryMsrPlaced = 0;
int numberOf15mMsrPlaced = 0;
int numberOf1hMsrPlaced = 0;
int numberOf15mAveraged = 0;
int numberOf1hAveraged = 0;

void resetTimer() {
    currentTimeMillisTimer = millis();
}

char* getTimeString(time_t time) {
    static char measureString[10];
    snprintf(measureString, 10, "%02d:%02d:%02d", hour(time), minute(time), second(time));
    return measureString;
}

float calculateAbsoluteHumidity(float temp, float rh) {
    return 6.112 * pow(2.71828, 17.67 * temp / (243.5 + temp)) * rh * 2.1674 / (273.15 + temp);
}

char* measureToString(Measure measure) {
    static char measureString[MAX_MEASURES_STRING_LENGTH];
    snprintf(measureString, MAX_MEASURES_STRING_LENGTH, "%02d/%02d/%d %02d:%02d:%02d - PM2.5 = %.1f, PM10 = %.1f, IN[%.1fC-%.0f%%-%.1fg/m3], OUT[%.1fC%s-%.0f%%-%.1fg/m3]",
             day(measure.measureTime),
             month(measure.measureTime),
             year(measure.measureTime),
             hour(measure.measureTime),
             minute(measure.measureTime),
             second(measure.measureTime),
             measure.pm25,
             measure.pm10,
             measure.inTemp,
             measure.inRh,
             measure.inTemp == NULL_MEASURE_VALUE || measure.inRh == NULL_MEASURE_VALUE ? NULL_MEASURE_VALUE : calculateAbsoluteHumidity(measure.inTemp, measure.inRh),
             measure.outTemp,
             measure.serviceInfo,
             measure.outRh,
             measure.outTemp == NULL_MEASURE_VALUE || measure.outRh == NULL_MEASURE_VALUE ? NULL_MEASURE_VALUE : calculateAbsoluteHumidity(measure.outTemp, measure.outRh)
    );
    return measureString;
}

char* timeToString(time_t t){
    static char measureString[20];
    snprintf(measureString, 60, "%02d/%02d/%d-%02d:%02d:%02d",        //todo add handling weekdays
             day(t),
             month(t),
             year(t),
             hour(t),
             minute(t),
             second(t));
    return measureString;
}

char* measureToCsvString(Measure measure) {
    static char measureString[100];
    snprintf(measureString, 100, "%02d/%02d/%d-%02d:%02d:%02d, %.1f, %.1f, %.1f, %.0f, %.1f, %.1f, %.0f, %.1f\n",
             day(measure.measureTime),
             month(measure.measureTime),
             year(measure.measureTime),
             hour(measure.measureTime),
             minute(measure.measureTime),
             second(measure.measureTime),
             measure.pm25,
             measure.pm10,
             measure.inTemp,
             measure.inRh,
             measure.inTemp == NULL_MEASURE_VALUE || measure.outRh == NULL_MEASURE_VALUE ? NULL_MEASURE_VALUE : calculateAbsoluteHumidity(measure.outTemp, measure.outRh),
             measure.outTemp,
             measure.outRh,
             measure.outTemp == NULL_MEASURE_VALUE || measure.outRh == NULL_MEASURE_VALUE ? NULL_MEASURE_VALUE : calculateAbsoluteHumidity(measure.outTemp, measure.outRh)
    );
    return measureString;
}

void printMeasure(Measure measure) {
    Serial.println(measureToString(measure));
}

boolean isNullMeasure(const Measure& measure){
    return measure.pm10 == NULL_MEASURE_VALUE && measure.pm25 == NULL_MEASURE_VALUE && measure.pm10 == NULL_MEASURE_VALUE
           && measure.outTemp == NULL_MEASURE_VALUE && measure.outRh == NULL_MEASURE_VALUE
           && measure.inTemp == NULL_MEASURE_VALUE && measure.inRh == NULL_MEASURE_VALUE;
}

void putEveryMeasure(const Measure& measure){
    if (isNullMeasure(measure)) {
        return;
    }
    numberOfEveryMsrPlaced++;
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

void putEvery15MinuteMeasure(const Measure& measure){
    if (isNullMeasure(measure)) {
        return;
    }
    numberOf15mMsrPlaced++;
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

void putEveryHourMeasure(const Measure& measure){
    if (isNullMeasure(measure)) {
        return;
    }
    numberOf1hMsrPlaced++;
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

bool isInIntervalOfSeconds(time_t currentTime, time_t measureTime, long intervalSec) {
    if (measureTime == 0) {
        /*if (DEBUG_CASE2) {
            Serial.print("measureTime is 0, ");
        }*/
        return false;
    }
    long currTimeTS = (long) currentTime;
    long measureTs = (long) measureTime;
    /*if (DEBUG_CASE2) {
        Serial.print("(currTime:");
        Serial.print(currTimeTS);
        Serial.print("-measureTime:");
        Serial.print(measureTs);
        Serial.print("-intervalSec:");
        Serial.print(intervalSec);
        Serial.print("), ");
    }*/
    return currTimeTS - measureTs < intervalSec;
}

void logAverage(const Measure &measure) {
    Serial.print("\t");
    Serial.print(getTimeString(measure.measureTime));
    Serial.print(" - PM2.5 = " );
    Serial.print(measure.pm25);
    Serial.print(", PM10 = ");
    Serial.print(measure.pm10);
    Serial.print(", outTemp = ");
    Serial.print(measure.outTemp);
    Serial.print("C, outRh = ");
    Serial.print(measure.outRh);
    Serial.print("%");
    Serial.print(", inTemp = ");
    Serial.print(measure.inTemp);
    Serial.print("C, inRh = ");
    Serial.print(measure.inRh);
    Serial.print("%");
    Serial.print("(");
    Serial.print(measure.window ? "W" : "R");
    Serial.println(")");
}

Measure calculateAverage(time_t currentTime, int seconds, Measure* measuresSource, int measurementArraySize) {
    if (DEBUG_CASE2) {
        Serial.println();
        Serial.print(getTimeString(currentTime));
        Serial.print(" - Calculating ");
        Serial.print(seconds);
        Serial.print(" seconds average from next values:");
        Serial.println();
    }
    float pm25Sum = 0, pm10Sum = 0, outTempSum = 0, outRhSum = 0, inTempSum = 0, inRhSum = 0;
    int pm25Counter = 0, pm10Counter = 0, outTempCounter = 0, outRhCounter = 0, inTempCounter = 0, inRhCounter = 0,
    windowCounter = 0, measurementsCounter = 0;
    for (int i = 0; i < measurementArraySize; i++) {
        Measure measure = measuresSource[i];
        if (isInIntervalOfSeconds(currentTime, measure.measureTime, seconds)) {
            measurementsCounter++;
            if(DEBUG_CASE2){ logAverage(measure); }
            if (measure.pm25 != NULL_MEASURE_VALUE) {
                pm25Sum += measure.pm25;
                pm25Counter++;
            }
            if (measure.pm10 != NULL_MEASURE_VALUE) {
                pm10Sum += measure.pm10;
                pm10Counter++;
            }
            if (measure.outTemp != NULL_MEASURE_VALUE) {
                outTempSum += measure.outTemp;
                outTempCounter++;
                if (measure.window) {
                    windowCounter++;
                }
            }
            if (measure.outRh != NULL_MEASURE_VALUE) {
                outRhSum += measure.outRh;
                outRhCounter++;
            }
            if (measure.inTemp != NULL_MEASURE_VALUE) {
                inTempSum += measure.inTemp;
                inTempCounter++;
            }
            if (measure.inRh != NULL_MEASURE_VALUE) {
                inRhSum += measure.inRh;
                inRhCounter++;
            }
        }
    }
    if (DEBUG_CASE2) {
        Serial.println();
    }
    bool window = outTempCounter <= 0 ? true : ((float) windowCounter / (float) outTempCounter > 0.5);
    Measure result = {
            currentTime,
            pm25Counter == 0 ? NULL_MEASURE_VALUE : static_cast<float>(round(pm25Sum / pm25Counter * 10) / 10),
            pm10Counter == 0 ? NULL_MEASURE_VALUE : static_cast<float>(round(pm10Sum / pm10Counter * 10) / 10),
            outTempCounter == 0 ? NULL_MEASURE_VALUE : static_cast<float>(round(outTempSum / outTempCounter * 10) / 10),
            outRhCounter == 0 ? NULL_MEASURE_VALUE : static_cast<float>(round(outRhSum / outRhCounter * 10) / 10),
            inTempCounter == 0 ? NULL_MEASURE_VALUE : static_cast<float>(round(inTempSum / inTempCounter * 10) / 10),
            inRhCounter == 0 ? NULL_MEASURE_VALUE : static_cast<float>(round(inRhSum / inRhCounter * 10) / 10),
            window
    };

    if (window) {
        strcpy(result.serviceInfo, "(W)");
    } else {
        strcpy(result.serviceInfo, "(R)");
    }

    if (seconds == period15m * 60) {
        numberOf15mAveraged += measurementsCounter;
    } else {
        numberOf1hAveraged += measurementsCounter;
    }

    if (DEBUG_CASE2) {
        Serial.print("There were ");
        Serial.print(measurementsCounter);
        Serial.print(" elements averaged. Total averaged: ");
        Serial.print(seconds == period15m * 60 ? numberOf15mAveraged : numberOf1hAveraged);
        Serial.print(". Total placed: "); Serial.println(seconds == period15m * 60 ? numberOf15mMsrPlaced : numberOf1hMsrPlaced);
        if (!isNullMeasure(result)) {
            Serial.print("Averaged measure: ");
            Serial.println(measureToString(result));
        }
        Serial.println();
    }

    return result;
}

void connectToWifi() {
    String passPhrase = "ekvatorthebest";
    WiFi.begin("ALEKSNET-ROOF", passPhrase);
    int retries = 0;
    while (WiFi.status() != WL_CONNECTED && retries < WIFI_MAX_RETRIES) {
        delay(500);
        Serial.print("Waiting for connection to ");
        Serial.println("ALEKSNET-ROOF");
        retries++;
    }
    if (WiFi.status() != WL_CONNECTED) {
        retries = 0;
        WiFi.begin("ALEKSNET", passPhrase);
        while (WiFi.status() != WL_CONNECTED && retries < WIFI_MAX_RETRIES) {
            delay(500);
            Serial.print("Waiting for connection to ");
            Serial.println("ALEKSNET");
            retries++;
        }
    }
    if (WiFi.status() != WL_CONNECTED) {
        retries = 0;
        WiFi.begin("ALEKSNET2", passPhrase);
        while (WiFi.status() != WL_CONNECTED && retries < WIFI_MAX_RETRIES) {
            delay(500);
            Serial.print("Waiting for connection to ");
            Serial.println("ALEKSNET2");
            retries++;
        }
    }
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("Could not connect to any WiFi");
    }
}

time_t rtcTime() {
//    Time t = rtc.time();
//    return makeTime({t.sec, t.min, t.hr, 1, t.date, t.mon, static_cast<uint8_t>(t.yr - 1970)});
    return now();
}

void syncTime() {
    HTTPClient http;
    http.begin("http://worldtimeapi.org/api/timezone/Europe/Kiev.txt");
    int httpCode = http.GET();
    if (httpCode > 0) {
        String payload = http.getString();
        std::string timePayload = payload.c_str();
        unsigned int timePosition = timePayload.find("unixtime: ");
        std::string unixTime = timePayload.substr(timePosition + 10, timePosition + 10 + 10);
        String a = unixTime.c_str();
        Serial.println(payload);
        setTime((time_t)a.toInt() + 3 * 3600);
    }
    Serial.print("Time synced. Current time is ");
    Serial.println(timeToString(rtcTime()));
    http.end();   //Close connection
}

void sendChunkedContent(Measure *measures, int measuresSize, ContentType contentType) {
    static const char br[5] = "<br>";
    static char cont[MAX_MEASURES_STRING_LENGTH * HTTP_RESPONSE_CHUNKS_SIZE] = "";

    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server.send(200, "text/html", "");
    int currentMeasureIndex = measuresSize - 1;
    boolean anyDataHasBeenSent = false;

    do {
        strcpy(cont, contentType == CSV && !anyDataHasBeenSent ? csvHeader : "");
        boolean thereIsData = false;
        for (int i = 0 ; i < HTTP_RESPONSE_CHUNKS_SIZE && currentMeasureIndex >= 0; i++) {
            if(!isNullMeasure(measures[currentMeasureIndex])){
                thereIsData = anyDataHasBeenSent = true;
                strcat(
                        cont, contentType == CSV ? measureToCsvString(measures[currentMeasureIndex]) :
                        measureToString(measures[currentMeasureIndex])
                );
                if (contentType == HTML) {
                    strcat(cont, br);
                }
            }
            currentMeasureIndex--;
        }
        if (thereIsData) {
            server.sendContent(cont);
        }
    } while (currentMeasureIndex > 0);

    server.sendContent("");
    server.client().stop();
}

void configureHttpServer() {
    server.on("/1", []() {
        sendChunkedContent(everyMeasures, EVERY_MEASURES_NUMBER, HTML);
    });

    server.on("/csv/1", []() {
        sendChunkedContent(everyMeasures, EVERY_MEASURES_NUMBER, CSV);
    });

    server.on("/15", []() {
        sendChunkedContent(every15minutesMeasures, EVERY_15_MINUTES_MEASURES_NUMBER, HTML);
    });

    server.on("/csv/15", []() {
        sendChunkedContent(every15minutesMeasures, EVERY_15_MINUTES_MEASURES_NUMBER, CSV);

    });

    server.on("/60", []() {
        sendChunkedContent(everyHourMeasures, EVERY_HOUR_MEASURES_NUMBER, HTML);
    });

    server.on("/csv/60", []() {
        sendChunkedContent(everyHourMeasures, EVERY_HOUR_MEASURES_NUMBER, CSV);
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
        setTime(hour, minute, second, day, month, year);
        resetTimer();
        last1HourAverageHour = hour;
        last15MinuteAverageMinute = minute;
        server.send(200, "text/plain", timeToString(rtcTime()));
    });

    server.on("/config", HTTP_POST, []() {
        char parameters[200];
        measuringDuration = server.arg("measuringDuration").toInt();
        sleepingPeriod = server.arg("sleepingPeriod").toInt();
        if (sleepingPeriod < measuringDuration) {
            sleepingPeriod = 0;
        }
        period15m = server.arg("15avgPeriod").toInt();
        period1h = server.arg("1hPeriod").toInt();
        snprintf(parameters, 200, "measuringDuration = %d, sleepingPeriod = %d, 15avgPeriod = %d, 1hPeriod = %d",
                 measuringDuration, sleepingPeriod, period15m, period1h);
        sds.wakeup();
        step = 1;
        resetTimer();
        server.send(200, "text/plain", String(parameters));
    });

    server.on("/config", HTTP_GET, []() {
        char parameters[250];
        snprintf(parameters, 250, "measuringDuration = %d, sleepingPeriod = %d, 15avgPeriod = %d, 1hPeriod = %d, currentTime = %s",
                 measuringDuration, sleepingPeriod, period15m, period1h, timeToString(rtcTime()));
        server.send(200, "text/plain", String(parameters));
    });

    server.begin();
}

void setup() {

    delay(500);

    Serial.begin(115200);
    sds.begin(9600);

    dht21Roof.begin();
    dht21Window.begin();
    dht22LivingRoom.begin();
    Serial.println("DHTxx Unified Sensor Example");
    // Print temperature sensor details.
    sensor_t sensor;
    dht21Roof.temperature().getSensor(&sensor);
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
    dht21Roof.humidity().getSensor(&sensor);
    Serial.println("------------------------------------");
    Serial.println("Humidity");
    Serial.print  ("Sensor:       "); Serial.println(sensor.name);
    Serial.print  ("Driver Ver:   "); Serial.println(sensor.version);
    Serial.print  ("Unique ID:    "); Serial.println(sensor.sensor_id);
    Serial.print  ("Max Value:    "); Serial.print(sensor.max_value); Serial.println("%");
    Serial.print  ("Min Value:    "); Serial.print(sensor.min_value); Serial.println("%");
    Serial.print  ("Resolution:   "); Serial.print(sensor.resolution); Serial.println("%");
    Serial.println("------------------------------------");

    measuringDuration = DEFAULT_MEASURING_DURATION;
    sleepingPeriod = DEFAULT_SLEEPING_PERIOD;
    period15m = MINUTE_AVERAGE_PERIOD;
    period1h = HOUR_AVERAGE_PERIOD;

    sds.setQueryReportingMode();
    sds.setCustomWorkingPeriod(0);
    sds.wakeup();

    Serial.println(sds.queryFirmwareVersion().toString()); // prints firmware version
    Serial.println(sds.setQueryReportingMode().toString()); // ensures sensor is in 'query' reporting mode

    const char *ssid = "ALEKSNET-ROOF";
    connectToWifi();

    syncTime();

    Serial.println(""); Serial.print("Connected to "); Serial.println(ssid);
    Serial.print("IP address: "); Serial.println(WiFi.localIP());

    if (mdns.begin("esp8266", WiFi.localIP())) {
        Serial.println("MDNS responder started");
    }

    configureHttpServer();

    resetTimer();

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
        Serial.print("DEFAULT_MEASURING_DURATION is ");Serial.println(DEFAULT_MEASURING_DURATION);Serial.print("DEFAULT_SLEEPING_PERIOD is ");
        Serial.println(DEFAULT_SLEEPING_PERIOD);
    }
}

void loop() {

    delay(20);

    if (WiFi.status() != WL_CONNECTED) {
        connectToWifi();
    }

    time_t currentTime = rtcTime();
    byte currentMinute = minute(currentTime);
    byte currentHour = hour(currentTime);

    /*if (DEBUG_CASE2 && (millis() / 1000) % 300 == 0 && currentMinute != lastLogMinute) {
        lastLogMinute = currentMinute;
        Serial.print("currentMinute: ");
        Serial.print(currentMinute);
        Serial.print(", currentHour: ");
        Serial.print(currentHour);
        Serial.print(", last15MinuteAverageMinute: ");
        Serial.print(last15MinuteAverageMinute);
        Serial.print(", last1HourAverageHour: ");
        Serial.println(last1HourAverageHour);
    }*/

    if (currentMinute % period15m == 0 && currentMinute != last15MinuteAverageMinute) {
        last15MinuteAverageMinute = currentMinute;
        putEvery15MinuteMeasure(
                calculateAverage(currentTime, period15m * 60, everyMeasures, EVERY_MEASURES_NUMBER)
                );
    }

    if (currentHour % period1h == 0 && currentHour != last1HourAverageHour) {
        last1HourAverageHour = currentHour;
        putEveryHourMeasure(
                calculateAverage(currentTime, period1h * 3600, every15minutesMeasures, EVERY_15_MINUTES_MEASURES_NUMBER)
                );
    }

    server.handleClient();

    //  Step 1 - Measuring
    if (millis() - currentTimeMillisTimer > measuringDuration && step == 1) {

        currentTimeMillisTimer = millis();

        PmResult pm = sds.queryPm();
        float pm25 = NULL_MEASURE_VALUE;
        float pm10 = NULL_MEASURE_VALUE;
        if (pm.isOk()) {
            pm25 = round(pm.pm25*10)/10;
            pm10 = round(pm.pm10*10)/10;
        }

        sensors_event_t roofTempEvent, roofHumidEvent;
        dht21Roof.temperature().getEvent(&roofTempEvent);
        dht21Roof.humidity().getEvent(&roofHumidEvent);

        sensors_event_t windowTempEvent, windowHumidEvent;
        dht21Window.temperature().getEvent(&windowTempEvent);
        dht21Window.humidity().getEvent(&windowHumidEvent);

        sensors_event_t livingRoomTempEvent;
        sensors_event_t livingRoomHumidEvent;
        dht22LivingRoom.temperature().getEvent(&livingRoomTempEvent);
        dht22LivingRoom.humidity().getEvent(&livingRoomHumidEvent);

        float roofTemp = isnan(roofTempEvent.temperature) ? NULL_MEASURE_VALUE : roofTempEvent.temperature;
        float roofHumid = isnan(roofHumidEvent.relative_humidity) ? NULL_MEASURE_VALUE : roofHumidEvent.relative_humidity;
        float windowTemp = isnan(windowTempEvent.temperature) ? NULL_MEASURE_VALUE : windowTempEvent.temperature;
        float windowHumid = isnan(windowHumidEvent.relative_humidity) ? NULL_MEASURE_VALUE : windowHumidEvent.relative_humidity;
        float livingRoomTemp = isnan(livingRoomTempEvent.temperature) ? NULL_MEASURE_VALUE : livingRoomTempEvent.temperature;
        float livingRoomHumid = isnan(livingRoomHumidEvent.relative_humidity) ? NULL_MEASURE_VALUE : livingRoomHumidEvent.relative_humidity;

        char serviceInfo[17];

        bool windowTempIsLess = (windowTemp != NULL_MEASURE_VALUE && windowHumid != NULL_MEASURE_VALUE) && windowTemp < roofTemp;
        snprintf(serviceInfo, 17, "(%s)-(%.1fC/%.0f%%)",
                 windowTempIsLess ? "W" : "R",
                 windowTempIsLess ? roofTemp : windowTemp,
                 windowHumid
        );

        Measure currentMeasure = {
                currentTime,
                pm25,
                pm10,
                windowTempIsLess ? windowTemp : roofTemp,
                roofHumid,
                livingRoomTemp,
                livingRoomHumid,
                windowTempIsLess
        };
        strcpy(currentMeasure.serviceInfo, serviceInfo);

        putEveryMeasure(currentMeasure);

        if (DEBUG_CASE2) {
            Serial.print(numberOfEveryMsrPlaced); Serial.print(". Got a measure: "); printMeasure(currentMeasure);

//            Serial.print("DHT2130: t="); Serial.print(roofTempEvent.temperature); ; Serial.print(", outRh="); Serial.println(roofHumidEvent.relative_humidity);
//            Serial.print("DHT21fa: t="); Serial.print(windowTempEvent.temperature); ; Serial.print(", outRh="); Serial.println(windowHumidEvent.relative_humidity);
//            Serial.print("DHT2214: t="); Serial.print(livingRoomTempEvent.temperature); ; Serial.print(", outRh="); Serial.println(livingRoomHumidEvent.relative_humidity);

        }

        if (sleepingPeriod > 0) {
            WorkingStateResult state = sds.sleep();
            if (state.isWorking()) {
                Serial.println("Problem with sleeping the sensor.");
            }
            step = 2;
        }
        long time = millis() - currentTimeMillisTimer;
        if (time > maxMeasuringTime) {
            maxMeasuringTime = time;
        }
        logCounter++;
        if(logCounter % 60 == 0 && DEBUG_CASE2) {
            Serial.print("Max measuring time: ");
            Serial.println(maxMeasuringTime);
        }
    }

    //  Step 2 - Waking up an SDS sensor after a sleeping period if any
    if (sleepingPeriod > 0 && millis() - currentTimeMillisTimer > sleepingPeriod && step == 2) {
        currentTimeMillisTimer = millis();
        step = 1;
        sds.wakeup();
    }
}
