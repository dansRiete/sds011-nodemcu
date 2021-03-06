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
#include <../lib/PubSubClient-2.8.0/src/PubSubClient.h>
#include <ArduinoOTA.h>

#define WIFI_MAX_RETRIES 20
#define MQTT_MAX_RETRIES 1
#define DEFAULT_MEASURING_DURATION_MILLIS 5 * 1000
#define DEFAULT_SLEEPING_PERIOD_MILLIS 55 * 1000
#define MINUTE_AVERAGE_PERIOD_SEC 10 * 60
#define HOUR_AVERAGE_PERIOD_SEC 1 * 3600
#define DAILY_AVERAGE_PERIOD_SEC 24 * 3600
int logCounter = 0;
int maxMeasuringTime = 0;
unsigned measuringDuration;
unsigned sleepingPeriod;
long period15m;
long period1h;
long period1d;
#define HTTP_RESPONSE_CHUNKS_SIZE 20
#define MAX_MEASURES_STRING_LENGTH 400
#define NULL_MEASURE_VALUE -10000
#define SIGNED_SHORT_MAX_VALUE 32767
const boolean DEBUG = false;
const boolean DEBUG_CASE2 = true;
#define DHT21_ROOF_PIN 12
#define DHT21_WINDOW_PIN 4
#define DHT22_LIVING_ROOM_PIN 13
#define DHT22_TERRACE_IN_PIN 14
#define SDS_RX_PIN 2
#define SDS_DX_PIN 0

#define EEPROM_DAILY_STORED_MEASURES_NUMBER 120
#define EEPROM_HOURLY_STORED_MEASURES_NUMBER 24
#define EEPROM_DAILY_MEASURES_OFFSET 256
#define EEPROM_HOURLY_MEASURES_OFFSET 2847
#define EEPROM_DAILY_CURSOR_POSITION_ADDRESS 10
#define MQTT_TOPIC "ALEXKZK-SMARTHOUSE"
#define MQTT_SERVER "185.212.128.246"
#define MQTT_USER "alexkzk"
#define MQTT_PASSWORD "Vlena<G13"
#define MQTT_PORT 1883
#define UTC_HOURS_SHIFT 2
const byte EEPROM_HOURLY_CURSOR_POSITION_ADDRESS = EEPROM_DAILY_CURSOR_POSITION_ADDRESS + 4;
const char TIME_API_URL[] = "http://worldtimeapi.org/api/timezone/Europe/Kiev.txt";
String MQTT_SUBSCRIBER;

unsigned long int currentTimeMillisTimer = 0;
byte step = 1;
enum ContentType {
    HTML, CSV, TEXT
};
enum MeasureType {
    INSTANT, MINUTELY, HOURLY, DAILY
};
MDNSResponder mdns;
ESP8266WebServer server(80);
SdsDustSensor sds(SDS_RX_PIN, SDS_DX_PIN);
DHT_Unified dht21Roof(DHT21_ROOF_PIN, DHT21);
DHT_Unified dht21Window(DHT21_WINDOW_PIN, DHT21);
DHT_Unified dht22LivingRoom(DHT22_LIVING_ROOM_PIN, DHT22);
DHT_Unified dht22TerraceIn(DHT22_TERRACE_IN_PIN, DHT22);
const char csvHeader[] = "date, pm2.5, pm10, inTemp, inRH, inAH, outTemp, outRH, outAH\n";
const char nullString[] = "NULL";
EEPROM_Rotate EEPROMr;
WiFiClient wclient;
PubSubClient mqttClient(wclient);

struct Measure {
    time_t measureTime;
    signed short int pm25;      //  todo change to unsigned short and multiple by 10 not 100 to increase max range width
    signed short int pm10;
    signed short int outTemp;
    signed short int minOutTemp;
    signed short int maxOutTemp;
    signed short int outRh;
    signed short int inTemp;
    signed short int inRh;
    boolean window;     //  todo replace by measure place enum
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
                             NULL_MEASURE_VALUE, NULL_MEASURE_VALUE, NULL_MEASURE_VALUE, NULL_MEASURE_VALUE, false,
                             "null"};
const SimpleMeasure nullSMeasure = {0, NULL_MEASURE_VALUE, NULL_MEASURE_VALUE, NULL_MEASURE_VALUE, NULL_MEASURE_VALUE,
                                    NULL_MEASURE_VALUE, NULL_MEASURE_VALUE, NULL_MEASURE_VALUE, NULL_MEASURE_VALUE};

/*#define INSTANT_MEASURES_NUMBER 180
struct Measure instantMeasures[INSTANT_MEASURES_NUMBER];
int instantMeasureIndex = -1;
boolean instantMeasureFirstPass = true;

#define MINUTES_AVG_MEASURES_NUMBER 4*24
struct Measure every15minutesMeasures[MINUTES_AVG_MEASURES_NUMBER];
int minutesAvgMeasureIndex = -1;
boolean minutesAvgMeasureFirstPass = true;

#define HOURLY_AVG_MEASURES_NUMBER 5*24
struct Measure hourlyMeasures[HOURLY_AVG_MEASURES_NUMBER];
int hourlyMeasuresIndex = -1;
boolean hourlyMeasuresFirstPass = true;

#define DAILY_MEASURES_NUMBER 31
struct Measure dailyMeasures[DAILY_MEASURES_NUMBER];*/

time_t lastMinAvg = NULL_MEASURE_VALUE;
time_t lastHourAvg = NULL_MEASURE_VALUE;

int dailyMeasuresIndex = -1;
boolean dailyMeasureFirstPass = true;

byte lastMinutesAverageMinute = NULL_MEASURE_VALUE;
byte last1HourAverageHour = NULL_MEASURE_VALUE;
byte last1DayAverageDay = NULL_MEASURE_VALUE;

int numberOfEveryMsrPlaced = 0;
int numberOf15mMsrPlaced = 0;
int numberOf1hMsrPlaced = 0;
int numberOf15mAveraged = 0;
int numberOf1hAveraged = 0;
int numberOfDayAveraged = 0;

Measure minuteRollupAveragedMeasure;
Measure hourlyRollupAveragedMeasure;
Measure dailyRollupAveragedMeasure;

void resetTimer() {
    currentTimeMillisTimer = millis();
    time_t time = now();
    lastMinutesAverageMinute = minute(time);
    last1HourAverageHour = hour(time);
    last1DayAverageDay = day(time);
}

char *getTimeString(time_t time) {
    static char measureString[20];
    snprintf(measureString, 20, "%02d/%02d/%d %02d:%02d:%02d", day(time), month(time), year(time), hour(time),
             minute(time), second(time));
    return measureString;
}

double roundTo(double value, int accuracy) {
    if (value == NULL_MEASURE_VALUE) {
        return NULL_MEASURE_VALUE;
    }
    return round(value * accuracy) / accuracy;
}

double round1(double value) {
    if (value == NULL_MEASURE_VALUE) {
        return NULL_MEASURE_VALUE;
    }
    return roundTo(value, 10);
}

double round2(double value) {
    if (value == NULL_MEASURE_VALUE) {
        return NULL_MEASURE_VALUE;
    }
    return roundTo(value, 100);
}

float calculateAbsoluteHumidity(double temp, double rh) {
    if (isnan(temp) || isnan (rh)) {
        return NAN;
    }
    return 6.112 * pow(2.71828, 17.67 * temp / (243.5 + temp)) * rh * 2.1674 / (273.15 + temp);
}

float calculateOutdoorAbsoluteHumidity(Measure &measure) {
    if (measure.outTemp == NULL_MEASURE_VALUE || measure.outRh == NULL_MEASURE_VALUE) {
        return NULL_MEASURE_VALUE / 100.0;
    }
    return calculateAbsoluteHumidity(round1(measure.outTemp / 100.0), round1(measure.outRh / 100.0));
}

float calculateIndoorAbsoluteHumidity(Measure &measure) {
    if (measure.inTemp == NULL_MEASURE_VALUE || measure.inRh == NULL_MEASURE_VALUE) {
        return NULL_MEASURE_VALUE / 100.0;
    }
    return calculateAbsoluteHumidity(round1(measure.inTemp / 100.0), round1(measure.inRh / 100.0));
}

char *measureToString(Measure measure, boolean rollup) {
    static char measureString[MAX_MEASURES_STRING_LENGTH];
    double pm25 = round1(measure.pm25 / 100.0);
    double pm10 = round1(measure.pm10 / 100.0);
    double inTemp = round1(measure.inTemp / 100.0);
    double inRh = round1(measure.inRh / 100.0);
    double inAh = round2(calculateIndoorAbsoluteHumidity(measure));
    double outTemp = round1(measure.outTemp / 100.0);
    double outRh = round1(measure.outRh / 100.0);
    double outAh = round2(calculateOutdoorAbsoluteHumidity(measure));
    double minOutTemp = round1(measure.minOutTemp / 100.0);
    double maxOutTemp = round1(measure.maxOutTemp / 100.0);
    snprintf(measureString, MAX_MEASURES_STRING_LENGTH,
             "%s%s - PM2.5 = %s, PM10 = %s, IN[%s-%s-%s], OUT[%s%s-%s-%s, min=%s, max=%s]",
             getTimeString(measure.measureTime),
             rollup ? "R" : "",
             pm25 < -90 ? nullString : String(pm25).c_str(),
             pm10 < -90 ? nullString : String(pm10).c_str(),
             inTemp < -90 ? nullString : (String(inTemp) + String("C")).c_str(),
             inRh < -90 ? nullString : (String(inRh) + String("%")).c_str(),
             inAh < -90 ? nullString : (String(inAh) + String("g/m3")).c_str(),
             outTemp < -90 ? nullString : (String(outTemp) + String("C")).c_str(),
             measure.serviceInfo,
             outRh < -90 ? nullString : (String(outRh) + String("%")).c_str(),
             outAh < -90 ? nullString : (String(outAh) + String("g/m3")).c_str(),
             minOutTemp < -90 ? nullString : (String(minOutTemp) + String("C")).c_str(),
             maxOutTemp < -90 ? nullString : (String(maxOutTemp) + String("C")).c_str()
    );
    return measureString;
}

char *measureToString(Measure measure) {
    return measureToString(measure, false);
}

char *measureToString(SimpleMeasure measure) {
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

char* getIsoTimeString(time_t time) {
    static char measureString[30];
    snprintf(measureString, 30, "%02d-%02d-%02dT%02d:%02d:%02d", year(time), month(time), day(time), hour(time), minute(time), second(time));
    return measureString;
}

char *timeToString(time_t t) {
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

char *measureToCsvString(Measure measure) {
    static char measureString[100];
    snprintf(measureString, 100, "%02d/%02d/%d-%02d:%02d:%02d, %.1f, %.1f, %.1f, %.0f, %.1f, %.1f, %.0f, %.1f\n",
             day(measure.measureTime),
             month(measure.measureTime),
             year(measure.measureTime),
             hour(measure.measureTime),
             minute(measure.measureTime),
             second(measure.measureTime),
             round1(measure.pm25 / 100.0),
             round1(measure.pm10 / 100.0),
             round1(measure.inTemp / 100.0),
             round1(measure.inRh / 100.0),
             round2(calculateIndoorAbsoluteHumidity(measure)),
             round1(measure.outTemp / 100.0),
             round1(measure.outRh / 100.0),
             round2(calculateOutdoorAbsoluteHumidity(measure))
    );
    return measureString;
}

void printMeasure(Measure measure) {
    Serial.println(measureToString(measure));
}

boolean isNullMeasure(const Measure &measure) {
    return measure.pm10 == NULL_MEASURE_VALUE && measure.pm25 == NULL_MEASURE_VALUE
           && measure.outTemp == NULL_MEASURE_VALUE && measure.outRh == NULL_MEASURE_VALUE
           && measure.inTemp == NULL_MEASURE_VALUE && measure.inRh == NULL_MEASURE_VALUE;
}

void placeHourlyMeasureEeprom(const Measure &measure) {
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

void placeDailyMeasureToEeprom(const Measure &measure, boolean autoCommit) {
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
    if (autoCommit) {
        EEPROMr.commit();
    }
}

void placeDailyMeasureToEeprom(const Measure &measure) {
    placeDailyMeasureToEeprom(measure, true);
}

/*void placeMeasure(const Measure &measure, MeasureType measureType) {
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
        case MINUTELY:
            index = &minutesAvgMeasureIndex;
            firstPass = &minutesAvgMeasureFirstPass;
            measuresArray = every15minutesMeasures;
            measuresNumberPlaced = &numberOf15mMsrPlaced;
            measuresNumber = MINUTES_AVG_MEASURES_NUMBER;
            break;
        case HOURLY:
            index = &hourlyMeasuresIndex;
            firstPass = &hourlyMeasuresFirstPass;
            measuresArray = hourlyMeasures;
            measuresNumberPlaced = &numberOf1hMsrPlaced;
            measuresNumber = HOURLY_AVG_MEASURES_NUMBER;
            break;
        case DAILY:
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

    if (measureType == DAILY) {
        placeDailyMeasureToEeprom(measure);
    } else if (measureType == HOURLY) {
        placeHourlyMeasureEeprom(measure);
    }
}*/

bool isInIntervalOfSeconds(time_t currentTime, time_t measureTime, long intervalSec) {
    if (measureTime == 0 || intervalSec == 0) {
        return false;
    }
    long currTimeTS = (long) currentTime;
    long measureTs = (long) measureTime;
    long diff = currTimeTS - measureTs;
    return diff > 0 && diff <= intervalSec;
}

void logAverage(const Measure &measure) {
    Serial.printf("\t [%s t:%.1f, min:%.1f, max:%.1f] ",
                  getTimeString(measure.measureTime),
                  measure.outTemp / 100.0, measure.minOutTemp / 100.0,
                  measure.maxOutTemp / 100.0);
}

short averagePeriods(int summ, int periods, int targetPeriods) {
    if (periods == 0 || (targetPeriods > 0 && periods != targetPeriods)) {
        return NULL_MEASURE_VALUE;
    } else {
        int result = summ / periods;
        return result > SIGNED_SHORT_MAX_VALUE ? SIGNED_SHORT_MAX_VALUE : result;
    }
}

void computeAvg(const Measure &measure, int &pm25Sum, int &pm10Sum, int &outTempSum, int &outRhSum, int &inTempSum,
                int &inRhSum, int &pm25Counter, int &pm10Counter, int &outTempCounter, int &outRhCounter,
                int &inTempCounter, int &inRhCounter, int &windowCounter, int &measurementsCounter, short &minTemp,
                short &maxTemp) {
    measurementsCounter++;
    if (DEBUG_CASE2) { logAverage(measure); }
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

Measure calculateAverage(time_t currentTime, MeasureType measureType, Measure *measuresSource, int measurementArraySize,
                         boolean rollup) {

    long seconds = 0;
    int targetPeriods = 0;
    switch (measureType) {
        case MINUTELY :
            seconds = period15m;
            break;
        case HOURLY :
            seconds = period1h;
            break;
        case DAILY :
            targetPeriods = 24;
            seconds = period1d;
            break;
        case INSTANT :
            return nullMeasure;
    }

    if (DEBUG_CASE2) {
        Serial.println();
        Serial.print(getTimeString(currentTime));
        Serial.printf(" - Calculating %s", rollup ? "rollup " : "");
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
            computeAvg(measure, pm25Sum, pm10Sum, outTempSum, outRhSum, inTempSum, inRhSum, pm25Counter, pm10Counter,
                       outTempCounter, outRhCounter, inTempCounter, inRhCounter, windowCounter, measurementsCounter,
                       minTemp, maxTemp);
        }
    }
    if (DEBUG_CASE2) {
        Serial.println();
    }
    Measure result;

    time_t lastTime = NULL_MEASURE_VALUE;
    if (seconds == period15m) {
        if (rollup) {
            lastTime = lastMinAvg;
        } else {
            lastMinAvg = currentTime - seconds;
        }
    } else if (seconds == period1h) {
        if (rollup) {
            lastTime = lastHourAvg;
        } else {
            lastHourAvg = currentTime - seconds;
        }
    }

    bool window = outTempCounter <= 0 ? true : ((float) windowCounter / (float) outTempCounter > 0.5);
    if (measurementsCounter != 0) {
        result = {
                rollup ? (lastTime != NULL_MEASURE_VALUE ? lastTime + seconds : currentTime) : currentTime - seconds,
                averagePeriods(pm25Sum, pm25Counter, targetPeriods),
                averagePeriods(pm10Sum, pm10Counter, targetPeriods),
                averagePeriods(outTempSum, outTempCounter, targetPeriods),
                minTemp,
                maxTemp,
                averagePeriods(outRhSum, outRhCounter, targetPeriods),
                averagePeriods(inTempSum, inTempCounter, targetPeriods),
                averagePeriods(inRhSum, inRhCounter, targetPeriods),
                window
        };
    } else {
        result = nullMeasure;
    }

    if (isNullMeasure(result)) {
        result = nullMeasure;
    }

    if (window) {
        strcpy(result.serviceInfo, "(W)");
    } else {
        strcpy(result.serviceInfo, "(R)");
    }

    if (DEBUG_CASE2) {

        if (!rollup && seconds == period15m) {
            numberOf15mAveraged += measurementsCounter;
        } else {
            numberOf1hAveraged += measurementsCounter;
        }
        Serial.print("There were ");
        Serial.print(measurementsCounter);
        Serial.print(" elements averaged. Total averaged: ");
        Serial.print(seconds == period15m ? numberOf15mAveraged : numberOf1hAveraged);
        Serial.print(". Total placed: ");
        Serial.println(seconds == period15m ? numberOf15mMsrPlaced : numberOf1hMsrPlaced);
        if (!isNullMeasure(result)) {
            Serial.print("Averaged measure: ");
            Serial.println(measureToString(result));
        }
        Serial.println();
    }

    return result;
}

void connectToWifi() {
    String passPhrase = "ekvator<the>best";
    /*IPAddress staticIp(192,168,1,80);
    IPAddress gateWay(192,168,1,1);
    IPAddress subnet(255,255,255,0);
    IPAddress dns(8,8,8,8);
    WiFi.config(staticIp, gateWay, subnet, dns, gateWay);*/
    WiFi.begin("ALEKSNET-2", passPhrase);
    int retries = 0;
    while (WiFi.status() != WL_CONNECTED && retries < WIFI_MAX_RETRIES) {
        delay(500);
        Serial.print("Waiting for connection to ");
        Serial.println("ALEKSNET-2");
        retries++;
    }
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("Couldn't connect to WiFi");
    }
}

void connectToMqtt() {
    int retries = 0;
    mqttClient.setServer(MQTT_SERVER, MQTT_PORT);
    while (!mqttClient.connected() && retries < MQTT_MAX_RETRIES) {
        retries++;
        Serial.println("Connecting to MQTT...");
        if (mqttClient.connect(MQTT_SUBSCRIBER.c_str(), MQTT_USER, MQTT_PASSWORD)) {
            Serial.print("Connected to ");
            Serial.print(MQTT_SERVER);
            Serial.print(":");
            Serial.println(MQTT_PORT);
        }
    }
    if (!mqttClient.connected()) {
        Serial.print("Connection to ");
        Serial.print(MQTT_SERVER);
        Serial.print(":");
        Serial.print(MQTT_PORT);
        Serial.printf("failed after %d attempts, last error state ", WIFI_MAX_RETRIES);
        Serial.println(mqttClient.state());
        delay(500);

    }
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
        setTime((time_t) a.toInt() + UTC_HOURS_SHIFT * 3600);
        Serial.print("Time synced. Current time is ");
        Serial.println(timeToString(now()));
        http.end();   //Close connection
    } else {
        Serial.print("Time syncing failed. Server respond with next code: ");
        Serial.println(httpCode);
    }
}

/*void sendChunkedContent(MeasureType measureType, ContentType contentType, boolean eeprom) {
    static const char br[5] = "<br>";
    static char cont[MAX_MEASURES_STRING_LENGTH * HTTP_RESPONSE_CHUNKS_SIZE] = "";
    Measure *currMeasures;
    Measure rollupMeasure = nullMeasure;
    int measuresSize;
    switch (measureType) {
        case INSTANT:
            currMeasures = instantMeasures;
            measuresSize = INSTANT_MEASURES_NUMBER;
            break;
        case MINUTELY:
            currMeasures = every15minutesMeasures;
            rollupMeasure = minuteRollupAveragedMeasure;
            measuresSize = MINUTES_AVG_MEASURES_NUMBER;
            break;
        case HOURLY:
            currMeasures = hourlyMeasures;
            rollupMeasure = hourlyRollupAveragedMeasure;
            measuresSize = HOURLY_AVG_MEASURES_NUMBER;
            break;
        case DAILY:
            rollupMeasure = dailyRollupAveragedMeasure;
            static Measure readMeasures[EEPROM_DAILY_STORED_MEASURES_NUMBER];
            for (auto &reading : readMeasures) {
                reading = nullMeasure;
            }
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
                *//*Serial.print("Trying to get a daily measure from the next address: ");
                Serial.print(address);
                Serial.print(", index was: ");
                Serial.println(currentIndex - 1);*//*

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
                if (!isNullMeasure(measure1)) {
                    readMeasures[i] = measure1;
                    Serial.println(measureToString(measure));
                }
            }
            Serial.println();
            Serial.println();
            currMeasures = readMeasures;
            break;
        default:
            currMeasures = instantMeasures;
            rollupMeasure = nullMeasure;
    }

    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server.send(200, "text/html", "");
    int currentMeasureIndex = measuresSize - 1;
//    Serial.printf("Measures to send size: %d\n", measuresSize);
    boolean anyDataHasBeenSent = false;

    do {
        boolean thereIsData = false;
        strcpy(cont, contentType == CSV && !anyDataHasBeenSent ? csvHeader : "");
        if (measureType != INSTANT && !anyDataHasBeenSent && !isNullMeasure(rollupMeasure)) {
            strcat(cont, contentType == CSV ? measureToCsvString(rollupMeasure) : measureToString(rollupMeasure, true));
            if (contentType == HTML) {
                strcat(cont, br);
            }
            thereIsData = anyDataHasBeenSent = true;
        }
        for (int i = 0; i < HTTP_RESPONSE_CHUNKS_SIZE && currentMeasureIndex >= 0; i++) {
            if (!isNullMeasure(currMeasures[currentMeasureIndex])) {
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
}*/

void clearDailyEeprom() {
    int currentIndex = 0;

    for (; currentIndex < EEPROM_DAILY_STORED_MEASURES_NUMBER; currentIndex++) {
        size_t address = EEPROM_DAILY_MEASURES_OFFSET + currentIndex * SIMPLE_MEASURE_SIZE;
        Serial.print("Writing a daily measure to a next address: ");
        Serial.println(address);
        EEPROMr.put(address, nullSMeasure);
    }

    EEPROMr.commit();
}

void clearHourlyEeprom() {
    int currentIndex = 0;

    for (; currentIndex < EEPROM_HOURLY_STORED_MEASURES_NUMBER; currentIndex++) {
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
        clearDailyEeprom();
        clearHourlyEeprom();
        Measure measure = {
                makeTime({0, 0, 0, 1, 9, 8, static_cast<uint8_t>(2020 - 1970)}),
                570, 840, 2510,
                1780, 3240, 5600,
                2330, 4200, false, ""
        };
        placeDailyMeasureToEeprom(measure);
        server.send(200, "text/html", "OK");
    });

    /*server.on("/instant", []() {
        sendChunkedContent(INSTANT, HTML, false);
    });

    server.on("/avg/minute", []() {
        sendChunkedContent(MINUTELY, HTML, false);
    });

    server.on("/avg/hourly", []() {
        sendChunkedContent(HOURLY, HTML, false);
    });

    server.on("/avg/daily", []() {
        sendChunkedContent(DAILY, HTML, true);
    });

    server.on("/csv/1", []() {
        sendChunkedContent(INSTANT, CSV, false);
    });

    server.on("/csv/15", []() {
        sendChunkedContent(MINUTELY, CSV, false);
    });

    server.on("/csv/60", []() {
        sendChunkedContent(HOURLY, CSV, false);
    });*/

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
        server.send(200, "text/plain", timeToString(now()));
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
        snprintf(parameters, 250,
                 "measuringDuration = %d, sleepingPeriod = %d, 15avgPeriod = %d, 1hPeriod = %d, currentTime = %s",
                 measuringDuration, sleepingPeriod, period15m, period1h, timeToString(now()));
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
    dht22TerraceIn.begin();
    Serial.println("DHTxx Unified Sensor Example");
    // Print temperature sensor details.
    sensor_t sensor;
    dht21Roof.temperature().getSensor(&sensor);
    Serial.println("------------------------------------");
    Serial.println("Temperature");
    Serial.print("Sensor:       ");
    Serial.println(sensor.name);
    Serial.print("Driver Ver:   ");
    Serial.println(sensor.version);
    Serial.print("Unique ID:    ");
    Serial.println(sensor.sensor_id);
    Serial.print("Max Value:    ");
    Serial.print(sensor.max_value);
    Serial.println(" *C");
    Serial.print("Min Value:    ");
    Serial.print(sensor.min_value);
    Serial.println(" *C");
    Serial.print("Resolution:   ");
    Serial.print(sensor.resolution);
    Serial.println(" *C");
    Serial.println("------------------------------------");
    // Print humidity sensor details.
    dht21Roof.humidity().getSensor(&sensor);
    Serial.println("------------------------------------");
    Serial.println("Humidity");
    Serial.print("Sensor:       ");
    Serial.println(sensor.name);
    Serial.print("Driver Ver:   ");
    Serial.println(sensor.version);
    Serial.print("Unique ID:    ");
    Serial.println(sensor.sensor_id);
    Serial.print("Max Value:    ");
    Serial.print(sensor.max_value);
    Serial.println("%");
    Serial.print("Min Value:    ");
    Serial.print(sensor.min_value);
    Serial.println("%");
    Serial.print("Resolution:   ");
    Serial.print(sensor.resolution);
    Serial.println("%");
    Serial.println("------------------------------------");

    measuringDuration = DEFAULT_MEASURING_DURATION_MILLIS;
    sleepingPeriod = DEFAULT_SLEEPING_PERIOD_MILLIS;
    period15m = MINUTE_AVERAGE_PERIOD_SEC;
    period1h = HOUR_AVERAGE_PERIOD_SEC;
    period1d = DAILY_AVERAGE_PERIOD_SEC;
    MQTT_SUBSCRIBER = "ESP8266-" + WiFi.macAddress();
    Serial.printf("measuringDuration: %d, sleepingPeriod: %d, period15m: %ld, period1h: %ld, period1d: %ld",
                  measuringDuration, sleepingPeriod, period15m, period1h, period1d);

    sds.setQueryReportingMode();
    sds.setCustomWorkingPeriod(0);
    sds.wakeup();

    Serial.println(sds.queryFirmwareVersion().toString()); // prints firmware version
    Serial.println(sds.setQueryReportingMode().toString()); // ensures sensor is in 'query' reporting mode

    const char *ssid = "ALEKSNET-ROOF";
    connectToWifi();
    connectToMqtt();

    // Port defaults to 8266
    // ArduinoOTA.setPort(8266);

    // Hostname defaults to esp8266-[ChipID]
    // ArduinoOTA.setHostname("myesp8266");

    // No authentication by default
    // ArduinoOTA.setPassword((const char *)"123");

    ArduinoOTA.onStart([]() {
        Serial.println("Start");
    });
    ArduinoOTA.onEnd([]() {
        Serial.println("\nEnd");
    });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
    });
    ArduinoOTA.onError([](ota_error_t error) {
        Serial.printf("Error[%u]: ", error);
        if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
        else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
        else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
        else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
        else if (error == OTA_END_ERROR) Serial.println("End Failed");
    });
    ArduinoOTA.begin();

    syncTime();

    Serial.println("");
    Serial.print("Connected to ");
    Serial.println(ssid);
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());

    if (mdns.begin("esp8266", WiFi.localIP())) {
        Serial.println("MDNS responder started");
    }

    configureHttpServer();

    resetTimer();

    EEPROMr.size(4);
    EEPROMr.begin(4 * 1024);

    /*for (auto &reading : instantMeasures) {
        reading = nullMeasure;
    }
    for (auto &reading : every15minutesMeasures) {
        reading = nullMeasure;
    }*/

    /*int currentHourlyIndex;
    EEPROMr.get(EEPROM_HOURLY_CURSOR_POSITION_ADDRESS, currentHourlyIndex);
    if (currentHourlyIndex > EEPROM_HOURLY_STORED_MEASURES_NUMBER - 1 || currentHourlyIndex < 0) {
        currentHourlyIndex = 0;
    }

    int currentDailyIndex;
    EEPROMr.get(EEPROM_DAILY_CURSOR_POSITION_ADDRESS, currentDailyIndex);
    if (currentDailyIndex > EEPROM_HOURLY_STORED_MEASURES_NUMBER - 1 || currentDailyIndex < 0) {
        currentDailyIndex = 0;
    }*/

    /*Serial.printf("Hourly EEPROM size: %d, byte range: %d - %d, current index: %d, current byte address: %d\n",
                  MEASURE_SIZE,
                  EEPROM_HOURLY_MEASURES_OFFSET,
                  EEPROM_HOURLY_MEASURES_OFFSET + EEPROM_HOURLY_STORED_MEASURES_NUMBER * MEASURE_SIZE,
                  currentHourlyIndex, currentHourlyIndex * MEASURE_SIZE
    );

    Serial.printf("Daily EEPROM size: %d byte range: %d - %d, current index: %d, current byte address: %d\n",
                  SIMPLE_MEASURE_SIZE,
                  EEPROM_DAILY_MEASURES_OFFSET,
                  EEPROM_DAILY_MEASURES_OFFSET + EEPROM_DAILY_STORED_MEASURES_NUMBER * SIMPLE_MEASURE_SIZE,
                  currentDailyIndex, currentDailyIndex * SIMPLE_MEASURE_SIZE
    );


    Serial.println("Populating an initial collection with the hourly measures from EEPROM ...");*/
    //CONSTRAINT:  HOURLY_AVG_MEASURES_NUMBER MUST BE GREATER THAN EEPROM_HOURLY_STORED_MEASURES_NUMBER
    /*for (int i1 = 0, i2 = HOURLY_AVG_MEASURES_NUMBER - EEPROM_HOURLY_STORED_MEASURES_NUMBER;
         i1 < EEPROM_HOURLY_STORED_MEASURES_NUMBER; i1++, i2++) {
        if (currentHourlyIndex > EEPROM_HOURLY_STORED_MEASURES_NUMBER - 1) {
            currentHourlyIndex = 0;
        }
        Measure measure;
        size_t address = EEPROM_HOURLY_MEASURES_OFFSET + currentHourlyIndex++ * MEASURE_SIZE;
        Serial.print(i1);
        Serial.print(". Cursor position: ");
        Serial.print(currentHourlyIndex - 1);
        Serial.print(", byte address: ");
        Serial.print(address);

        EEPROMr.get(address, measure);
        hourlyMeasures[i2] = measure;
        Serial.print(", loaded measure: ");
        Serial.println(measureToString(measure));
    }*/
    /*Serial.println();
    hourlyMeasuresFirstPass = false;
    hourlyMeasuresIndex = EEPROM_HOURLY_STORED_MEASURES_NUMBER;

    Serial.println("Setting null to hourlyMeasures");
    for (int i = 0; i < HOURLY_AVG_MEASURES_NUMBER - EEPROM_HOURLY_STORED_MEASURES_NUMBER; i++) {
        Serial.print(i);
        Serial.print(",");
        hourlyMeasures[i] = nullMeasure;
    }
    Serial.println();*/

    if (DEBUG) {
        Serial.print(getTimeString(now()));
        Serial.println(" - The sensor should be woken now");
    }
}

void loop() {

    delay(20);

    time_t currentTime = now();
    byte currentMinute = minute(currentTime);
    byte currentHour = hour(currentTime);
    byte currentDay = day(currentTime);
    int currentYear = year(currentTime);

    /*if (currentMinute % (period15m / 60) == 0 && currentMinute != lastMinutesAverageMinute) {
        lastMinutesAverageMinute = currentMinute;
        const Measure &measure =
                calculateAverage(currentTime, MINUTELY, instantMeasures, INSTANT_MEASURES_NUMBER, false);
        placeMeasure(measure, MINUTELY);
    }

    if (currentHour % (period1h / 3600) == 0 && currentHour != last1HourAverageHour) {
        last1HourAverageHour = currentHour;
        const Measure &measure =
                calculateAverage(currentTime, HOURLY, every15minutesMeasures, MINUTES_AVG_MEASURES_NUMBER, false);
        placeMeasure(measure, HOURLY);
    }

    if (currentDay != last1DayAverageDay) {
        last1DayAverageDay = currentDay;
        const Measure &measure =
                calculateAverage(currentTime, DAILY, hourlyMeasures, HOURLY_AVG_MEASURES_NUMBER, false);
        placeMeasure(measure, DAILY);
    }*/

    ArduinoOTA.handle();
    server.handleClient();
    mqttClient.loop();

    //  Step 1 - Measuring
    if (millis() - currentTimeMillisTimer > measuringDuration && step == 1) {

        currentTimeMillisTimer = millis();

        if (WiFi.status() != WL_CONNECTED) {
            connectToWifi();
        }

        if (!mqttClient.connected()) {
            connectToMqtt();
        }

        PmResult pm = sds.queryPm();
        signed short int pm25 = NULL_MEASURE_VALUE;
        signed short int pm10 = NULL_MEASURE_VALUE;
        if (pm.isOk()) {
            int round3 = round(pm.pm25 * 100);
            int round4 = round(pm.pm10 * 100);
            pm25 = round3 > SIGNED_SHORT_MAX_VALUE ? SIGNED_SHORT_MAX_VALUE : round3;
            pm10 = round4 > SIGNED_SHORT_MAX_VALUE ? SIGNED_SHORT_MAX_VALUE : round4;
        }

        sensors_event_t outTerraceTempEvent, outTerraceHumidEvent, windowTempEvent,
        windowHumidEvent, livingRoomTempEvent, livingRoomHumidEvent, terraceInTempEvent, terraceInHumidEvent;

        dht21Roof.temperature().getEvent(&outTerraceTempEvent);
        dht21Roof.humidity().getEvent(&outTerraceHumidEvent);

        dht21Window.temperature().getEvent(&windowTempEvent);
        dht21Window.humidity().getEvent(&windowHumidEvent);

        dht22LivingRoom.temperature().getEvent(&livingRoomTempEvent);
        dht22LivingRoom.humidity().getEvent(&livingRoomHumidEvent);

        dht22TerraceIn.temperature().getEvent(&terraceInTempEvent);
        dht22TerraceIn.humidity().getEvent(&terraceInHumidEvent);

        signed short int roofTemp = isnan(outTerraceTempEvent.temperature) ? NULL_MEASURE_VALUE : round(
                outTerraceTempEvent.temperature * 100);
        signed short int roofHumid = isnan(outTerraceHumidEvent.relative_humidity) ? NULL_MEASURE_VALUE : round(
                outTerraceHumidEvent.relative_humidity * 100);
        signed short int windowTemp = isnan(windowTempEvent.temperature) ? NULL_MEASURE_VALUE : round(
                windowTempEvent.temperature * 100);
        signed short int windowHumid = isnan(windowHumidEvent.relative_humidity) ? NULL_MEASURE_VALUE : round(
                windowHumidEvent.relative_humidity * 100);
        float livRoomTemp = livingRoomTempEvent.temperature;
        signed short int livingRoomTemp = isnan(livRoomTemp) ? NULL_MEASURE_VALUE : round(
                livRoomTemp * 100);
        float liveRoomHumid = livingRoomHumidEvent.relative_humidity;
        signed short int livingRoomHumid = isnan(liveRoomHumid) ? NULL_MEASURE_VALUE : round(
                liveRoomHumid * 100);

        char serviceInfo[17];

        bool windowTempIsLess =
                (windowTemp != NULL_MEASURE_VALUE && windowHumid != NULL_MEASURE_VALUE) && windowTemp < roofTemp;
        snprintf(serviceInfo, 17, "(%s)-(%.1fC/%.0f%%)",
                 windowTempIsLess ? "W" : "R",
                 windowTempIsLess ? roofTemp / 100.0 : windowTemp / 100.0,
                 windowHumid / 100.0
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
        static char measureString[MAX_MEASURES_STRING_LENGTH];
        snprintf(measureString, MAX_MEASURES_STRING_LENGTH,
                 "%s,pm25=%.1f,pm10=%.1f,roofT=%.1f,roofRh=%.0f,roofAh=%.1f,windT=%.1f,windRh=%.0f,windAh=%.1f,livRoomT=%.1f,livRoomRh=%.0f,livRoomAh=%.1f",
                 getTimeString(currentMeasure.measureTime),
                 round1(currentMeasure.pm25 / 100.0),
                 round1(currentMeasure.pm10 / 100.0),
                 outTerraceTempEvent.temperature,
                 outTerraceHumidEvent.relative_humidity,
                 round2(calculateAbsoluteHumidity(outTerraceTempEvent.temperature, outTerraceHumidEvent.relative_humidity)),
                 windowTempEvent.temperature,
                 windowHumidEvent.relative_humidity,
                 round2(calculateAbsoluteHumidity(windowTempEvent.temperature, windowHumidEvent.relative_humidity)),
                 livRoomTemp,
                 liveRoomHumid,
                 round2(calculateAbsoluteHumidity(livRoomTemp, liveRoomHumid))
        );
        Serial.println(measureString);
        mqttClient.publish(MQTT_TOPIC, measureString, false);

        float terraceInTemp = terraceInTempEvent.temperature;
        float terraceInHumid = terraceInHumidEvent.relative_humidity;
        float inTerraceAh = calculateAbsoluteHumidity(terraceInTemp, terraceInHumid);

        snprintf(measureString, MAX_MEASURES_STRING_LENGTH,
                 R"({"messageIssued": "%s", "publisherId": "%s", "measurePlace": "IN-TERRACE", "air": {"temp": {"celsius": %s, "rh": %s, "ah": %s}}})",
                 getIsoTimeString(now()),
                 MQTT_SUBSCRIBER.c_str(),
                 isnan(terraceInTemp) ? "null" : String(round1(terraceInTemp)).c_str(),
                 isnan(terraceInHumid) ? "null" : String(round1(terraceInHumid)).c_str(),
                 isnan(inTerraceAh) ? "null" : String(inTerraceAh).c_str()
        );
        Serial.println(measureString);
        mqttClient.publish(MQTT_TOPIC, measureString, false);

        char *isoTimeString = getIsoTimeString(currentMeasure.measureTime);
        snprintf(measureString, MAX_MEASURES_STRING_LENGTH,
                 R"({"messageIssued": "%s", "publisherId": "%s", "measurePlace": "OUT-TERRACE", "air": {"quality": {"pm25": %.1f, "pm10": %.1f}, "temp": {"celsius": %s, "rh": %s, "ah": %s}}})",
                 isoTimeString,
                 MQTT_SUBSCRIBER.c_str(),
                 round1(currentMeasure.pm25 / 100.0),
                 round1(currentMeasure.pm10 / 100.0),
                 isnan(outTerraceTempEvent.temperature) ? "null" : String(round1(outTerraceTempEvent.temperature)).c_str(),
                 isnan(outTerraceHumidEvent.relative_humidity) ? "null" : String(round1(outTerraceHumidEvent.relative_humidity)).c_str(),
                 isnan(calculateAbsoluteHumidity(outTerraceTempEvent.temperature, outTerraceHumidEvent.relative_humidity)) ? "null" :
                 String(calculateAbsoluteHumidity(outTerraceTempEvent.temperature, outTerraceHumidEvent.relative_humidity)).c_str()
        );
        Serial.println(measureString);
        mqttClient.publish(MQTT_TOPIC, measureString, false);

        snprintf(measureString, MAX_MEASURES_STRING_LENGTH,
                 R"({"messageIssued": "%s", "publisherId": "%s", "measurePlace": "IN-LIVING-ROOM", "air": {"temp": {"celsius": %s, "rh": %s, "ah": %s}}})",
                 isoTimeString,
                 MQTT_SUBSCRIBER.c_str(),
                 isnan(livRoomTemp) ? "null" : String(round1(livRoomTemp)).c_str(),
                 isnan(liveRoomHumid) ? "null" : String(round1(liveRoomHumid)).c_str(),
                 isnan(calculateAbsoluteHumidity(livRoomTemp, liveRoomHumid)) ? "null" :
                 String(calculateAbsoluteHumidity(livRoomTemp, liveRoomHumid)).c_str()
        );
        Serial.println(measureString);
        mqttClient.publish(MQTT_TOPIC, measureString, false);
        
        
//        placeMeasure(currentMeasure, INSTANT);

        if (DEBUG_CASE2) {
            Serial.print(numberOfEveryMsrPlaced);
            Serial.print(". Got a measure: ");
            printMeasure(currentMeasure);
        }

        /*minuteRollupAveragedMeasure = calculateAverage(currentTime, MINUTELY, instantMeasures, INSTANT_MEASURES_NUMBER,
                                                       true);
        hourlyRollupAveragedMeasure = calculateAverage(currentTime, HOURLY, every15minutesMeasures,
                                                       MINUTES_AVG_MEASURES_NUMBER, true);
        dailyRollupAveragedMeasure = calculateAverage(currentTime, DAILY, hourlyMeasures, HOURLY_AVG_MEASURES_NUMBER,
                                                      true);*/

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
        if (logCounter % 60 == 0 && DEBUG_CASE2) {
            Serial.print("Max measuring time: ");
            Serial.println(maxMeasuringTime);
        }

        if (currentYear < 2020) {
            syncTime();
        }
    }

    //  Step 2 - Waking up an SDS sensor after a sleeping period if any
    if (sleepingPeriod > 0 && millis() - currentTimeMillisTimer > sleepingPeriod && step == 2) {
        currentTimeMillisTimer = millis();
        step = 1;
        sds.wakeup();
    }
}
