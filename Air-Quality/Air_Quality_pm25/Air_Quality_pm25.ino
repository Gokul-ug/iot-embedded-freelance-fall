#define BLYNK_TEMPLATE_NAME "Air Quality Alert"
#define BLYNK_TEMPLATE_ID "TMPL3APnPgJQT"
#define BLYNK_AUTH_TOKEN "_swAdylle1YnD5tVXzqOM1w-poo8rtNS"

#define BLYNK_PRINT Serial
#include <WiFi.h>
#include <BlynkSimpleEsp32.h>
#include <DHT.h>
#include <Wire.h>
//#include <LiquidCrystal_I2C.h>

// ================= Pin Definitions =================
#define GAS_SENSOR   34   // MQ-135 AO → GPIO34 (input only)
#define DHT_PIN      4    // DHT11 Data → GPIO4
#define DHT_TYPE     DHT11
#define ALERT_PIN    5    // Buzzer/LED → GPIO5

// GP2Y1010AU Dust Sensor
#define DUST_LED_PIN 25   // White wire → digital drive pin
#define DUST_PIN     35   // Black wire (Vo) → ADC input
const float VOC_BASE = 0.90;  // baseline voltage in clean air (measure & adjust)
const float ADC_REF  = 3.3;   // ESP32 ADC reference
const int   ADC_MAX  = 4095;

// ================= WiFi & Blynk =================
char auth[] = BLYNK_AUTH_TOKEN;
char ssid[] = "Hari's Galaxy A238785";
char pass[] = "hari1234";

DHT dht(DHT_PIN, DHT_TYPE);
BlynkTimer timer;
//LiquidCrystal_I2C lcd(0x27, 16, 2);

// ================= Global Variables =================
int gasThreshold = 900;          // Default threshold
String locationName = "Traffic Area";  // Default location
int pm25Threshold = 200;         // PM2.5 µg/m³ alert threshold

// ================= Location Menu (V4) =================
BLYNK_WRITE(V4) {
  int location = param.asInt();
  switch (location) {
    case 0: gasThreshold = 500; locationName = "Home"; break;
    case 1: gasThreshold = 600; locationName = "Office"; break;
    case 2: gasThreshold = 800; locationName = "Industry"; break;
    case 3: gasThreshold = 900; locationName = "Traffic Area"; break;
    default: gasThreshold = 600; locationName = "Office"; break;
  }
  Serial.printf("📌 Threshold Updated: %d for %s\n", gasThreshold, locationName.c_str());
}

// ================= Dust Sensor Function =================
float readPM25() {
  // Timing from Sharp datasheet
  digitalWrite(DUST_LED_PIN, LOW);        // LED ON (active LOW)
  delayMicroseconds(280);                 // wait 0.28ms
  int raw = analogRead(DUST_PIN);         // sample
  delayMicroseconds(40);                  // keep LED ON total 0.32ms
  digitalWrite(DUST_LED_PIN, HIGH);       // LED OFF
  delayMicroseconds(9680);                // rest of 10ms cycle

  float voltage = (raw * ADC_REF) / ADC_MAX;
  float deltaV = voltage - VOC_BASE;
  if (deltaV <= 0) return 0.0;

  // Sensitivity: 0.5V per 0.1mg/m³ => 0.2 mg/m³ per V
  float dust_mg_m3 = deltaV * 0.2;
  float dust_ug_m3 = dust_mg_m3 * 1000.0;

  return dust_ug_m3;
}

// ================= Sensor Reading Function =================
void sendSensorData() {
  float humidity = dht.readHumidity();
  float temperature = dht.readTemperature();
  int gasValue = analogRead(GAS_SENSOR);
  float pm25 = readPM25();

  if (isnan(humidity) || isnan(temperature)) {
    Serial.println("⚠️ Failed to read from DHT sensor!");
    return;
  }

  // Print Data to Serial
  Serial.println("\n============================");
  Serial.println("🌍 Air Quality Monitoring System");
  Serial.println("============================");
  Serial.printf("📍 Location: %s\n", locationName.c_str());
  Serial.printf("💨 Gas Level: %d (Threshold: %d)\n", gasValue, gasThreshold);
  Serial.printf("🌡️ Temp: %.1f°C | 💧 Humidity: %.1f%%\n", temperature, humidity);
  Serial.printf("🌫️ PM2.5: %.1f µg/m³ (Threshold: %d)\n", pm25, pm25Threshold);
  Serial.println("============================");

  // Send Data to Blynk (V0=Gas, V1=Temp, V2=Humidity, V6=PM2.5)
  Blynk.virtualWrite(V0, gasValue);
  Blynk.virtualWrite(V1, temperature);
  Blynk.virtualWrite(V2, humidity);
  Blynk.virtualWrite(V6, pm25);

  // ================= Alert Handling =================
  bool alert = false;
  String alertMsg = "";

  if (gasValue > gasThreshold) {
    alert = true;
    alertMsg += "⚠️ High Gas Level! ";
  }
  if (pm25 > pm25Threshold) {
    alert = true;
    alertMsg += "⚠️ PM2.5 Exceeded! ";
  }

  if (alert) {
    Serial.println(alertMsg);
    digitalWrite(ALERT_PIN, HIGH);
    Blynk.virtualWrite(V5, 1);
    Blynk.logEvent("air_quality_alert", alertMsg);
  } else {
    Serial.println("✅ Air Quality is NORMAL.");
    digitalWrite(ALERT_PIN, LOW);
    Blynk.virtualWrite(V5, 0);
  }
}

// ================= Setup =================
void setup() {
  Serial.begin(115200);
  Serial.println("🚀 Starting Air Quality Monitoring System...");

  Blynk.begin(auth, ssid, pass);

  pinMode(GAS_SENSOR, INPUT);
  pinMode(ALERT_PIN, OUTPUT);
  pinMode(DUST_LED_PIN, OUTPUT);
  digitalWrite(DUST_LED_PIN, HIGH); // LED off initially
  dht.begin();

  delay(2000);
  timer.setInterval(2000L, sendSensorData);
}

// ================= Loop =================
void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("🔄 Reconnecting to WiFi...");
    WiFi.begin(ssid, pass);
    while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      Serial.print(".");
    }
    Blynk.connect();
  }
  Blynk.run();
  timer.run();
}
