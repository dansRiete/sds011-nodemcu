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
#include <../lib/eeprom_rotate-master/src/EEPROM_Rotate.h>

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
#define MAX_MEASURES_STRING_LENGTH 170
#define NULL_MEASURE_VALUE -10000
const boolean DEBUG = false;
const boolean DEBUG_CASE2 = true;
#define DHT21_ROOF_PIN 12
#define DHT21_WINDOW_PIN 4
#define DHT22_LIVING_ROOM_PIN 13

#define EEPROM_DAILY_STORED_MEASURES_NUMBER 3
#define EEPROM_HOURLY_STORED_MEASURES_NUMBER 24
#define EEPROM_DAILY_MEASURES_OFFSET 256
#define EEPROM_HOURLY_MEASURES_OFFSET 2847
#define EEPROM_DAILY_CURSOR_POSITION_ADDRESS 10
const byte EEPROM_HOURLY_CURSOR_POSITION_ADDRESS = EEPROM_DAILY_CURSOR_POSITION_ADDRESS + 4;
const char TIME_API_URL[] = "http://worldtimeapi.org/api/timezone/Europe/Kiev.txt";

const byte SENSOR_RX_PIN = 2;
const byte SENSOR_DX_PIN = 0;
unsigned long int currentTimeMillisTimer = 0;
byte step = 1;
enum ContentType {HTML, CSV, TEXT};
enum MeasureType {INSTANT, MINUTE, HOUR, DAY};
MDNSResponder mdns;
ESP8266WebServer server(80);
SdsDustSensor sds(SENSOR_RX_PIN, SENSOR_DX_PIN);
DHT_Unified dht21Roof(DHT21_ROOF_PIN, DHT21);
DHT_Unified dht21Window(DHT21_WINDOW_PIN, DHT21);
DHT_Unified dht22LivingRoom(DHT22_LIVING_ROOM_PIN, DHT22);
const char csvHeader[] = "date, pm2.5, pm10, inTemp, inRH, inAH, outTemp, outRH, outAH\n";
EEPROM_Rotate EEPROMr;

struct Measure {
    time_t measureTime;
    signed short int pm25;
    signed short int pm10;
    signed short int outTemp;
    signed short int minOutTemp;
    signed short int maxOutTemp;
    signed short int outRh;
    signed short int inTemp;
    signed short int inRh;
    boolean window;
    char serviceInfo[20];
};
struct SimpleMeasure {
    time_t measureTime;
    signed short int pm25;
    signed short int pm10;
    signed short int outTemp;
    signed short int minOutTemp;
    signed short int maxOutTemp;
    signed short int outRh;
    signed short int inTemp;
    signed short int inRh;
};

const byte MEASURE_SIZE = sizeof(Measure);
const byte SIMPLE_MEASURE_SIZE = sizeof(SimpleMeasure);

const Measure nullMeasure = {0, NULL_MEASURE_VALUE, NULL_MEASURE_VALUE, NULL_MEASURE_VALUE, NULL_MEASURE_VALUE,
                       NULL_MEASURE_VALUE, NULL_MEASURE_VALUE, NULL_MEASURE_VALUE, NULL_MEASURE_VALUE, false, "null"};
const SimpleMeasure nullSMeasure = {0, NULL_MEASURE_VALUE, NULL_MEASURE_VALUE, NULL_MEASURE_VALUE, NULL_MEASURE_VALUE,
                             NULL_MEASURE_VALUE, NULL_MEASURE_VALUE, NULL_MEASURE_VALUE, NULL_MEASURE_VALUE};

#define INSTANT_MEASURES_NUMBER 180
struct Measure instantMeasures[INSTANT_MEASURES_NUMBER];
int instantMeasureIndex = -1;
boolean instantMeasureFirstPass = true;

#define MINUTES_AVG_MEASURES_NUMBER 4*24
struct Measure every15minutesMeasures[MINUTES_AVG_MEASURES_NUMBER];
int minutesAvgMeasureIndex = -1;
boolean minutesAvgMeasureFirstPass = true;

#define HOUR_AVG_MEASURES_NUMBER 5*24
struct Measure hourlyMeasures[HOUR_AVG_MEASURES_NUMBER];
int hourlyMeasuresIndex = -1;
boolean hourlyMeasuresFirstPass = true;

#define DAILY_MEASURES_NUMBER 31
struct Measure dailyMeasures[DAILY_MEASURES_NUMBER];
int dailyMeasuresIndex = -1;
boolean dailyMeasureFirstPass = true;

byte lastMinutesAverageMinute = NULL_MEASURE_VALUE;
byte last1HourAverageHour = NULL_MEASURE_VALUE;
byte last1DayAverageDay = NULL_MEASURE_VALUE;

byte lastLogMinute = NULL_MEASURE_VALUE;

int numberOfEveryMsrPlaced = 0;
int numberOf15mMsrPlaced = 0;
int numberOf1hMsrPlaced = 0;
int numberOf15mAveraged = 0;
int numberOf1hAveraged = 0;
int numberOfDayAveraged = 0;

void resetTimer() {
    currentTimeMillisTimer = millis();
    time_t time = now();
    lastMinutesAverageMinute = minute(time);
    last1HourAverageHour = hour(time);
    last1DayAverageDay = day(time);
}

char* getTimeString(time_t time) {
    static char measureString[20];
    snprintf(measureString, 20, "%02d/%02d/%d %02d:%02d:%02d", day(time), month(time), year(time), hour(time), minute(time), second(time));
    return measureString;
}

float calculateAbsoluteHumidity(float temp, float rh) {
    return 6.112 * pow(2.71828, 17.67 * temp / (243.5 + temp)) * rh * 2.1674 / (273.15 + temp);
}

char* measureToString(Measure measure) {
    static char measureString[MAX_MEASURES_STRING_LENGTH];
    snprintf(measureString, MAX_MEASURES_STRING_LENGTH,
             "%s - PM2.5 = %.1f, PM10 = %.1f, IN[%.1fC-%.0f%%-%.1fg/m3], OUT[%.1fC%s-%.0f%%-%.1fg/m3, min=%.1fC, max=%.1fC]",
             getTimeString(measure.measureTime),
             round((float) measure.pm25 / 10) / 10,
             round((float) measure.pm10 / 10) / 10,
             round((float) measure.inTemp / 10) / 10,
             round((float) measure.inRh / 10) / 10,
             measure.inTemp == NULL_MEASURE_VALUE || measure.inRh == NULL_MEASURE_VALUE ? NULL_MEASURE_VALUE
                              : round(calculateAbsoluteHumidity(measure.inTemp/100, measure.inRh/100)*100)/100,
             round((float) measure.outTemp / 10) / 10,
             measure.serviceInfo,
             round((float) measure.outRh / 10) / 10,
             measure.outTemp == NULL_MEASURE_VALUE || measure.outRh == NULL_MEASURE_VALUE ? NULL_MEASURE_VALUE
                               : round(calculateAbsoluteHumidity(measure.outTemp/100, measure.outRh/100)*100)/100,
             round((float) measure.minOutTemp / 10) / 10,
             round((float) measure.maxOutTemp / 10) / 10
    );
    return measureString;
}

char* measureToString(SimpleMeasure measure) {
    Measure measure1 = {
            measure.measureTime,
            measure.pm25,
            measure.pm10,
            measure.outTemp,
            measure.minOutTemp,
            measure.maxOutTemp,
            measure.outRh,
            measure.inTemp,
            measure.inRh,
            false,
            ""
    };
    return measureToString(measure1);
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
             round((float) measure.pm25 / 10) / 10,
             round((float) measure.pm10 / 10) / 10,
             round((float) measure.inTemp / 10) / 10,
             round((float) measure.inRh / 10) / 10,
             measure.inTemp == NULL_MEASURE_VALUE || measure.outRh == NULL_MEASURE_VALUE ? NULL_MEASURE_VALUE :
             round(calculateAbsoluteHumidity(measure.outTemp/100, measure.outRh/100)*100),
             round((float) measure.outTemp / 10) / 10,
             round((float) measure.outRh / 10) / 10,
             measure.outTemp == NULL_MEASURE_VALUE || measure.outRh == NULL_MEASURE_VALUE ? NULL_MEASURE_VALUE :
             round(calculateAbsoluteHumidity(measure.outTemp/100, measure.outRh/100)*100)
    );
    return measureString;
}

void printMeasure(Measure measure) {
    Serial.println(measureToString(measure));
}

boolean isNullMeasure(const Measure& measure){
    return measure.pm10 == NULL_MEASURE_VALUE && measure.pm25 == NULL_MEASURE_VALUE && measure.pm10 == NULL_MEASURE_VALUE
           && measure.outTemp == NULL_MEASURE_VALUE && measure.minOutTemp == NULL_MEASURE_VALUE && measure.maxOutTemp == NULL_MEASURE_VALUE && measure.outRh == NULL_MEASURE_VALUE
           && measure.inTemp == NULL_MEASURE_VALUE && measure.inRh == NULL_MEASURE_VALUE;
}

void placeMeasure(const Measure& measure, MeasureType measureType) {
    if (isNullMeasure(measure)) {
        return;
    }
    int *index;
    boolean *firstPass;
    Measure *measuresArray;
    int *measuresNumberPlaced;
    int measuresNumber;
    
    switch (measureType) {
        case INSTANT:
            index = &instantMeasureIndex;
            firstPass = &instantMeasureFirstPass;
            measuresArray = instantMeasures;
            measuresNumberPlaced = &numberOfEveryMsrPlaced;
            measuresNumber = INSTANT_MEASURES_NUMBER;
            break;
        case MINUTE:
            index = &minutesAvgMeasureIndex;
            firstPass = &minutesAvgMeasureFirstPass;
            measuresArray = every15minutesMeasures;
            measuresNumberPlaced = &numberOf15mMsrPlaced;
            measuresNumber = MINUTES_AVG_MEASURES_NUMBER;
            break;
        case HOUR:
            index = &hourlyMeasuresIndex;
            firstPass = &hourlyMeasuresFirstPass;
            measuresArray = hourlyMeasures;
            measuresNumberPlaced = &numberOf1hMsrPlaced;
            measuresNumber = HOUR_AVG_MEASURES_NUMBER;
            break;
        case DAY:
            index = &dailyMeasuresIndex;
            firstPass = &dailyMeasureFirstPass;
            measuresArray = dailyMeasures;
            measuresNumberPlaced = &numberOfDayAveraged;
            measuresNumber = DAILY_MEASURES_NUMBER;
            break;
        default:
            index = &instantMeasureIndex;
            firstPass = &instantMeasureFirstPass;
            measuresArray = instantMeasures;
            measuresNumberPlaced = &numberOfEveryMsrPlaced;
            measuresNumber = INSTANT_MEASURES_NUMBER;
            break;
    }

    (*measuresNumberPlaced)++;
    if (++(*index) <= measuresNumber - 1 && *firstPass) {
        measuresArray[(*index)] = measure;
    } else {
        for (int i1 = 1, i2 = 0; i1 < measuresNumber; i1++, i2++) {
            // Shift all the array's content on one position
            measuresArray[i2] = measuresArray[i1];
        }
        measuresArray[measuresNumber - 1] = measure;
        if (*firstPass) {
            *firstPass = false;
        }
    }

    if (measureType == DAY) {
        int currentIndex;
        EEPROMr.get(EEPROM_DAILY_CURSOR_POSITION_ADDRESS, currentIndex);
        Serial.print("Daily index from EEPROM: ");
        Serial.println(currentIndex);
        if (currentIndex > EEPROM_DAILY_STORED_MEASURES_NUMBER - 1 || currentIndex < 0) {
            currentIndex = 0;
        }
        SimpleMeasure simpleMeasure = {
                measure.measureTime, measure.pm25, measure.pm10, measure.outTemp, measure.minOutTemp,
                measure.maxOutTemp, measure.outRh, measure.inTemp, measure.inRh
        };
        size_t address = EEPROM_DAILY_MEASURES_OFFSET + currentIndex * SIMPLE_MEASURE_SIZE;
        Serial.print("Writing a daily measure to a next address: ");
        Serial.println(address);
        EEPROMr.put(address, simpleMeasure);
        EEPROMr.put(EEPROM_DAILY_CURSOR_POSITION_ADDRESS, ++currentIndex);
        EEPROMr.commit();
    } else if (measureType == HOUR) {
        int currentIndex;
        EEPROMr.get(EEPROM_HOURLY_CURSOR_POSITION_ADDRESS, currentIndex);
        Serial.print("Hourly index from EEPROM: ");
        Serial.println(currentIndex);
        if (currentIndex > EEPROM_HOURLY_STORED_MEASURES_NUMBER - 1 || currentIndex < 0) {
            currentIndex = 0;
        }
        size_t address = EEPROM_HOURLY_MEASURES_OFFSET + currentIndex * MEASURE_SIZE;
        Serial.print("Writing a hourly measure to a next address: ");
        Serial.println(address);
        EEPROMr.put(address, measure);
        EEPROMr.put(EEPROM_HOURLY_CURSOR_POSITION_ADDRESS, ++currentIndex);
        EEPROMr.commit();

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
    long diff = currTimeTS - measureTs;
    return diff > 0 && diff <= intervalSec;
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
    int pm25Sum = 0, pm10Sum = 0, outTempSum = 0, outRhSum = 0, inTempSum = 0, inRhSum = 0;
    int pm25Counter = 0, pm10Counter = 0, outTempCounter = 0, outRhCounter = 0, inTempCounter = 0, inRhCounter = 0,
    windowCounter = 0, measurementsCounter = 0;
    signed short int minTemp = 32767;
    signed short int maxTemp = -32768;
    for (int i = 0; i < measurementArraySize; i++) {
        Measure measure = measuresSource[i];
        if (isInIntervalOfSeconds(currentTime, measure.measureTime, seconds)) {
            measurementsCounter++;
            if(DEBUG_CASE2){ logAverage(measure); }
            if (measure.minOutTemp != NULL_MEASURE_VALUE && measure.minOutTemp < minTemp) {
                minTemp = measure.minOutTemp;
            }
            if (measure.maxOutTemp != NULL_MEASURE_VALUE && measure.maxOutTemp > maxTemp) {
                maxTemp = measure.maxOutTemp;
            }
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
            currentTime - seconds,
            static_cast<short>(pm25Counter == 0 ? NULL_MEASURE_VALUE : pm25Sum / pm25Counter),
            static_cast<short>(pm10Counter == 0 ? NULL_MEASURE_VALUE : pm10Sum / pm10Counter),
            static_cast<short>(outTempCounter == 0 ? NULL_MEASURE_VALUE : outTempSum / outTempCounter),
            minTemp,
            maxTemp,
            static_cast<short>(outRhCounter == 0 ? NULL_MEASURE_VALUE : outRhSum / outRhCounter),
            static_cast<short>(inTempCounter == 0 ? NULL_MEASURE_VALUE : inTempSum / inTempCounter),
            static_cast<short>(inRhCounter == 0 ? NULL_MEASURE_VALUE : inRhSum / inRhCounter),
            window
    };

    if (seconds == 24 * 3600 && measurementsCounter != 24) {
        Serial.printf("Not enough measures for daily average %d", measurementsCounter);
        result = nullMeasure;
    }

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
    Serial.print("Getting current time from ");
    Serial.println(TIME_API_URL);
    http.begin(TIME_API_URL);
    int httpCode = http.GET();
    if (httpCode == 200) {
        String payload = http.getString();
        std::string timePayload = payload.c_str();
        unsigned int timePosition = timePayload.find("unixtime: ");
        std::string unixTime = timePayload.substr(timePosition + 10, timePosition + 10 + 10);
        String a = unixTime.c_str();
        Serial.println(payload);
        setTime((time_t)a.toInt() + 3 * 3600);
        Serial.print("Time synced. Current time is ");
        Serial.println(timeToString(rtcTime()));
        http.end();   //Close connection
    } else {
        Serial.print("Time syncing failed. Server respond with next code: ");
        Serial.println(httpCode);
    }
}

void sendChunkedContent(Measure *measures, int measuresSize, ContentType contentType, boolean eeprom) {
    static const char br[5] = "<br>";
    static char cont[MAX_MEASURES_STRING_LENGTH * HTTP_RESPONSE_CHUNKS_SIZE] = "";
    Measure *currMeasures;
    if (eeprom) {
        static Measure readMeasures[EEPROM_DAILY_STORED_MEASURES_NUMBER];
        measuresSize = EEPROM_DAILY_STORED_MEASURES_NUMBER;
        int currentIndex;
        EEPROMr.get(EEPROM_DAILY_CURSOR_POSITION_ADDRESS, currentIndex);
        Serial.print("Daily index from EEPROM: ");
        Serial.println(currentIndex);
        if (currentIndex > EEPROM_DAILY_STORED_MEASURES_NUMBER - 1 || currentIndex < 0) {
            currentIndex = 0;
        }

        Serial.println("Reading the daily measures from EEPROM ...\n");

        for (int i = 0; i < EEPROM_DAILY_STORED_MEASURES_NUMBER; i++) {
            if (currentIndex > EEPROM_DAILY_STORED_MEASURES_NUMBER - 1) {
                currentIndex = 0;
            }
            SimpleMeasure measure;
            size_t address = EEPROM_DAILY_MEASURES_OFFSET + currentIndex++ * SIMPLE_MEASURE_SIZE;
            Serial.print("Trying to get a daily measure from the next address: ");
            Serial.print(address);
            Serial.print(", index was: ");
            Serial.println(currentIndex - 1);

            EEPROMr.get(address, measure);
            Measure measure1 = {
                    measure.measureTime,
                    measure.pm25,
                    measure.pm10,
                    measure.outTemp,
                    measure.minOutTemp,
                    measure.maxOutTemp,
                    measure.outRh,
                    measure.inTemp,
                    measure.inRh,
                    false,
                    ""
            };
            readMeasures[i] = measure1;
            Serial.println(measureToString(measure));
        }
        Serial.println(); Serial.println();
        currMeasures = readMeasures;
    } else{
        currMeasures = measures;
    }

    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server.send(200, "text/html", "");
    int currentMeasureIndex = measuresSize - 1;
    boolean anyDataHasBeenSent = false;

    do {
        strcpy(cont, contentType == CSV && !anyDataHasBeenSent ? csvHeader : "");
        boolean thereIsData = false;
        for (int i = 0 ; i < HTTP_RESPONSE_CHUNKS_SIZE && currentMeasureIndex >= 0; i++) {
            if(!isNullMeasure(currMeasures[currentMeasureIndex])){
                thereIsData = anyDataHasBeenSent = true;
                strcat(
                        cont, contentType == CSV ? measureToCsvString(currMeasures[currentMeasureIndex]) :
                        measureToString(currMeasures[currentMeasureIndex])
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

void clearEeprom(){
    int currentIndex = 0;
    Serial.print("Daily index from EEPROM: ");
    Serial.println(currentIndex);

    for(;currentIndex < EEPROM_DAILY_STORED_MEASURES_NUMBER; currentIndex++){
        size_t address = EEPROM_DAILY_MEASURES_OFFSET + currentIndex * SIMPLE_MEASURE_SIZE;
        Serial.print("Writing a daily measure to a next address: ");
        Serial.println(address);
        EEPROMr.put(address, nullSMeasure);
    }

    currentIndex = 0;

    for(;currentIndex < EEPROM_HOURLY_STORED_MEASURES_NUMBER; currentIndex++){
        size_t address = EEPROM_HOURLY_MEASURES_OFFSET + currentIndex * MEASURE_SIZE;
        Serial.print("Writing a daily measure to a next address: ");
        Serial.println(address);
        EEPROMr.put(address, nullMeasure);
    }

    EEPROMr.commit();
}

void configureHttpServer() {

    server.on("/restart", []() {
        ESP.restart();
        server.send(200, "text/html", "OK");
    });

    server.on("/eeprom/clear", []() {
        clearEeprom();
        server.send(200, "text/html", "OK");
    });

    server.on("/instant", []() {
        sendChunkedContent(instantMeasures, INSTANT_MEASURES_NUMBER, HTML, false);
    });

    server.on("/avg/minute", []() {
        sendChunkedContent(every15minutesMeasures, MINUTES_AVG_MEASURES_NUMBER, HTML, false);
    });

    server.on("/avg/hourly", []() {
        sendChunkedContent(hourlyMeasures, HOUR_AVG_MEASURES_NUMBER, HTML, false);
    });

    server.on("/avg/daily", []() {
        sendChunkedContent(instantMeasures, INSTANT_MEASURES_NUMBER, HTML, true);
    });

    server.on("/csv/1", []() {
        sendChunkedContent(instantMeasures, INSTANT_MEASURES_NUMBER, CSV, false);
    });

    server.on("/csv/15", []() {
        sendChunkedContent(every15minutesMeasures, MINUTES_AVG_MEASURES_NUMBER, CSV, false);
    });

    server.on("/csv/60", []() {
        sendChunkedContent(hourlyMeasures, HOUR_AVG_MEASURES_NUMBER, CSV, false);
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
        lastMinutesAverageMinute = minute;
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

    EEPROMr.size(4);
    EEPROMr.begin(4 * 1024);

    for (auto &reading : instantMeasures) {
        reading = nullMeasure;
    }
    for (auto &reading : every15minutesMeasures) {
        reading = nullMeasure;
    }

    Serial.print("Size of Measure: ");
    Serial.print(MEASURE_SIZE);
    Serial.print(" bytes\nSize of SimpleMeasure: ");
    Serial.print(SIMPLE_MEASURE_SIZE);
    Serial.print(" bytes\n");

    int currentIndex;
    EEPROMr.get(EEPROM_HOURLY_CURSOR_POSITION_ADDRESS, currentIndex);
    if (currentIndex > EEPROM_HOURLY_STORED_MEASURES_NUMBER - 1 || currentIndex < 0) {
        currentIndex = 0;
    }

    Serial.println("Populating an initial collection with the hourly measures from EEPROM ...");
    //CONSTRAINT:  HOUR_AVG_MEASURES_NUMBER MUST BE GREATER THAN EEPROM_HOURLY_STORED_MEASURES_NUMBER
    for (int i1 = 0, i2 = HOUR_AVG_MEASURES_NUMBER - EEPROM_HOURLY_STORED_MEASURES_NUMBER; i1 < EEPROM_HOURLY_STORED_MEASURES_NUMBER; i1++, i2++) {
        if (currentIndex > EEPROM_HOURLY_STORED_MEASURES_NUMBER - 1) {
            currentIndex = 0;
        }
        Measure measure;
        size_t address = EEPROM_HOURLY_MEASURES_OFFSET + currentIndex++ * MEASURE_SIZE;
        Serial.print(i1);
        Serial.print(". Cursor position: ");
        Serial.print(currentIndex - 1);
        Serial.print(", byte address: ");
        Serial.print(address);

        EEPROMr.get(address, measure);
        hourlyMeasures[i2] = measure;
        Serial.print(", loaded measure: ");
        Serial.println(measureToString(measure));
    }
    Serial.println();
    hourlyMeasuresFirstPass = false;
    hourlyMeasuresIndex = EEPROM_HOURLY_STORED_MEASURES_NUMBER;


    for (int i = 0; i < HOUR_AVG_MEASURES_NUMBER - EEPROM_HOURLY_STORED_MEASURES_NUMBER; i++) {
        hourlyMeasures[i] = nullMeasure;
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
    byte currentDay = day(currentTime);

    /*if (DEBUG_CASE2 && (millis() / 1000) % 300 == 0 && currentMinute != lastLogMinute) {
        lastLogMinute = currentMinute;
        Serial.print("currentMinute: ");
        Serial.print(currentMinute);
        Serial.print(", currentHour: ");
        Serial.print(currentHour);
        Serial.print(", lastMinutesAverageMinute: ");
        Serial.print(lastMinutesAverageMinute);
        Serial.print(", last1HourAverageHour: ");
        Serial.println(last1HourAverageHour);
    }*/

    if (currentMinute % period15m == 0 && currentMinute != lastMinutesAverageMinute) {
        lastMinutesAverageMinute = currentMinute;
        const Measure &measure = calculateAverage(currentTime, period15m * 60, instantMeasures, INSTANT_MEASURES_NUMBER);
        placeMeasure(measure, MINUTE);
    }

    if (currentHour % period1h == 0 && currentHour != last1HourAverageHour) {
        last1HourAverageHour = currentHour;
        const Measure &measure = calculateAverage(currentTime, period1h * 3600, every15minutesMeasures, MINUTES_AVG_MEASURES_NUMBER);
        placeMeasure(measure, HOUR);
    }

    if (currentDay != last1DayAverageDay) {
        last1DayAverageDay = currentDay;
        const Measure &measure = calculateAverage(currentTime, 24 * 3600, hourlyMeasures, HOUR_AVG_MEASURES_NUMBER);
        placeMeasure(measure, DAY);
    }

    server.handleClient();

    //  Step 1 - Measuring
    if (millis() - currentTimeMillisTimer > measuringDuration && step == 1) {

        currentTimeMillisTimer = millis();

        PmResult pm = sds.queryPm();
        signed short int pm25 = NULL_MEASURE_VALUE;
        signed short int pm10 = NULL_MEASURE_VALUE;
        if (pm.isOk()) {
            pm25 = round(pm.pm25*100);
            pm10 = round(pm.pm10*100);
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

        signed short int roofTemp = isnan(roofTempEvent.temperature) ? NULL_MEASURE_VALUE : round(roofTempEvent.temperature*100);
        signed short int roofHumid = isnan(roofHumidEvent.relative_humidity) ? NULL_MEASURE_VALUE : round(roofHumidEvent.relative_humidity*100);
        signed short int windowTemp = isnan(windowTempEvent.temperature) ? NULL_MEASURE_VALUE : round(windowTempEvent.temperature*100);
        signed short int windowHumid = isnan(windowHumidEvent.relative_humidity) ? NULL_MEASURE_VALUE : round(windowHumidEvent.relative_humidity*100);
        signed short int livingRoomTemp = isnan(livingRoomTempEvent.temperature) ? NULL_MEASURE_VALUE : round(livingRoomTempEvent.temperature*100);
        signed short int livingRoomHumid = isnan(livingRoomHumidEvent.relative_humidity) ? NULL_MEASURE_VALUE : round(livingRoomHumidEvent.relative_humidity*100);

        char serviceInfo[17];

        bool windowTempIsLess = (windowTemp != NULL_MEASURE_VALUE && windowHumid != NULL_MEASURE_VALUE) && windowTemp < roofTemp;
        snprintf(serviceInfo, 17, "(%s)-(%.1fC/%.0f%%)",
                 windowTempIsLess ? "W" : "R",
                 windowTempIsLess ? ((float)roofTemp)/100 :((float) windowTemp)/100,
                 ((float)windowHumid)/100
        );

        short temp = windowTempIsLess ? windowTemp : roofTemp;
        Measure currentMeasure = {
                currentTime,
                pm25,
                pm10,
                temp,
                temp,
                temp,
                roofHumid,
                livingRoomTemp,
                livingRoomHumid,
                windowTempIsLess
        };
        strcpy(currentMeasure.serviceInfo, serviceInfo);

        placeMeasure(currentMeasure, INSTANT);

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
