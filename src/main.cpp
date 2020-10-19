#include <ESP8266WiFi.h>                                // Подключаем библиотеку ESP8266WiFi
#include <Wire.h>                                       // Подключаем библиотеку Wire
#include <../lib/Adafruit_BME280_Library-master/Adafruit_BME280.cpp>                            // Подключаем библиотеку Adafruit_BME280
#include <Adafruit_Sensor.h>                            // Подключаем библиотеку Adafruit_Sensor

#define SEALEVELPRESSURE_HPA (1013.25)                  // Задаем высоту

Adafruit_BME280 bme;                                    // Установка связи по интерфейсу I2C

const char* ssid = "ALEKSNET";          // Название Вашей WiFi сети
const char* password = "ekvatorthebest";     // Пароль от Вашей WiFi сети

WiFiServer server(80);                                  // Указываем порт Web-сервера
String header;

void setup() {
    Serial.begin(115200);                                 // Скорость передачи 115200
    bool status;

    if (!bme.begin(0x76)) {                               // Проверка инициализации датчика
        Serial.println("Could not find a valid BME280 sensor, check wiring!"); // Печать, об ошибки инициализации.
        while (1);                                          // Зацикливаем
    }

    Serial.print("Connecting to ");                       // Отправка в Serial port
    Serial.println(ssid);                                 // Отправка в Serial port
    WiFi.begin(ssid, password);                           // Подключение к WiFi Сети
    while (WiFi.status() != WL_CONNECTED) {               // Проверка подключения к WiFi сети
        delay(500);                                         // Пауза
        Serial.print(".");                                  // Отправка в Serial port
    }

    Serial.println("");                                   // Отправка в Serial port
    Serial.println("WiFi connected.");                    // Отправка в Serial port
    Serial.println("IP address: ");                       // Отправка в Serial port
    Serial.println(WiFi.localIP());                       // Отправка в Serial port
    server.begin();
}

void loop(){
    delay(1000);
    Serial.print("Temp: ");
    Serial.println(bme.readTemperature());
    Serial.print("Humid: ");
    Serial.println(bme.readHumidity());
}

