#define BLYNK_TEMPLATE_ID "TMPL3a820QkBm"
#define BLYNK_TEMPLATE_NAME "Pet Bot Smart Feeder"
#define BLYNK_AUTH_TOKEN "uyr8n-J0dheLY9KWV_5PNfGjj2Go_EIx"

#include <ESP8266WiFi.h>
#include <BlynkSimpleEsp8266.h>
#include <Servo.h>
#include <time.h>

// WiFi credentials
char ssid[] = "Gtech";
char pass[] = "12345678";

// NTP Time Setup
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 19800;  // GMT +5:30 for IST
const int daylightOffset_sec = 0;

// Blynk Virtual Pins
#define VPIN_MANUAL_FEED V3
#define VPIN_TIME_1 V0
#define VPIN_TIME_2 V1
#define VPIN_TIME_3 V2

// Pins
#define SERVO_PIN D2               // GPIO4
#define TRIG_PIN D5                // GPIO14
#define ECHO_PIN D6                // GPIO12
#define SOUND_TRIGGER_PIN D1       // GPIO5 (To Uno D4)

Servo myServo;

// Feed times
long feedTimes[3] = { -1, -1, -1 };
bool hasFed[3] = { false, false, false };

// Manual Feed
BLYNK_WRITE(VPIN_MANUAL_FEED) {
  int manual = param.asInt();
  if (manual == 1) {
    Serial.println("Manual Feed Button Pressed");
    dispenseFood();
  }
}

// Time Inputs from Blynk
BLYNK_WRITE(VPIN_TIME_1) {
  feedTimes[0] = param.asInt();
  hasFed[0] = false;
  Serial.printf("Feed Time 1 (V0) set: %ld seconds\n", feedTimes[0]);
}
BLYNK_WRITE(VPIN_TIME_2) {
  feedTimes[1] = param.asInt();
  hasFed[1] = false;
  Serial.printf("Feed Time 2 (V1) set: %ld seconds\n", feedTimes[1]);
}
BLYNK_WRITE(VPIN_TIME_3) {
  feedTimes[2] = param.asInt();
  hasFed[2] = false;
  Serial.printf("Feed Time 3 (V2) set: %ld seconds\n", feedTimes[2]);
}

void setup() {
  Serial.begin(115200);

  // Servo setup
  myServo.attach(SERVO_PIN);
  myServo.write(0); // Initial position

  // Pins
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(SOUND_TRIGGER_PIN, OUTPUT);
  digitalWrite(SOUND_TRIGGER_PIN, LOW); // Ensure low initially

  // Connect WiFi
  WiFi.begin(ssid, pass);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi Connected");

  // Blynk & Time
  Blynk.begin(BLYNK_AUTH_TOKEN, ssid, pass);
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
}

void loop() {
  Blynk.run();

  time_t now = time(nullptr);
  struct tm* timeinfo = localtime(&now);
  if (!timeinfo) {
    Serial.println("Failed to get time");
    return;
  }

  int secondsNow = timeinfo->tm_hour * 3600 + timeinfo->tm_min * 60 + timeinfo->tm_sec;
  Serial.printf("Current Time: %02d:%02d:%02d (%d seconds since midnight)\n",
                timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec, secondsNow);

  for (int i = 0; i < 3; i++) {
    if (feedTimes[i] > 0 && secondsNow == feedTimes[i] && !hasFed[i]) {
      Serial.printf("Auto Feed Triggered at Time Slot %d\n", i + 1);
      dispenseFood();
      hasFed[i] = true;
    }
  }

  delay(1000);  // 1-second interval
}

void dispenseFood() {
  Serial.println("Food Dispensed");

  // 🔊 Trigger sound on Uno via D1
  digitalWrite(SOUND_TRIGGER_PIN, HIGH);
  delay(100);  // Pulse for 100 ms
  digitalWrite(SOUND_TRIGGER_PIN, LOW);

  // Move servo
  myServo.write(180);
  delay(500);
  myServo.write(0);

  // Check if pet eats food within 10 seconds
  bool petAte = false;
  for (int i = 0; i < 10; i++) {
    float distance = getUltrasonicDistance();
    Serial.printf("Distance check (%d/10): %.2f cm\n", i + 1, distance);
    if (distance > 0 && distance <= 20.0) {
      petAte = true;
      break;
    }
    delay(1000);
  }

  if (!petAte) {
    Serial.println("Alert: Pet didn't eat the food");
    Blynk.logEvent("alert", "Pet didn't eat the food!");
  } else {
    Serial.println("Pet detected eating the food");
  }
}

float getUltrasonicDistance() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  long duration = pulseIn(ECHO_PIN, HIGH, 30000); // Timeout at 30ms
  if (duration == 0) return -1;

  float distance_cm = (duration * 0.0343) / 2.0;
  return distance_cm;
}
