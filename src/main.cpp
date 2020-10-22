#include "../lib/BSEC-Arduino-library-master/src/bsec.h"
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

// Helper functions declarations
void checkIaqSensorStatus(void);
void errLeds(void);

// Create an object of the class Bsec
Bsec iaqSensor;

String output;

#define MQTT_TOPIC "ALEXKZK-SMARTHOUSE"
#define MQTT_SERVER "185.212.128.246"
#define MQTT_SUBSCRIBER "ESP8266-SmartHouse-BME680"
#define MQTT_USER "alexkzk"
#define MQTT_PASSWORD "Vlena<G13"
#define MQTT_PORT 1883
#define MQTT_MAX_RETRIES 1
#define WIFI_MAX_RETRIES 20
#define MAX_MEASURES_STRING_LENGTH 170

const char TIME_API_URL[] = "http://worldtimeapi.org/api/timezone/Europe/Kiev.txt";

WiFiClient wclient;
PubSubClient mqttClient(wclient);

void connectToWifi() {
    String passPhrase = "ekvatorthebest";
    WiFi.begin("ALEKSNET-2", "ekvator<the>best");
    int retries = 0;
    while (WiFi.status() != WL_CONNECTED && retries < WIFI_MAX_RETRIES) {
        delay(500);
        Serial.print("Waiting for connection to ");
        Serial.println("ALEKSNET-2");
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
        WiFi.begin("ALEKSNET-ROOF", passPhrase);
        while (WiFi.status() != WL_CONNECTED && retries < WIFI_MAX_RETRIES) {
            delay(500);
            Serial.print("Waiting for connection to ");
            Serial.println("ALEKSNET-ROOF");
            retries++;
        }
    }
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("Could not connect to any WiFi");
    }
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
        Serial.println(timeToString(now()));
        http.end();   //Close connection
    } else {
        Serial.print("Time syncing failed. Server respond with next code: ");
        Serial.println(httpCode);
    }
}

void connectToMqtt() {
    int retries = 0;
    mqttClient.setServer(MQTT_SERVER, MQTT_PORT);
    while (!mqttClient.connected() && retries < MQTT_MAX_RETRIES) {
        retries++;
        Serial.println("Connecting to MQTT...");
        if (mqttClient.connect(MQTT_SUBSCRIBER, MQTT_USER, MQTT_PASSWORD)) {
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

char* getTimeString(time_t time) {
    static char measureString[20];
    snprintf(measureString, 20, "%02d/%02d/%d %02d:%02d:%02d", day(time), month(time), year(time), hour(time), minute(time), second(time));
    return measureString;
}

// Entry point for the example
void setup(void)
{
    Serial.begin(115200);
    Wire.begin();

    connectToWifi();
    connectToMqtt();

    iaqSensor.begin(BME680_I2C_ADDR_PRIMARY, Wire);
    output = "\nBSEC library version " + String(iaqSensor.version.major) + "." + String(iaqSensor.version.minor) + "." + String(iaqSensor.version.major_bugfix) + "." + String(iaqSensor.version.minor_bugfix);
    Serial.println(output);
    checkIaqSensorStatus();

    bsec_virtual_sensor_t sensorList[10] = {
            BSEC_OUTPUT_RAW_TEMPERATURE,
            BSEC_OUTPUT_RAW_PRESSURE,
            BSEC_OUTPUT_RAW_HUMIDITY,
            BSEC_OUTPUT_RAW_GAS,
            BSEC_OUTPUT_IAQ,
            BSEC_OUTPUT_STATIC_IAQ,
            BSEC_OUTPUT_CO2_EQUIVALENT,
            BSEC_OUTPUT_BREATH_VOC_EQUIVALENT,
            BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_TEMPERATURE,
            BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_HUMIDITY,
    };

    iaqSensor.updateSubscription(sensorList, 10, BSEC_SAMPLE_RATE_LP);
    checkIaqSensorStatus();

    // Print the header
    output = "Timestamp [ms], raw temperature [°C], pressure [hPa], raw relative humidity [%], gas [Ohm], IAQ, IAQ accuracy, temperature [°C], relative humidity [%], Static IAQ, CO2 equivalent, breath VOC equivalent";
    Serial.println(output);
}

// Function that is looped forever
void loop(void)
{
//    delay(20);
//    if(second(now()) % 10 == 0) {
        unsigned long time_trigger = millis();
        if (iaqSensor.run()) { // If new data is available


            if (WiFi.status() != WL_CONNECTED) {
                connectToWifi();
            }

            if (!mqttClient.connected()) {
                connectToMqtt();
            }

            output = String(time_trigger);
            output += ", " + String(iaqSensor.rawTemperature);
            output += ", " + String(iaqSensor.pressure);
            output += ", " + String(iaqSensor.rawHumidity);
            output += ", " + String(iaqSensor.gasResistance);
            output += ", " + String(iaqSensor.iaq);
            output += ", " + String(iaqSensor.iaqAccuracy);
            output += ", " + String(iaqSensor.temperature);
            output += ", " + String(iaqSensor.humidity);
            output += ", " + String(iaqSensor.staticIaq);
            output += ", " + String(iaqSensor.co2Equivalent);
            output += ", " + String(iaqSensor.breathVocEquivalent);
            Serial.println(output);


            static char measureString[MAX_MEASURES_STRING_LENGTH];
            snprintf(measureString, MAX_MEASURES_STRING_LENGTH,
                     "%s,childrenTemp=%.1f,childrenHumid=%.1f,childrenIaq=%.2f,childrenCo2=%.0f,childrenVoc=%.2f",
                     getTimeString(now()),
                     iaqSensor.temperature,
                     iaqSensor.humidity,
                     iaqSensor.staticIaq,
                     iaqSensor.co2Equivalent,
                     iaqSensor.breathVocEquivalent
            );
            mqttClient.publish(MQTT_TOPIC, measureString, false);


        } else {
            checkIaqSensorStatus();
        }

//    }
}

// Helper function definitions
void checkIaqSensorStatus(void)
{
    if (iaqSensor.status != BSEC_OK) {
        if (iaqSensor.status < BSEC_OK) {
            output = "BSEC error code : " + String(iaqSensor.status);
            Serial.println(output);
            for (;;)
                errLeds(); /* Halt in case of failure */
        } else {
            output = "BSEC warning code : " + String(iaqSensor.status);
            Serial.println(output);
        }
    }

    if (iaqSensor.bme680Status != BME680_OK) {
        if (iaqSensor.bme680Status < BME680_OK) {
            output = "BME680 error code : " + String(iaqSensor.bme680Status);
            Serial.println(output);
            for (;;)
                errLeds(); /* Halt in case of failure */
        } else {
            output = "BME680 warning code : " + String(iaqSensor.bme680Status);
            Serial.println(output);
        }
    }
}

void errLeds(void)
{
    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, HIGH);
    delay(100);
    digitalWrite(LED_BUILTIN, LOW);
    delay(100);
}