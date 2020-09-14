#include <../lib/Time-master/TimeLib.h>
#include <Arduino.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <../lib/Adafruit_Sensor-master/Adafruit_Sensor.h>
#include <../lib/DHT-sensor-library-master/DHT.h>
#include <../lib/DHT-sensor-library-master/DHT_U.h>
#include <ESP8266HTTPClient.h>
#include <../lib/eeprom_rotate-master/src/EEPROM_Rotate.h>
#include <../lib/PubSubClient-2.8.0/src/PubSubClient.h>

#define WIFI_MAX_RETRIES 20
#define MQTT_MAX_RETRIES 1
#define MQTT_TOPIC "ALEXKZK-SMARTHOUSE"
#define MQTT_SERVER "185.212.128.246"
#define MQTT_SUBSCRIBER "ESP8266-SmartHouse-FanSwitcher"
#define MQTT_USER "alexkzk"
#define MQTT_PASSWORD "Vlena<G13"
#define MQTT_PORT 1883

// constants
const char TIME_API_URL[] = "http://worldtimeapi.org/api/timezone/Europe/Kiev.txt";
const boolean DEBUG = false;

// configuration properties
static const int CONTACT_BOUNCE_PREVENTION_THRESHOLD = 1000;
unsigned int fanServiceDurationMinutes = 4;
unsigned int fanEngagementThresholdMinutes = 2;
unsigned int continuousModeDurationMinutes = 180;

// pins definitions
static const int TEMP_SENSOR_PIN = 0;
static const int LIGHT_PIN = 4;
static const int RELAY_PIN = 5;
static const int BUZZER_PIN = 15;

//state flags
boolean lightState = false;
boolean fanState = false;
boolean continuousFanState = false;
boolean continuousMode = false;
boolean fanEngagedMsg = false;

//time stamps
unsigned long lastLightSwitchTs = 0;
unsigned long fanSwitchedOnTs = 0;
unsigned long contModeEnabledTs = 0;
unsigned long contactBouncePreventionTs = 0;
unsigned long logTimer = 0;

int lastMqttSendMinute = -1;
float lastTemp;
float lastRh;
float lastAh;
DHT_Unified dht22Washroom(TEMP_SENSOR_PIN, DHT22);
WiFiClient wclient;
PubSubClient mqttClient(wclient);
ESP8266WebServer server(8088);

void beep();

void disableContMode();

float calculateAbsoluteHumidity(float temp, float rh) {
    return 6.112 * pow(2.71828, 17.67 * temp / (243.5 + temp)) * rh * 2.1674 / (273.15 + temp);
}

float roundTo(float value, int accuracy){
    return round(value * accuracy) / accuracy;
}

char* getTimeString(time_t time) {
    static char measureString[20];
    snprintf(measureString, 20, "%02d/%02d/%d %02d:%02d:%02d", day(time), month(time), year(time), hour(time), minute(time), second(time));
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

void activateContMode() {
    contModeEnabledTs = millis();
    continuousMode = true;
}

void turnOnTheFan() {
    fanState = true;
    fanSwitchedOnTs = millis();
}

void turnOffTheFan() {
    fanState = false;
    fanEngagedMsg = false;
    contactBouncePreventionTs = millis();
}

void configureHttpServer() {

    server.on("/contOn", HTTP_POST, []() {
        activateContMode();
        server.send(200, "text/html", "OK");
    });

    server.on("/contOff", HTTP_POST, []() {
        disableContMode();
        server.send(200, "text/html", "OK");
    });

    server.on("/config", HTTP_POST, []() {
        char parameters[200];
        continuousModeDurationMinutes = server.arg("continuousModeDurationMinutes").toInt();
        fanEngagementThresholdMinutes = server.arg("fanEngagementThresholdMinutes").toInt();
        fanServiceDurationMinutes = server.arg("fanServiceDurationMinutes").toInt();
        disableContMode();
        turnOffTheFan();
        snprintf(parameters, 200, "continuousModeDurationMinutes = %d, fanEngagementThresholdMinutes = %d, fanServiceDurationMinutes = %d",
                 continuousModeDurationMinutes, fanEngagementThresholdMinutes, fanServiceDurationMinutes);
        server.send(200, "text/plain", String(parameters));
    });

    server.on("/config", []() {
        char parameters[500];
        snprintf(parameters, 500,
                "%s - continuousModeDurationMinutes = %d, fanEngagementThresholdMinutes = %d, fanServiceDurationMinutes = %d,"
                " continuousMode = %d, continuousFanState = %d, lightState = %d,"
                " fanState = %d, nowTs = %lu, contModeEnabledTs = %lu, fanSwitchedOnTs = %lu, lastLightSwitchTs = %lu,"
                " contModeRemain(min) = %lu",
                 getTimeString(now()), continuousModeDurationMinutes, fanEngagementThresholdMinutes, fanServiceDurationMinutes,
                 continuousMode, continuousFanState, lightState, fanState, millis(),
                 contModeEnabledTs, fanSwitchedOnTs, lastLightSwitchTs,
                 continuousMode ? continuousModeDurationMinutes - (millis() - contModeEnabledTs) / 1000 / 60 : 0
        );
        server.send(200, "text/plain", String(parameters));
    });

    server.begin();
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

void setup() {
    Serial.begin(115200);
    configureHttpServer();
    dht22Washroom.begin();
    pinMode(RELAY_PIN, OUTPUT);
    pinMode(BUZZER_PIN, OUTPUT);
    connectToWifi();
    connectToMqtt();
    if (WiFi.status() == WL_CONNECTED) {
        syncTime();
    }
    lastLightSwitchTs = millis();
    logTimer = millis();
    lightState = !digitalRead(LIGHT_PIN);
    Serial.print(millis());
    Serial.print(" - The initial light state was (");
    Serial.print(lightState);
    Serial.println(lightState ? ") on" : ") off");
    fanState = false;
}

void changeLightState(boolean newLightState, unsigned long currentLightStateDuration) {
    if (newLightState) {
        Serial.print(millis());
        Serial.print(" - Light is on, duration ");
        Serial.print(currentLightStateDuration);
        Serial.println(" ms.");
        if (continuousFanState) {
            continuousFanState = fanState;
        }
        lastLightSwitchTs = millis();
    } else {
        Serial.print(millis());
        Serial.print(" - Light is off, duration ");
        Serial.print(currentLightStateDuration);
        Serial.println(" ms.");
        lastLightSwitchTs = millis();
        if (currentLightStateDuration / 1000 / 60 > fanEngagementThresholdMinutes) {
            turnOnTheFan();
        }
        fanSwitchedOnTs = millis();
    }
    lightState = newLightState;
}

unsigned long millisToSeconds(unsigned long sinceLastSwitchTs) { return sinceLastSwitchTs / 1000; }

void issueFanArmingMessage() {
        fanEngagedMsg = true;
        Serial.print(millis());
        Serial.println(" - Fan armed ");
        beep();
}

void beep() {
    tone(BUZZER_PIN, 600);
    delay(50);
    tone(BUZZER_PIN, 900);
    delay(50);
    noTone(BUZZER_PIN);
}

void longBeep() {
    tone(BUZZER_PIN, 900);
    delay(50);
    tone(BUZZER_PIN, 600);
    delay(50);
    noTone(BUZZER_PIN);
}

void loop() {

    delay(20);
    server.handleClient();

    time_t currTime = now();
    int currentMinute = minute(currTime);

    if (Serial.available() > 0 && Serial.parseInt() == 5) {
        activateContMode();
        Serial.print(millis());
        Serial.println(" - CONT MODE ACTIVATED");
    }

    unsigned long sinceLastLightSwitchTimestamp = millis() - lastLightSwitchTs;
    boolean newLightState = !digitalRead(LIGHT_PIN);

    if (!fanState && lightState && !fanEngagedMsg && millisToSeconds(sinceLastLightSwitchTimestamp) > fanEngagementThresholdMinutes) {
        issueFanArmingMessage();
    }

    if (newLightState != lightState) {
        delay(500);
        newLightState = !digitalRead(LIGHT_PIN);
        unsigned long sinceLastQuickshiftMillis = millis() - contactBouncePreventionTs;
        if (newLightState == lightState && (sinceLastQuickshiftMillis > CONTACT_BOUNCE_PREVENTION_THRESHOLD || sinceLastQuickshiftMillis < 0)) {
            Serial.print(millis());
            Serial.println(" - QUICKSHIFT!");
            longBeep();
            if (fanState) {
                turnOffTheFan();
            } else {
                turnOnTheFan();
            }
            contactBouncePreventionTs = millis();
        } else {
            changeLightState(newLightState, sinceLastLightSwitchTimestamp);
        }
    }

    if ((fanState && !lightState && (millis() - fanSwitchedOnTs) / 1000 / 60 > fanServiceDurationMinutes)
        || (millis() - fanSwitchedOnTs) / 1000 < 0) {
        // After switching the light off, fan must be switched off upon timeout
        turnOffTheFan();
    }

    if (!fanState && !lightState && continuousMode && millis() - contModeEnabledTs < continuousModeDurationMinutes * 60 * 1000) {
        // If continuous mode enabled, turn on the fan by a schedule
        continuousFanState = currentMinute % 15 < fanServiceDurationMinutes;
    } else if (continuousMode && (millis() - contModeEnabledTs >= continuousModeDurationMinutes * 60 * 1000
        || millis() - contModeEnabledTs < 0)) {
        disableContMode();
    }

    if(DEBUG && millis() - logTimer > 500) {
        if (fanState || continuousFanState) {
            Serial.print(millis());
            Serial.print(" - Fan switched ON. LIGHT=");
            Serial.print(fanState);
            Serial.print("; CONT=");
            Serial.println(continuousFanState);
        } else {
            Serial.print(millis());
            Serial.print(" - Fan switched OFF. LIGHT=");
            Serial.print(fanState);
            Serial.print("; CONT=");
            Serial.println(continuousFanState);
        }
        logTimer = millis();
    }
    
    if (currentMinute != lastMqttSendMinute) {
        // Send mqtt message with temp and humid
        lastMqttSendMinute = currentMinute;

        if (WiFi.status() != WL_CONNECTED) {
            connectToWifi();
        }

        if (!mqttClient.connected()) {
            connectToMqtt();
        }

        sensors_event_t washroomTempEvent;
        sensors_event_t washroomHumidEvent;
        dht22Washroom.temperature().getEvent(&washroomTempEvent);
        dht22Washroom.humidity().getEvent(&washroomHumidEvent);
        static char measureString[100];
        snprintf(measureString, 100,
                 "%s,washroom1T=%.1f,washroom1Rh=%.0f,washroom1Ah=%.1f",
                 getTimeString(currTime),
                 isnan(washroomTempEvent.temperature) ? lastTemp = -100 : lastTemp = washroomTempEvent.temperature,
                 isnan(washroomHumidEvent.relative_humidity) ? lastRh = -100 : lastRh = washroomHumidEvent.relative_humidity,
                 isnan(washroomTempEvent.temperature) || isnan(washroomHumidEvent.relative_humidity) ? lastRh = -100 :
                         lastRh = roundTo(calculateAbsoluteHumidity(washroomTempEvent.temperature, washroomHumidEvent.relative_humidity), 1)
         );
        
        mqttClient.publish(MQTT_TOPIC, measureString);
    }

    // Set the relay position
    digitalWrite(RELAY_PIN, fanState || continuousFanState);

}

void disableContMode() {// Disable continuous mode
    continuousMode = false;
    continuousFanState = false;
    contModeEnabledTs = 0;
    Serial.print(millis());
    Serial.println(" - CONT MODE DEACTIVATED");
}
