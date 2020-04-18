#include <Arduino.h>
#include "../lib/Nova_Fitness_Sds_dust_sensors_library/src/SdsDustSensor.h"

int rxPin = 4;
int txPin = 5;
SdsDustSensor sds(rxPin, txPin);

void setup() {
    Serial.begin(115200);
    sds.begin(9600);

    Serial.println(sds.queryFirmwareVersion().toString()); // prints firmware version
    Serial.println(sds.setQueryReportingMode().toString()); // ensures sensor is in 'query' reporting mode
}

void loop() {
    sds.wakeup();
    delay(30000); // working 30 seconds

    PmResult pm = sds.queryPm();
    if (pm.isOk()) {
        Serial.print("PM2.5!!! = ");
        Serial.print(pm.pm25);
        Serial.print(", PM10!!! = ");
        Serial.println(pm.pm10);

        // if you want to just print the measured values, you can use toString() method as well
        Serial.println(pm.toString());
    } else {
        Serial.print("Could not read values from sensor, reason: ");
        Serial.println(pm.statusToString());
    }

    WorkingStateResult state = sds.sleep();
    if (state.isWorking()) {
        Serial.println("Problem with sleeping the sensor.");
    } else {
        Serial.println("Sensor is sleeping");
        delay(60000); // wait 1 minute
    }
}
