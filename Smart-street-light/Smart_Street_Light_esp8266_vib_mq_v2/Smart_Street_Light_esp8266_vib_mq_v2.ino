/* Smart Street Light System + Accident Detection + DHT11 + MQ135 */

#define BLYNK_TEMPLATE_ID "TMPL3218JQrwr"
#define BLYNK_TEMPLATE_NAME "Smart Street Light System"
#define BLYNK_AUTH_TOKEN "1WA71Er22Qp48rqyEH7zG7WRYMkWdosY"

#include <ESP8266WiFi.h>
#include <BlynkSimpleEsp8266.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <DHT.h>

// -------------------- WiFi Credentials --------------------
char ssid[] = "Gtech";
char pass[] = "12345678";

// -------------------- NTP Client --------------------
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 19800, 60000);  // GMT +5:30

// -------------------- Pin Definitions --------------------
#define IR1 D5
#define IR2 D6
#define IR3 D7

#define LED1 D1
#define LED2 D2
#define LED3 D3

#define LDR_PIN D0      // Moved to digital pin
#define MQ135_PIN A0    // MQ135 Air Quality Sensor (Analog)

#define VIB_PIN D8
#define DHTPIN D4
#define DHTTYPE DHT11

DHT dht(DHTPIN, DHTTYPE);

// -------------------- Global Variables --------------------
int mode = 0;  // 0 = City, 1 = Village
int testHour = -1;
int testMinute = -1;
int blynkSeconds = 0;

bool vibrationDetected = false;
unsigned long lastAlertTime = 0;
const unsigned long alertCooldown = 15000; // 15 sec cooldown

unsigned long lastSensorRead = 0;
const unsigned long SENSOR_INTERVAL = 2000; // 2 sec

// -------------------- Air Quality Threshold --------------------
const int MQ135_THRESHOLD = 400;  // Adjust as needed (300–500 is common baseline)

// -------------------- Blynk Virtual Pins --------------------
BLYNK_WRITE(V0) { // Mode Selector
  mode = param.asInt();
  Serial.print("Mode selected: ");
  Serial.println(mode == 0 ? "City" : "Village");
}

BLYNK_WRITE(V2) { // Manual Time Input (seconds)
  blynkSeconds = param.asInt();
  testHour = blynkSeconds / 3600;
  testMinute = (blynkSeconds % 3600) / 60;
  Serial.printf("Received time: %02d:%02d\n", testHour, testMinute);
}

// -------------------- Setup --------------------
void setup() {
  Serial.begin(115200);

  pinMode(IR1, INPUT);
  pinMode(IR2, INPUT);
  pinMode(IR3, INPUT);

  pinMode(LED1, OUTPUT);
  pinMode(LED2, OUTPUT);
  pinMode(LED3, OUTPUT);

  pinMode(VIB_PIN, INPUT);
  pinMode(LDR_PIN, INPUT);

  dht.begin();

  WiFi.begin(ssid, pass);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi Connected");

  Blynk.begin(BLYNK_AUTH_TOKEN, ssid, pass);
  timeClient.begin();
}

// -------------------- Main Loop --------------------
void loop() {
  Blynk.run();
  timeClient.update();

  int currentHour, currentMinute;
  if (testHour >= 0) {
    currentHour = testHour;
    currentMinute = testMinute;
  } else {
    currentHour = timeClient.getHours();
    currentMinute = timeClient.getMinutes();
  }

  // -------------------- LDR Digital Read --------------------
  int ldrDigital = digitalRead(LDR_PIN); // 0=light, 1=dark
  bool isDark = (ldrDigital == HIGH);

  // -------------------- IR Sensor Readings --------------------
  int ir1 = digitalRead(IR1);
  int ir2 = digitalRead(IR2);
  int ir3 = digitalRead(IR3);

  bool led1Status = false;
  bool led2Status = false;
  bool led3Status = false;

  // -------------------- Lighting Logic --------------------
  switch (mode) {
    case 0: // City Mode
      if (isDark && (currentHour < 6 || currentHour > 17)) {
        led1Status = led2Status = led3Status = true;
      }
      break;

    case 1: // Village Mode
      if (currentHour >= 17 && currentHour < 22) {
        led1Status = led2Status = led3Status = isDark;
      } else if (currentHour >= 22 || currentHour < 6) {
        led1Status = (ir1 == LOW);
        led2Status = (ir2 == LOW);
        led3Status = (ir3 == LOW);
      }
      break;
  }

  digitalWrite(LED1, led1Status ? HIGH : LOW);
  digitalWrite(LED2, led2Status ? HIGH : LOW);
  digitalWrite(LED3, led3Status ? HIGH : LOW);

  // -------------------- Vibration Detection --------------------
  int vibValue = digitalRead(VIB_PIN);
  if (vibValue == HIGH && (millis() - lastAlertTime > alertCooldown)) {
    vibrationDetected = true;
    lastAlertTime = millis();

    Serial.println("⚠️ Vibration detected! Sending Blynk Alert...");
    Blynk.logEvent("alert", "⚠️ Possible accident detected!");
  }

  // -------------------- Periodic Sensor Reading --------------------
  if (millis() - lastSensorRead > SENSOR_INTERVAL) {
    lastSensorRead = millis();

    float h = dht.readHumidity();
    float t = dht.readTemperature();
    int airQuality = analogRead(MQ135_PIN);

    // Send data to Blynk
    if (!isnan(h) && !isnan(t)) {
      Blynk.virtualWrite(V3, t); // Temperature
      Blynk.virtualWrite(V4, h); // Humidity
    }

    Blynk.virtualWrite(V5, airQuality); // Air Quality

    // --- New Air Quality Alert ---
    if (airQuality > MQ135_THRESHOLD && (millis() - lastAlertTime > alertCooldown)) {
      lastAlertTime = millis();
      Serial.println("⚠️ Poor Air Quality Detected!");
      Blynk.logEvent("alert", "⚠️ Poor Air Quality Detected!");
    }

    // Debug log
    Serial.printf("DHT11 => Temp: %.1f°C | Humidity: %.1f%% | MQ135: %d\n", t, h, airQuality);
  }

  // -------------------- Debug Output --------------------
  Serial.printf("LDR: %d | IR: %d,%d,%d | VIB: %d | LED: %s,%s,%s | Mode: %d\n",
                ldrDigital, ir1, ir2, ir3, vibValue,
                led1Status ? "ON" : "OFF",
                led2Status ? "ON" : "OFF",
                led3Status ? "ON" : "OFF",c:\Users\user\Downloads
                mode);

  delay(500);
}
