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

const char TIME_API_URL[] = "http://worldtimeapi.org/api/timezone/Europe/Kiev.txt";
const boolean DEBUG = false;
const int FAN_DURATION_SEC = 240;
const int FAN_ENGAGEMENT_THRESHOLD_SEC = 120;
static const int TEMP_SENSOR_PIN = 0;
static const int LIGHT_PIN = 4;
static const int RELAY_PIN = 5;
static const int BUZZER_PIN = 15;
boolean lightState = false;
boolean fanState = false;
boolean fanEngagedMsg = false;
unsigned long lastLightSwitchTimestamp;
unsigned long fanTimer;
boolean continuousMode = false;
boolean continuousFanState = false;
unsigned long continuousModeEnabledTimer = 0;
unsigned long lastQuickshiftTimestamp = 0;
const unsigned long continuousModeDuration = 40000;
unsigned long logTimer;
int lastMqttSendMinute = -1;
DHT_Unified dht22LivingRoom(TEMP_SENSOR_PIN, DHT22);
WiFiClient wclient;
PubSubClient mqttClient(wclient);

void beep();

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
    delay(20);
    Serial.begin(115200);
    dht22LivingRoom.begin();
    pinMode(RELAY_PIN, OUTPUT);
    pinMode(BUZZER_PIN, OUTPUT);
    connectToWifi();
    connectToMqtt();
    if (WiFi.status() == WL_CONNECTED) {
        syncTime();
    }
    lastLightSwitchTimestamp = millis();
    logTimer = millis();
    lightState = !digitalRead(LIGHT_PIN);
    Serial.print(millis());
    Serial.print(" - The initial light state was (");
    Serial.print(lightState);
    Serial.println(lightState ? ") on" : ") off");
    fanState = false;
}

void turnOnTheFan() {
    fanState = true;
    fanTimer = millis();
}

void turnOffTheFan() {
    fanState = false;
    fanEngagedMsg = false;
    lastQuickshiftTimestamp = millis();
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
        lastLightSwitchTimestamp = millis();
    } else {
        Serial.print(millis());
        Serial.print(" - Light is off, duration ");
        Serial.print(currentLightStateDuration);
        Serial.println(" ms.");
        lastLightSwitchTimestamp = millis();
        if (currentLightStateDuration / 1000 > FAN_ENGAGEMENT_THRESHOLD_SEC) {
            turnOnTheFan();
        }
        fanTimer = millis();
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

void activateContinuousMode() {
    continuousModeEnabledTimer = millis();
    continuousMode = true;
}

void loop() {

    time_t currTime = now();
    int currentSecond = second(currTime);
    int currentMinute = minute(currTime);

    if (Serial.available() > 0 && Serial.parseInt() == 5) {
        activateContinuousMode();
        Serial.print(millis());
        Serial.println(" - CONT MODE ACTIVATED");
    }

    unsigned long sinceLastLightSwitchTimestamp = millis() - lastLightSwitchTimestamp;
    boolean newLightState = !digitalRead(LIGHT_PIN);

    if (!fanState && lightState && !fanEngagedMsg && millisToSeconds(sinceLastLightSwitchTimestamp) > FAN_ENGAGEMENT_THRESHOLD_SEC) {
        issueFanArmingMessage();
    }

    if (newLightState != lightState) {
        delay(500);
        newLightState = !digitalRead(LIGHT_PIN);
        unsigned long sinceLastQuickshiftMillis = millis() - lastQuickshiftTimestamp;
        if (newLightState == lightState && (sinceLastQuickshiftMillis > 3000 || sinceLastQuickshiftMillis < 0)) {
            Serial.print(millis());
            Serial.println(" - QUICKSHIFT!");
            longBeep();
            if (fanState) {
                turnOffTheFan();
            } else {
                turnOnTheFan();
            }
            lastQuickshiftTimestamp = millis();
        } else {
            changeLightState(newLightState, sinceLastLightSwitchTimestamp);
        }
    }

    if ((fanState && !lightState && (millis() - fanTimer) / 1000 > FAN_DURATION_SEC) || (millis() - fanTimer) / 1000 < 0) {
        // After switching the light off, fan must be switched off upon timeout
        turnOffTheFan();
    }

    if (!fanState && !lightState && continuousMode && millis() - continuousModeEnabledTimer < continuousModeDuration) {
        // If continuous mode enabled, turn on the fan by a schedule
        continuousFanState = currentSecond % 5 == 0;
    } else if (continuousMode && (millis() - continuousModeEnabledTimer >= continuousModeDuration || millis() - continuousModeEnabledTimer < 0)) {
        // Disable continuous mode
        continuousMode = false;
        continuousFanState = false;
        continuousModeEnabledTimer = 0;
        Serial.print(millis());
        Serial.println(" - CONT MODE DEACTIVATED");
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

        sensors_event_t livingRoomTempEvent;
        sensors_event_t livingRoomHumidEvent;
        dht22LivingRoom.temperature().getEvent(&livingRoomTempEvent);
        dht22LivingRoom.humidity().getEvent(&livingRoomHumidEvent);
        static char measureString[100];
        snprintf(measureString, 100,
                 "%s,wcT=%.1f,wcRh=%.0f",
                 getTimeString(currTime),
                 isnan(livingRoomTempEvent.temperature) ? -100 : livingRoomTempEvent.temperature,
                 isnan(livingRoomHumidEvent.relative_humidity) ? -100 : livingRoomHumidEvent.relative_humidity
                 );
        
        mqttClient.publish(MQTT_TOPIC, measureString);
    }

    // Set the relay position
    digitalWrite(RELAY_PIN, fanState || continuousFanState);

}