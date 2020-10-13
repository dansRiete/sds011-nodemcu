#include <../lib/Time-master/TimeLib.h>
#include <Arduino.h>

// constants
const char TIME_API_URL[] = "http://worldtimeapi.org/api/timezone/Europe/Kiev.txt";
const boolean DEBUG = false;

// configuration properties
static const int CONTACT_BOUNCE_PREVENTION_THRESHOLD = 1000;
unsigned int fanServiceDurationMinutes = 5;
unsigned int fanEngagementThresholdMinutes = 2;
unsigned int continuousModeDurationMinutes = 180;
unsigned int continuousModeTimesPerHour = 3;

// pins definitions
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

void beep();

void disableContMode();

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
    pinMode(RELAY_PIN, OUTPUT);
    pinMode(BUZZER_PIN, OUTPUT);
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

    time_t currTime = now();
    int currentMinute = minute(currTime);

    if (Serial.available() > 0 && Serial.parseInt() == 5) {
        activateContMode();
        Serial.print(millis());
        Serial.println(" - CONT MODE ACTIVATED");
    }

    unsigned long sinceLastLightSwitchTimestamp = millis() - lastLightSwitchTs;
    boolean newLightState = !digitalRead(LIGHT_PIN);

    if (!fanState && lightState && !fanEngagedMsg && millisToSeconds(sinceLastLightSwitchTimestamp) > fanEngagementThresholdMinutes * 60) {
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
        continuousFanState = currentMinute % (60 / continuousModeTimesPerHour) < fanServiceDurationMinutes;
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
