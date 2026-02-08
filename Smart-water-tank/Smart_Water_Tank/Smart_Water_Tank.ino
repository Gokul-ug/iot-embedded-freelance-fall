#include <Servo.h>
#include <DHT.h>

// --- Pin Definitions ---
#define TRIG_PIN 8
#define ECHO_PIN 9
#define RELAY_PIN 5
#define FLOW_PIN 2
#define RED_LED 7
#define SERVO_PIN 3
#define DHTPIN 6
#define DHTTYPE DHT11
#define RAIN_PIN A0

// --- Constants ---
#define FULL_LEVEL 10   // cm
#define REFILL_LEVEL 20 // cm
#define FLOW_PULSES_PER_LITRE 450.0

Servo valve;
DHT dht(DHTPIN, DHTTYPE);

// --- Variables ---
volatile int flowPulseCount = 0;
unsigned long lastFlowMillis = 0;
unsigned long lastFlowDetected = 0;
float flowRate = 0.0;
bool pumpOn = false;

// --- Flow Interrupt ---
void flowISR() {
  flowPulseCount++;
  lastFlowDetected = millis(); // Update last flow time
}

// --- Measure Water Level ---
float getWaterLevel() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  long duration = pulseIn(ECHO_PIN, HIGH, 30000);
  float distance = duration * 0.0343 / 2.0;
  return constrain(distance, 0, 400);
}

// --- Calculate Flow Rate ---
void calculateFlowRate() {
  unsigned long now = millis();
  if (now - lastFlowMillis >= 1000) {
    flowRate = (flowPulseCount / FLOW_PULSES_PER_LITRE) * 60.0;
    flowPulseCount = 0;
    lastFlowMillis = now;
  }
}

void setup() {
  Serial.begin(9600);

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(FLOW_PIN, INPUT_PULLUP);
  pinMode(RED_LED, OUTPUT);
  pinMode(RAIN_PIN, INPUT);

  attachInterrupt(digitalPinToInterrupt(FLOW_PIN), flowISR, RISING);

  dht.begin();
  valve.attach(SERVO_PIN);
  valve.write(0); // Valve open initially

  digitalWrite(RELAY_PIN, HIGH); // Relay OFF (reverse logic)
  digitalWrite(RED_LED, LOW);

  lastFlowDetected = millis();

  Serial.println("Smart Tank System Initialized");
}

void loop() {
  calculateFlowRate();

  // --- Sensor Readings ---
  float level = getWaterLevel();
  int rainVal = analogRead(RAIN_PIN);
  bool isRain = (rainVal < 500);
  float temp = dht.readTemperature();
  float hum = dht.readHumidity();

  // Handle DHT failure
  if (isnan(temp) || isnan(hum)) {
    temp = 25.0;
    hum = 60.0;
  }

  // --- Relay Logic (Reversed) ---
  if (level > REFILL_LEVEL && !pumpOn) {
    digitalWrite(RELAY_PIN, LOW); // Motor ON
    pumpOn = true;
    lastFlowDetected = millis();  // reset flow timer
    Serial.println("Motor ON - Refilling Started");
  } 
  else if (level <= FULL_LEVEL && pumpOn) {
    digitalWrite(RELAY_PIN, HIGH); // Motor OFF
    pumpOn = false;
    Serial.println("Motor OFF - Tank Full");
  }

  // --- Rain Logic (Servo Control) ---
  if (isRain) {
    valve.write(90); // Close valve
  } else {
    valve.write(0);  // Open valve
  }

  // --- Flow Safety Check ---
  if (pumpOn) {
    if (flowRate > 0.1) {
      lastFlowDetected = millis();
    }

    if (millis() - lastFlowDetected > 5000) {
      // No flow detected for 5 seconds
      digitalWrite(RELAY_PIN, HIGH); // Motor OFF
      pumpOn = false;
      digitalWrite(RED_LED, HIGH);   // Turn ON alert LED
      valve.write(90);               // Close valve
      Serial.println("⚠️ No Water Flow Detected! Motor Stopped.");
      delay(3000);
      digitalWrite(RED_LED, LOW);    // Turn OFF alert LED after delay
    }
  }

  // --- Serial Output ---
  Serial.print("Water Level: "); Serial.print(level, 1);
  Serial.print(" cm | Pump: "); Serial.print(pumpOn ? "ON" : "OFF");
  Serial.print(" | Flow: "); Serial.print(flowRate, 2); Serial.print(" L/min");
  Serial.print(" | Rain: "); Serial.print(isRain ? "Detected" : "None");
  Serial.print(" | Temp: "); Serial.print(temp, 1); Serial.print("°C");
  Serial.print(" | Humidity: "); Serial.print(hum, 1); Serial.print("%");
  Serial.print(" | RainVal: "); Serial.println(rainVal);

  delay(1000);
}
