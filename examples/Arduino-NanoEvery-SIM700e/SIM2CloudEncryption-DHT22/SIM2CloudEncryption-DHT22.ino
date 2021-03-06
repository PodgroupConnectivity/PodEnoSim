/*
  Arduino Nano Every + Waveshare Sim7000E NB-IoT Hat + DHT22 SIM2Cloud-Encryption Demo.
  Copyright (c) 2021 Pod Group Ltd. http://podgroup.com
  
  This program is free software; you can redistribute it and/or modify
  it under the terms of the MIT License.
  
  You should have received a copy of the MIT License
  along with this program.  If not, see <https://opensource.org/licenses/MIT>.

  Description:
    - Post DHT22 Temperature to Pod IoT Platform API (See below).
    - Uses Pod ENO SIM built-in TLS1.3-PSK to request HTTPS POST /v1/data/{DEVICEID}

  More information:
    - Pod IoT Platform: https://iotsim.podgroup.com/v1/docs/#/
    - Arduino Project Hub Article: https://create.arduino.cc/projecthub/kostiantynchertov/tls-1-3-for-arduino-nano-649610

  Usage:
    - Open Board Manager and install "Arduino megaAVR Boards".
    - Open Library Manager and install: "Adafruit DHT Sensor Library >= 1.4.2".
    - Select Board "Arduino Nano Every".

  Requirements:
    - Arduino Nano Every.
    - Waveshare Sim7000E NB-IoT Hat
    - AM2302 DHT22 Temperature Sensor.
    - Pod ENO SIM Card (ask for yours, emails below).

  Authors:
   - Kostiantyn Chertov <kostiantyn.chertov@podgroup.com>
   - J. Félix Ontañón <felix.ontanon@podgroup.com>
*/

// --------- BEGINING OF CONFIGURABLE FIRMWARE PARAMETERS SECTION ---------

// PIN the WaveShare SIM7000E NB-IoT HAT DTR is wired to
#define PIN_DTR 4

// PIN the AM2302 (DHT22) is wired to
#define DHT_PIN  5

// Adafruit DHT library offers support for several DHT sensors.DHT22 AM2302, DHT22 AM2301 and 
// We've only tested with DHT22 AM2302 ...
#define DHT_TYPE DHT22 // DHT 22 AM2302, AM2321
// ... but you can optionally give it a try to DHT11 and DHT21
//#define DHT_TYPE DHT11   // DHT 11
//#define DHT_TYPE DHT21   // DHT 21 (AM2301)

// Minutes between each request. You can configure this, but please know
// each request is queued. The SIM + GSM Module will asynchronously run 
// each HTTPS POST operation from queue. One miniute or more is suggested
// between each requests.
const byte WAIT_MINUTES = 5;

// --------- END OF CONFIGURABLE FIRMWARE PARAMETERS SECTION ---------

#include "DHT.h" // Adafruit DHT Sensor Library >= 1.4.2
#include "PodEnoSim.h"

PodEnoSim enosim(&Serial1);

DHT sensor(DHT_PIN, DHT_TYPE);

#define MODEM_BAUD_RATE  57600
#define LOG_BAUD_RATE  57600

#define LEN_DATA  22
#define OFS_DATA_DIGITS  16
const char DATA_ITEM[LEN_DATA] = { '{', '"', 't', 'e', 'm', 'p', 'e', 'r', 'a', 't', 'u', 'r', 'e', '"', ':', ' ', ' ', ' ', '.', ' ', ' ', '}' }; 

byte dataBuf[LEN_DATA];

char digits[4];

#define LEN_DEVICE_ID  10
byte id[LEN_DEVICE_ID];

// Aux function to get 4 digits temp from sensor read
void getTemperatureDigits(float t, char * digitBuffer) {
  int it = round(t * 100);
  char b;
  for (char i=0; i<4; i++) {
    b = it % 10;
    digitBuffer[3 - i] = (char)(0x30 + b);
    it -= b;
    it /= 10;
  }
}

// Aux function to properly print DEVICEID from ArduinoUniqueID
void logHex(byte * buf, short len) {
  byte b;
  for (short i=0; i<len; i++) {
    b = buf[i];
    if (b < 0x10) {
      Serial.print("0");
    }
    Serial.print(b, HEX);
  }
  Serial.println();
}

void setup() {
  byte res;
  
  Serial.begin(LOG_BAUD_RATE); 
  while (!Serial) {
    ; // wait for serial port to connect
  }

  // Get Device ID
  memcpy(id, (void *)&SIGROW.SERNUM0, LEN_DEVICE_ID);
  Serial.print("Device ID: ");
  logHex(id, LEN_DEVICE_ID);

  // configure pins
  pinMode(PIN_DTR, OUTPUT);
  pinMode(DHT_PIN, INPUT);
  
  Serial.print("Modem initialization...");
  res = enosim.init(MODEM_BAUD_RATE);
  if (res == RES_OK) {
    Serial.println("OK");
  } else {
    Serial.print("ERROR: ");
    Serial.println(res);
  }

  // Initialize DHT sensor
  sensor.begin();

  // wake up Sim7000
  digitalWrite(PIN_DTR, LOW);   
  delay(1000);

  Serial.print("waiting for modem start...");
  enosim.waitForModemStart();
  Serial.println("OK");

  Serial.print("waiting for network registration...");
  enosim.waitForNetworkRegistration();
  Serial.println("OK");
  
  // This command sets the DEVICEID to form the 
  // HTTPS POST /data/{DEVICEID} API request.
  res = enosim.deviceIdSet(id, LEN_DEVICE_ID);
  if (res == RES_OK) {
    Serial.println("Set Device ID: OK");
  } else {
    Serial.println("Set Device ID: ERROR");
  }
  enosim.prepareForSleep();
}

void loop() {

  memcpy(dataBuf, DATA_ITEM, LEN_DATA);

  float t = sensor.readTemperature();
  Serial.print(F("Temperature: "));
  Serial.print(t);
  Serial.println(F("°C "));

  // convert temperature value into digits
  getTemperatureDigits(t, digits);

  // put temperature digits in message buffer
  // assume 10 <= temperature < 99
  dataBuf[OFS_DATA_DIGITS] = digits[0];
  dataBuf[OFS_DATA_DIGITS + 1] = digits[1];
  dataBuf[OFS_DATA_DIGITS + 3] = digits[2];
  dataBuf[OFS_DATA_DIGITS + 4] = digits[3];

  Serial.print("Put Data: ");

  // This command to deliver the data to cloud.
  // Uses SIM-embedded TLS1.3. The SIM ensembles
  // an HTTPS POST /data/{DEVICEID} request against
  // Pod IoT Platform API https://iotsim.podgroup.com/v1/docs/#
  // to be executed by GSM Module.  
  byte res = enosim.dataSend(dataBuf, LEN_DATA);

  if (res == RES_OK) {
    Serial.println("OK");
  } else {
    Serial.print("ERROR ");
    Serial.println(res);
  }

  Serial.print("waiting");
  byte minutes = WAIT_MINUTES;
  byte secs;
  while (minutes > 0) {
    for (secs=0; secs < 60; secs++) {
      delay(1000);
    }
    minutes -= 1;
    Serial.print('.');
  }
  Serial.println();
}
