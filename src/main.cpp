#include <Arduino.h>
#include <../lib/Time-master/TimeLib.h>
#include <time.h>
#include <HardwareSerial.h>

const boolean DEBUG = true;
const int FAN_DURATION_SEC = 2;
const int FAN_ENGAGEMENT_THRESHOLD_SEC = 5;
static const int LIGHT_PIN = 4;
static const int RELAY_PIN = 5;
boolean lightState = false;
boolean fanState = false;
boolean fanEngagedMsg = false;
unsigned long lastLightSwitchTimestmap;
unsigned long fanTimer;
boolean continuousMode = false;
boolean continuousFanState = false;
unsigned long continuousModeEnabledTimer = 0;
const unsigned long continuousModeDuration = 40000;
long logTimer;

void setup() {
    Serial.begin(115200);
    pinMode(RELAY_PIN, OUTPUT);
    lastLightSwitchTimestmap = millis();
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
        lastLightSwitchTimestmap = millis();
    } else {
        Serial.print(millis());
        Serial.print(" - Light is off, duration ");
        Serial.print(currentLightStateDuration);
        Serial.println(" ms.");
        lastLightSwitchTimestmap = millis();
        if (currentLightStateDuration / 1000 > FAN_ENGAGEMENT_THRESHOLD_SEC) {
            turnOnTheFan();
        }
        fanTimer = millis();
    }
    lightState = newLightState;
}

void turnOffTheFanByTimeout() {
    if ((fanState && !lightState && (millis() - fanTimer) / 1000 > FAN_DURATION_SEC) || (millis() - fanTimer) / 1000 < 0) {
        turnOffTheFan();
    }
    /*if(millis() - fanTimer <= 0) {    //todo
        Serial.println("Turning off by negatve timer");
        turnOffTheFan();

    }*/

}

unsigned long millisToSeconds(unsigned long sinceLastSwitchTs) { return sinceLastSwitchTs / 1000; }

void issueFanArmingMessage(unsigned long sinceLastSwitchTs) {
    if (!fanState && lightState && !fanEngagedMsg && millisToSeconds(sinceLastSwitchTs) > FAN_ENGAGEMENT_THRESHOLD_SEC) {
        fanEngagedMsg = true;
        Serial.print(millis());
        Serial.println(" - Fan armed ");
    }
}

void activateContinuousMode() {
    continuousModeEnabledTimer = millis();
    continuousMode = true;
}

void loop() {

    int currentSecond = second(now());

    if (Serial.available() > 0 && Serial.parseInt() == 5) {
        activateContinuousMode();
        Serial.print(millis());
        Serial.println(" - CONT MODE ACTIVATED");
    }

    unsigned long sinceLastLightSwitchTimestamp = millis() - lastLightSwitchTimestmap;
    boolean newLightState = !digitalRead(LIGHT_PIN);

    issueFanArmingMessage(sinceLastLightSwitchTimestamp);

    if (newLightState != lightState) {
        delay(500);
        newLightState = !digitalRead(LIGHT_PIN);
        if (newLightState == lightState) {
            Serial.print(millis());
            Serial.println(" - QUICKSHIFT!");
            if (fanState) {
                turnOffTheFan();
            } else {
                turnOnTheFan();
            }
        } else {
            changeLightState(newLightState, sinceLastLightSwitchTimestamp);
        }
    }

    // After switching the light off, fan must be switched off upon timeout
    turnOffTheFanByTimeout();

    if (!fanState && !lightState && continuousMode && millis() - continuousModeEnabledTimer < continuousModeDuration) {
        if(currentSecond % 5 == 0){
            continuousFanState = true;
        } else {
            continuousFanState = false;
        }
    } else if (continuousMode && (millis() - continuousModeEnabledTimer >= continuousModeDuration || millis() - continuousModeEnabledTimer < 0)) {
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

    digitalWrite(RELAY_PIN, fanState || continuousFanState);

}