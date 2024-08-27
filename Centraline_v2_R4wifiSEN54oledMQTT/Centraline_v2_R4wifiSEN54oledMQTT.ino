

#include <Arduino.h>
#include <SensirionI2CSen5x.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFiS3.h>
#include <ArduinoMqttClient.h>
#include "arduino_secrets.h"

char ssid[] = SECRET_SSID;
char pass[] = SECRET_PASS;
int status = WL_IDLE_STATUS;

WiFiClient wifiClient;
MqttClient mqttClient(wifiClient);
const char broker[] = "37100lab.it";
//const char broker[] = "test.mosquitto.org";
int port = 12883;
const char PM10topic[] = "oltreildomani/centralina1/PM10";
const char Humtopic[] = "oltreildomani/centralina1/RH";
const char Temptopic[] = "oltreildomani/centralina1/T";

const long interval = 1000;
unsigned long previousMillis = 0;

#define SCREEN_WIDTH 128  // OLED display width, in pixels
#define SCREEN_HEIGHT 64  // OLED display height, in pixels

// Declaration for an SSD1306 display connected to I2C (SDA, SCL pins)
// The pins for I2C are defined by the Wire-library.
// On an arduino UNO:       A4(SDA), A5(SCL)
// On an arduino MEGA 2560: 20(SDA), 21(SCL)
// On an arduino LEONARDO:   2(SDA),  3(SCL), ...
#define OLED_RESET -1        // Reset pin # (or -1 if sharing Arduino reset pin)
#define SCREEN_ADDRESS 0x3C  ///< See datasheet for Address; 0x3D for 128x64, 0x3C for 128x32
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

#define MAXBUF_REQUIREMENT 48
#if (defined(I2C_BUFFER_LENGTH) && (I2C_BUFFER_LENGTH >= MAXBUF_REQUIREMENT)) || (defined(BUFFER_LENGTH) && BUFFER_LENGTH >= MAXBUF_REQUIREMENT)
#define USE_PRODUCT_INFO
#endif

SensirionI2CSen5x sen5x;
float massConcentrationPm1p0;
float massConcentrationPm2p5;
float massConcentrationPm4p0;
float massConcentrationPm10p0;
float ambientHumidity;
float ambientTemperature;
float vocIndex;
float noxIndex;

void setup() {

  pinMode(12, OUTPUT);  // un brutto modo di alimentare il display OLED
  digitalWrite(12, HIGH);

  // SSD1306_SWITCHCAPVCC = generate display voltage from 3.3V internally
  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println(F("OLED SSD1306 allocation failed"));
    for (;;)
      ;  // Don't proceed, loop forever
  }
  display.clearDisplay();
  display.display();
  delay(100);

  stampaIntro();

  Serial.begin(9600);
  while (!Serial) {
    delay(100);
  }

  if (WiFi.status() == WL_NO_MODULE) {
    Serial.println("WIFI Communication with WiFi module failed!");
    // don't continue
    while (true)
      ;
  }

  // attempt to connect to WiFi network:
  while (status != WL_CONNECTED) {
    Serial.print("WIFI Attempting to connect to WPA SSID: ");
    Serial.println(ssid);
    status = WiFi.begin(ssid, pass);
    delay(10000);
  }
  Serial.println("WIFI You're connected to the network");
  IPAddress ip = WiFi.localIP();
  Serial.print("WIFI IP Address: ");
  Serial.println(ip);

  Serial.print("MQTT Attempting to connect to the MQTT broker: ");
  Serial.println(broker);

  if (!mqttClient.connect(broker, port)) {
    Serial.print("MQTT connection failed! Error code = ");
    Serial.println(mqttClient.connectError());

    while (1)
      ;
  }

  Serial.println("MQTT You're connected to the MQTT broker!");
  Serial.println();

  Wire1.begin();
  sen5x.begin(Wire1);

  uint16_t error;
  char errorMessage[256];
  error = sen5x.deviceReset();
  if (error) {
    Serial.print("SEN54 Error trying to execute deviceReset(): ");
    errorToString(error, errorMessage, 256);
    Serial.println(errorMessage);
  }

  float tempOffset = 0.0;
  error = sen5x.setTemperatureOffsetSimple(tempOffset);
  if (error) {
    Serial.print("SEN54 Error trying to execute setTemperatureOffsetSimple(): ");
    errorToString(error, errorMessage, 256);
    Serial.println(errorMessage);
  } else {
    Serial.print("SEN54 Temperature Offset set to ");
    Serial.print(tempOffset);
    Serial.println(" deg. Celsius (SEN54/SEN55 only");
  }

  error = sen5x.startMeasurement();
  if (error) {
    Serial.print("SEN54 Error trying to execute startMeasurement(): ");
    errorToString(error, errorMessage, 256);
    Serial.println(errorMessage);
  }
}

void loop() {

  uint16_t error;
  char errorMessage[256];
  delay(1000);
  error = sen5x.readMeasuredValues(
    massConcentrationPm1p0, massConcentrationPm2p5, massConcentrationPm4p0,
    massConcentrationPm10p0, ambientHumidity, ambientTemperature, vocIndex,
    noxIndex);

  if (error) {
    Serial.print("SEN54 Error trying to execute readMeasuredValues(): ");
    errorToString(error, errorMessage, 256);
    Serial.println(errorMessage);
  } else {
    Serial.print("SEN54 PM1:");
    Serial.print(massConcentrationPm1p0);
    Serial.print("\t");
    Serial.print("PM 2.5:");
    Serial.print(massConcentrationPm2p5);
    Serial.print("\t");
    Serial.print("PM 4:");
    Serial.print(massConcentrationPm4p0);
    Serial.print("\t");
    Serial.print("PM 10:");
    Serial.print(massConcentrationPm10p0);
    Serial.print("\t");
    Serial.print("RH:");
    if (isnan(ambientHumidity)) {
      Serial.print("n/a");
    } else {
      Serial.print(ambientHumidity);
    }
    Serial.print("\t");
    Serial.print("Temp:");
    if (isnan(ambientTemperature)) {
      Serial.print("n/a");
    } else {
      Serial.print(ambientTemperature);
    }
    Serial.println();
  }

  stampaLetture();

  mqttClient.poll();
  unsigned long currentMillis = millis();

  if (currentMillis - previousMillis >= interval) {
    previousMillis = currentMillis;
    Serial.print("MQTT Sending message to topic: ");
    Serial.print(PM10topic);
    Serial.print(" : ");
    Serial.println(massConcentrationPm10p0);

    mqttClient.beginMessage(PM10topic);
    mqttClient.print(massConcentrationPm10p0);
    mqttClient.endMessage();

    Serial.print("MQTT Sending message to topic: ");
    Serial.print(Humtopic);
    Serial.print(" : ");
    Serial.println(ambientHumidity);

    mqttClient.beginMessage(Humtopic);
    mqttClient.print(ambientHumidity);
    mqttClient.endMessage();

    Serial.print("MQTT Sending message to topic: ");
    Serial.print(Temptopic);
    Serial.print(" : ");
    Serial.println(ambientTemperature);

    mqttClient.beginMessage(Temptopic);
    mqttClient.print(ambientTemperature);
    mqttClient.endMessage();

    Serial.println();
  }
}

void stampaLetture() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("Dati real time:");
  display.println("");
  display.print("PM10= ");
  display.print(massConcentrationPm10p0);
  display.println(" ug/m3");
  display.print("temp= ");
  display.print(ambientTemperature);
  display.println(" C");
  display.print("Hum = ");
  display.print(ambientHumidity);
  display.print(" %");
  display.display();
}

void stampaIntro() {
  display.clearDisplay();
  display.setTextSize(2);  // Draw 2X-scale text
  display.setTextColor(SSD1306_WHITE);
  display.println("Verona ");
  display.println("FabLab");
  display.setTextSize(1);  // Draw 2X-scale text
  display.setTextColor(SSD1306_WHITE);
  display.println("");
  display.println("");
  display.println("");
  display.println("       presenta..");
  display.display();
  delay(3000);
}
