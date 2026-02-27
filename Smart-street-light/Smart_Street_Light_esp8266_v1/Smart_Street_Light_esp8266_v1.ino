/* accident detection alert to blynk*/


#define BLYNK_TEMPLATE_ID "TMPL3218JQrwr"
#define BLYNK_TEMPLATE_NAME "Smart Street Light System"
#define BLYNK_AUTH_TOKEN "1WA71Er22Qp48rqyEH7zG7WRYMkWdosY"

#include <ESP8266WiFi.h>
#include <BlynkSimpleEsp8266.h>
#include <NTPClient.h>
#include <WiFiUdp.h>

// WiFi credentials
char ssid[] = "Gtech";
char pass[] = "12345678";

// NTP client setup
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 19800, 60000);  // 19800 = GMT+5:30

// Pin Definitions (adjust GPIOs as per your board)
#define IR1 D5
#define IR2 D6
#define IR3 D7

#define LED1 D1
#define LED2 D2
#define LED3 D3

#define LDR_PIN A0
#define VIB_PIN D8   // vibration sensor input (digital pin)

// Global variables
int mode = 0;        // 0 = City, 1 = Village
int testHour = -1;
int testMinute = -1;
int blynkSeconds = 0;

bool vibrationDetected = false;
unsigned long lastAlertTime = 0;
const unsigned long alertCooldown = 15000; // 15 sec cooldown between alerts

// Blynk Mode Selector (V0)
BLYNK_WRITE(V0) {
  mode = param.asInt();
  Serial.print("Mode selected: ");
  Serial.println(mode == 0 ? "City" : "Village");
}

// Blynk Time Input (V2) in seconds
BLYNK_WRITE(V2) {
  blynkSeconds = param.asInt();
  testHour = blynkSeconds / 3600;
  testMinute = (blynkSeconds % 3600) / 60;
  Serial.print("Received from Blynk V2 (seconds): ");
  Serial.println(blynkSeconds);
  Serial.printf("Converted Test Time => %02d:%02d\n", testHour, testMinute);
}

void setup() {
  Serial.begin(115200);

  pinMode(IR1, INPUT);
  pinMode(IR2, INPUT);
  pinMode(IR3, INPUT);

  pinMode(LED1, OUTPUT);
  pinMode(LED2, OUTPUT);
  pinMode(LED3, OUTPUT);

  pinMode(VIB_PIN, INPUT);

  WiFi.begin(ssid, pass);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi Connected");

  Blynk.begin(BLYNK_AUTH_TOKEN, ssid, pass);

  // Start NTP Client
  timeClient.begin();
}

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

  Serial.printf("Using time: %02d:%02d (%s)\n", currentHour, currentMinute,
                (testHour >= 0) ? "Test from V2" : "Real NTP");

  int ldrVal = analogRead(LDR_PIN);
  bool isDark = ldrVal > 1000;  // adjust threshold for LDR (0–1023)

  int ir1 = digitalRead(IR1);
  int ir2 = digitalRead(IR2);
  int ir3 = digitalRead(IR3);

  bool led1Status = false;
  bool led2Status = false;
  bool led3Status = false;

  switch (mode) {
    case 0: // City Mode: LDR + Time (6AM–5PM off even if dark)
      if (isDark && (currentHour < 6 || currentHour > 17)) {
        led1Status = led2Status = led3Status = true;
      }
      break;

    case 1: // Village Mode
      if (currentHour >= 17 && currentHour < 22) {
        // Between 5PM–10PM use LDR
        led1Status = led2Status = led3Status = isDark;
      } else if (currentHour >= 22 || currentHour < 6) {
        // After 10PM to 6AM use IR sensors
        led1Status = (ir1 == LOW);
        led2Status = (ir2 == LOW);
        led3Status = (ir3 == LOW);
      }
      break;
  }

  digitalWrite(LED1, led1Status ? HIGH : LOW);
  digitalWrite(LED2, led2Status ? HIGH : LOW);
  digitalWrite(LED3, led3Status ? HIGH : LOW);

  // --- Vibration Detection & Alert ---
  int vibValue = digitalRead(VIB_PIN);
  if (vibValue == HIGH && (millis() - lastAlertTime > alertCooldown)) {
    vibrationDetected = true;
    lastAlertTime = millis();

    Serial.println("⚠️ Vibration detected! Sending Blynk Alert...");
    Blynk.logEvent("alert", "Vibration detected! Possible accident nearby.");
  }

  // Debug log
  Serial.printf("LDR: %d | IR: %d,%d,%d | VIB: %d | LED: %s,%s,%s | Mode: %d\n",
                ldrVal, ir1, ir2, ir3, vibValue,
                led1Status ? "ON" : "OFF",
                led2Status ? "ON" : "OFF",
                led3Status ? "ON" : "OFF",
                mode);

  // Send LDR value to Blynk V1
  Blynk.virtualWrite(V1, ldrVal);

  delay(500);
}
