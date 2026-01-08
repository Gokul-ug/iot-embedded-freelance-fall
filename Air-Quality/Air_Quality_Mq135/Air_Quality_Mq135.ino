#define BLYNK_TEMPLATE_ID "TMPL3p33eWIZH"
#define BLYNK_TEMPLATE_NAME "Air Quality"
#define BLYNK_AUTH_TOKEN "TUAigJJJsA5JcSXRnuo8c2UpQZVsuMqr"

#define BLYNK_PRINT Serial
#include <WiFi.h>
#include <BlynkSimpleEsp32.h>
#include <DHT.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// ================= Pin Definitions =================
#define GAS_SENSOR 34   // MQ-135 AO → D34 (GPIO34, input only)
#define DHT_PIN    4    // DHT11 Data → D4
#define DHT_TYPE   DHT11
#define ALERT_PIN  5    // Buzzer/LED → D5
// LCD I2C: SDA → D21, SCL → D22

// ================= WiFi & Blynk =================
char auth[] = BLYNK_AUTH_TOKEN;
char ssid[] = "Harish";
char pass[] = "harish1234";

DHT dht(DHT_PIN, DHT_TYPE);
BlynkTimer timer;
LiquidCrystal_I2C lcd(0x27, 16, 2);  // I2C LCD Address (0x27 or 0x3F)

// ================= Global Variables =================
int gasThreshold = 900;          // Default threshold
String locationName = "Traffic Area";  // Default location

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

// ================= Sensor Reading Function =================
void sendSensorData() {
  float humidity = dht.readHumidity();
  float temperature = dht.readTemperature();
  int gasValue = analogRead(GAS_SENSOR);

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
  Serial.println("============================");

  // Send Data to Blynk (V0=Gas, V1=Temp, V2=Humidity)
  Blynk.virtualWrite(V0, gasValue);
  Blynk.virtualWrite(V1, temperature);
  Blynk.virtualWrite(V2, humidity);

  // ================= Display on LCD =================
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Gas:");
  lcd.print(gasValue);
  lcd.print("/");
  lcd.print(gasThreshold);

  lcd.setCursor(0, 1);
  lcd.print("T:");
  lcd.print(temperature, 1);
  lcd.print("C H:");
  lcd.print(humidity, 1);
  lcd.print("%");

  // ================= Alert Handling =================
  if (gasValue > gasThreshold) {
    Serial.println("⚠️ ALERT: High Pollution Detected!");
    digitalWrite(ALERT_PIN, HIGH);   // Buzzer/LED ON
    Blynk.virtualWrite(V5, 1);       // Alert LED ON in Blynk
    Blynk.logEvent("air_quality_alert", "⚠️ High Pollution Detected! Take action!");

    // Show Alert on LCD
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("!! HIGH ALERT !!");
    lcd.setCursor(0, 1);
    lcd.print("Gas:");
    lcd.print(gasValue);
  } else {
    Serial.println("✅ Air Quality is NORMAL.");
    digitalWrite(ALERT_PIN, LOW);    // Buzzer/LED OFF
    Blynk.virtualWrite(V5, 0);       // Alert LED OFF
  }
}

// ================= Setup =================
void setup() {
  Serial.begin(115200);
  Serial.println("🚀 Starting Air Quality Monitoring System...");

  // WiFi & Blynk
  Blynk.begin(auth, ssid, pass);

  // Init Sensors & LCD
  pinMode(GAS_SENSOR, INPUT);
  pinMode(ALERT_PIN, OUTPUT);
  dht.begin();
  Wire.begin(21, 22); // SDA=21, SCL=22
  lcd.init();
  lcd.backlight();

  lcd.setCursor(0, 0);
  lcd.print("Air Quality Init");
  lcd.setCursor(0, 1);
  lcd.print("Loading...");
  delay(2000);

  // Repeat Sensor Reading every 2 sec
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
