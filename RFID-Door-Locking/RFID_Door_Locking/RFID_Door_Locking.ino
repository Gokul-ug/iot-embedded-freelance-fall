#include <SPI.h>
#include <MFRC522.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <ESP32Servo.h>
#include <Keypad.h>

// RFID module pins
#define RST_PIN 22
#define SS_PIN 5
MFRC522 rfid(SS_PIN, RST_PIN);

// LED + Buzzer (shared pin D2)
#define ALERT_PIN 2

// Servo
#define SERVO_PIN 25
Servo doorServo;

// I2C LCD (16x2 with address 0x27)
LiquidCrystal_I2C lcd(0x27, 16, 2);

// Multiple Authorized RFID UIDs
byte authorizedUIDs[][4] = {
  {0xF3, 0x19, 0xB5, 0x02},   // card 1
  {0x7A, 0x0B, 0x8C, 0x05},   // card monesh
  {0x12, 0x34, 0x56, 0x78}    // card 2 (example)
};
const int totalAuthorized = 2;

// Keypad setup
const byte ROWS = 4; 
const byte COLS = 4; 
char keys[ROWS][COLS] = {
  {'1','2','3','A'},
  {'4','5','6','B'},
  {'7','8','9','C'},
  {'*','0','#','D'}
};
byte rowPins[ROWS] = {32, 33, 26, 27}; 
byte colPins[COLS] = {14, 12, 13, 4};  
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

// PIN Code
String correctPIN = "9090";
String enteredPIN = "";

// Security variables
bool waitingForPIN = false;
unsigned long pinStartTime = 0;
int failedAttempts = 0;
const unsigned long PIN_TIMEOUT = 10000; // 10s timeout

// ---------------- Utility Functions ----------------
bool isAuthorized(byte *uid) {
  for (int i=0; i < totalAuthorized; i++) {
    bool match = true;
    for (int j=0; j<4; j++) {
      if (uid[j] != authorizedUIDs[i][j]) {
        match = false;
        break;
      }
    }
    if (match) return true;
  }
  return false;
}

void lcdClearLine(int line) {
  lcd.setCursor(0, line);
  lcd.print("                "); // clear 16 chars
  lcd.setCursor(0, line);
}

// ---------------- Setup ----------------
void setup() {
  delay(1000);
  Serial.begin(115200);
  Serial.println("System Booting...");

  pinMode(ALERT_PIN, OUTPUT);
  digitalWrite(ALERT_PIN, LOW);

  // Init RFID
  SPI.begin();
  rfid.PCD_Init();
  delay(200);

  // Init LCD
  Wire.begin(21, 15);
  lcd.init();
  lcd.backlight();
  lcdClearLine(0);
  lcd.print("Server Room Lock");
  lcdClearLine(1);
  lcd.print("Scan RFID Card ");
  delay(300);

  // Init Servo last
  doorServo.attach(SERVO_PIN);
  doorServo.write(0);
  delay(200);

  Serial.println("System Ready ✅");
}

// ---------------- Loop ----------------
void loop() {
  if (!waitingForPIN) {
    if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
      handleRFID();
    }
  }

  if (waitingForPIN) {
    char key = keypad.getKey();
    if (key) handleKeypadInput(key);

    if (millis() - pinStartTime > PIN_TIMEOUT) {
      lcdClearLine(1);
      lcd.print("Timeout! Reset ");
      resetAuth();
      delay(2000);
      lcdClearLine(0);
      lcd.print("Server Room Lock");
      lcdClearLine(1);
      lcd.print("Scan RFID Card ");
    }
  }
}

// ---------------- RFID Handling ----------------
void handleRFID() {
  Serial.print("Card UID: ");
  for (byte i = 0; i < rfid.uid.size; i++) {
    Serial.print(rfid.uid.uidByte[i], HEX);
    if (i != rfid.uid.size - 1) Serial.print(":");
  }
  Serial.println();

  bool authorized = (rfid.uid.size == 4 && isAuthorized(rfid.uid.uidByte));

  if (authorized) {
    lcdClearLine(1);
    lcd.print("Enter PIN:     ");
    waitingForPIN = true;
    pinStartTime = millis();
    Serial.println("RFID OK -> Waiting for PIN");
  } else {
    accessDenied("RFID Failed");
  }

  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
}

// ---------------- Keypad Handling ----------------
void handleKeypadInput(char key) {
  if (key == '#') { // Submit
    if (enteredPIN == correctPIN) {
      grantAccess();
    } else {
      accessDenied("PIN Failed");
    }
  } else if (key == '*') { // Clear
    enteredPIN = "";
    lcdClearLine(1);
    lcd.print("Cleared        ");
  } else {
    enteredPIN += key;
    lcdClearLine(1);
    lcd.print("PIN: ");
    for (int i=0; i<enteredPIN.length(); i++) {
      lcd.print("*");
    }
    pinStartTime = millis();
  }
}

// ---------------- Access Control ----------------
void grantAccess() {
  lcdClearLine(1);
  lcd.print("Access Granted ");
  Serial.println("ACCESS GRANTED ✅");

  digitalWrite(ALERT_PIN, HIGH); // LED+BUZZER ON
  delay(200); 
  digitalWrite(ALERT_PIN, LOW);

  doorServo.write(90);
  delay(5000);
  doorServo.write(0);
  digitalWrite(ALERT_PIN, LOW);
  lcdClearLine(1);
  lcd.print("Door Locked    ");
  resetAuth();
  delay(2000);
  lcdClearLine(0);
  lcd.print("Server Room Lock");
  lcdClearLine(1);
  lcd.print("Scan RFID Card ");
}

void accessDenied(String reason) {
  Serial.println("ACCESS DENIED ❌ : " + reason);
  lcdClearLine(1);
  lcd.print("Access Denied  ");

  // 3 sec alert (LED + Buzzer on D2)
  digitalWrite(ALERT_PIN, HIGH);
  delay(2000);
  digitalWrite(ALERT_PIN, LOW);

  failedAttempts++;
  if (failedAttempts >= 3) {
    lcdClearLine(1);
    lcd.print("System Locked! ");
    Serial.println("SYSTEM LOCKED 🔒 for 30s");
    delay(30000);
    failedAttempts = 0;
  }

  resetAuth();
  delay(2000);
  lcdClearLine(0);
  lcd.print("Server Room Lock");
  lcdClearLine(1);
  lcd.print("Scan RFID Card ");
}

// ---------------- Helpers ----------------
void resetAuth() {
  enteredPIN = "";
  waitingForPIN = false;
}
