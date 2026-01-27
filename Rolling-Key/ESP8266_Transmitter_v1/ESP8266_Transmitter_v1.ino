/*
  ESP8266 Receiver (Device B) - OTP + Fingerprint + Iris Authentication + Admin Enrollment
  - Receives rolling OTP via ESP-NOW
  - Verifies OTP (15s validity)
  - Fingerprint verification
  - Redirects to Iris verification page
  - Admin page for live fingerprint enrollment
  - Alerts with RED LED + buzzer if Iris fails
*/

#include <ESP8266WiFi.h>
#include <espnow.h>
#include <ESP8266WebServer.h>
#include <Adafruit_Fingerprint.h>
#include <SoftwareSerial.h>
#include <FS.h>
#include <ESP8266HTTPClient.h>

// ===== CONFIG =====
const char* ssid = "Secure_OTP_AP";
const char* password = "12345678";

// Replace with your transmitter MAC
uint8_t transmitterMAC[6] = {0x48, 0x3F, 0xDA, 0x61, 0xBA, 0x18};

// LEDs & Buzzer
#define RED_LED   D5
#define GREEN_LED D6
#define BUZZER    D7

ESP8266WebServer server(80);

typedef struct __attribute__((packed)) {
  uint32_t counter;
  char otp[9];
} otp_packet_t;

otp_packet_t lastPacket;
unsigned long lastReceivedTime = 0;
bool newData = false;
unsigned long ledTimer = 0;
const unsigned long LED_DURATION_MS = 3000;

// ===== Fingerprint =====
#define RX_PIN D1
#define TX_PIN D2
SoftwareSerial fingerSerial(RX_PIN, TX_PIN);
Adafruit_Fingerprint finger(&fingerSerial);

// Iris verification
bool irisVerified = false;
const char* FLASK_SERVER = "http://192.168.4.2:8501/verify_iris";

// Hidden Admin page URL
const String adminURL = "/admin123xyz";

// ================= HELPERS =================
void printMac(const uint8_t *mac) {
  for (int i=0; i<6; i++) {
    if (mac[i] < 16) Serial.print('0');
    Serial.print(mac[i], HEX);
    if (i<5) Serial.print(":");
  }
  Serial.println();
}

// ================= ESP-NOW =================
void OnDataRecv(uint8_t * mac, uint8_t *incomingData, uint8_t len) {
  Serial.println("📡 Packet Received via ESP-NOW");
  bool macMatch = true;
  for(int i=0;i<6;i++) if(mac[i]!=transmitterMAC[i]) {macMatch=false; break;}
  if(!macMatch){ Serial.println("❌ Unauthorized device!"); return; }
  if(len!=sizeof(otp_packet_t)){ Serial.println("⚠️ Packet size mismatch!"); return; }
  memcpy(&lastPacket,incomingData,sizeof(otp_packet_t));
  lastReceivedTime = millis();
  newData = true;
  Serial.print("✅ OTP received: "); Serial.println(lastPacket.otp);
}

// ================= FINGERPRINT =================
bool verifyFingerprint() {
  Serial.println("Place finger to verify...");
  int p = -1;
  unsigned long start = millis();
  while ((millis()-start)<10000) {
    p = finger.getImage();
    if(p==FINGERPRINT_NOFINGER) continue; else break;
  }
  if(p!=FINGERPRINT_OK){ Serial.println("No finger detected"); return false; }
  p = finger.image2Tz();
  if(p!=FINGERPRINT_OK){ Serial.println("Error converting image"); return false; }
  p = finger.fingerFastSearch();
  if(p==FINGERPRINT_OK){
    Serial.print("Fingerprint matched ID: "); Serial.println(finger.fingerID);
    return true;
  } else { Serial.println("Fingerprint not found"); return false; }
}

// ================= WEB UI =================
void handleRoot() {
  String html = R"rawliteral(
<!DOCTYPE html><html><head><meta charset='UTF-8'>
<title>Secure File Access</title>
<style>
body{font-family:Arial,sans-serif;background:linear-gradient(135deg,#1e3c72,#2a5298);color:white;text-align:center;margin:0;padding:0;}
.container{display:flex;flex-direction:column;align-items:center;justify-content:center;height:100vh;}
.card{background:#fff;color:#333;width:360px;padding:25px;border-radius:15px;box-shadow:0 8px 25px rgba(0,0,0,0.3);}
h2{color:#2a5298;} input{padding:10px;width:80%;margin:10px 0;border-radius:8px;border:1px solid #aaa;}
button{padding:10px 20px;background:#2a5298;color:white;border:none;border-radius:8px;cursor:pointer;}
button:hover{background:#1e3c72;}
p{font-size:12px;color:gray;}
</style></head><body>
<div class="container">
<div class="card">
<h2>🔒 Secure File Access</h2>
<form action='/verify' method='GET'>
<input type='text' name='otp' placeholder='Enter OTP' maxlength='16' required><br>
<button type='submit'>Verify OTP</button>
</form>
<p>OTP valid for 15 seconds</p>
</div>
</div>
</body></html>
)rawliteral";
  server.send(200,"text/html",html);
}

// ================= VERIFY OTP & FINGERPRINT =================
void handleVerify() {
  String enteredOTP = server.arg("otp");
  Serial.print("🔑 OTP Entered: "); Serial.println(enteredOTP);
  unsigned long now = millis();
  String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><title>Secure Access</title></head><body><div style='text-align:center;padding:30px;'>";
  digitalWrite(GREEN_LED,LOW); digitalWrite(RED_LED,LOW); digitalWrite(BUZZER,LOW);

  if(newData && enteredOTP==String(lastPacket.otp) && (now-lastReceivedTime<=15000)){
    Serial.println("✅ OTP Correct | Verify Fingerprint");
    if(verifyFingerprint()){
        Serial.println("✅ Fingerprint Verified | Redirecting to Iris Page");
        server.sendHeader("Location", "http://192.168.4.2:8501"); // Flask server
        server.send(302, "text/plain", "Redirecting to Iris verification page...");
        digitalWrite(GREEN_LED,HIGH);
    } else {
        Serial.println("❌ Fingerprint Not Recognized");
        digitalWrite(RED_LED,HIGH);
        html += "<h2>❌ Fingerprint Not Recognized</h2><p>Access Denied</p>";
        server.send(200,"text/html",html);
    }
  } else if(newData && enteredOTP==String(lastPacket.otp)){
    Serial.println("⚠️ OTP Expired");
    digitalWrite(RED_LED,HIGH);
    html += "<h2>⚠️ OTP Expired</h2><p>Please request a new OTP</p>";
    server.send(200,"text/html",html);
  } else {
    Serial.println("❌ OTP Incorrect");
    digitalWrite(RED_LED,HIGH);
    html += "<h2>❌ Invalid OTP</h2><p>Access Denied</p>";
    server.send(200,"text/html",html);
  }

  ledTimer = now;
}

// ================= ADMIN PAGE =================
void handleEnrollPage() {
  String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset='UTF-8'>
<title>Fingerprint Enrollment</title>
<style>
body{font-family:'Segoe UI',sans-serif;margin:0;padding:0;background:#141e30;color:white;}
.container{display:flex;flex-direction:column;align-items:center;justify-content:center;height:100vh;}
.card{background:#fff;color:#333;width:500px;padding:30px;border-radius:15px;box-shadow:0 8px 25px rgba(0,0,0,0.3);text-align:center;}
h2{color:#2a5298;margin-bottom:20px;}
input{padding:12px;width:70%;margin:10px 0;border-radius:8px;border:1px solid #aaa;}
button{padding:12px 25px;background:#2a5298;color:white;border:none;border-radius:8px;cursor:pointer;transition:0.3s;}
button:hover{background:#1e3c72;}
#log{margin-top:20px;text-align:left;background:#f0f2f5;color:#333;padding:15px;border-radius:10px;max-height:250px;overflow-y:auto;font-size:14px;}
.progress-container{background:#ddd;border-radius:15px;width:100%;height:20px;margin-top:20px;}
.progress-bar{background:#2a5298;height:100%;width:0%;border-radius:15px;transition:width 0.5s;}
.icon{margin-right:8px;}
</style>
</head>
<body>
<div class="container">
<div class="card">
<h2>🔑 Enroll New Fingerprint</h2>
<input type="number" id="fingerID" placeholder="Enter ID (1-200)" min="1" max="200">
<br><button id="startEnroll">Start Enrollment</button>
<div class="progress-container">
  <div class="progress-bar" id="progressBar"></div>
</div>
<div id="log"></div>
<br><button onclick='location.href="/"'>Home</button>
</div>
</div>

<script>
function appendLog(msg, status){
  let icon = status=='success'?'✅':status=='error'?'❌':'⏳';
  let logDiv = document.getElementById('log'); 
  logDiv.innerHTML += "<span class='icon'>"+icon+"</span>"+msg+"<br>"; 
  logDiv.scrollTop = logDiv.scrollHeight;
}
function updateProgress(step){
  let progress = step*25;
  document.getElementById('progressBar').style.width = progress+"%";
}
document.getElementById('startEnroll').addEventListener('click',function(){
  let id = document.getElementById('fingerID').value;
  if(id<1 || id>200){ alert("ID must be 1-200"); return; }
  
  appendLog("Enrollment started for ID "+id,"pending");
  updateProgress(0);
  function step(n){
    fetch("/enroll_step?id="+id+"&step="+n)
    .then(res => res.text())
    .then(resp => {
      if(resp.includes("next")){
        appendLog(resp.replace(" next",""),"pending");
        updateProgress(n);
        step(n+1);
      } else if(resp.includes("✅")){
        appendLog(resp,"success");
        updateProgress(4);
      } else if(resp.includes("❌")){
        appendLog(resp,"error");
      }
    });
  }
  step(1);
});
</script>
</body>
</html>
)rawliteral";

  server.send(200,"text/html",html);
}

// ================= ADMIN ENROLL STEP =================
void handleEnrollStep() {
  if(!server.hasArg("id") || !server.hasArg("step")){ server.send(400,"text/plain","Missing parameters"); return; }
  int enrollID = server.arg("id").toInt();
  int stepNum = server.arg("step").toInt();
  
  String msg = "";
  int p=-1;

  switch(stepNum){
    case 1:
      msg = "Place finger on sensor (Step 1)..."; 
      while(p!=FINGERPRINT_OK){ p=finger.getImage(); if(p==FINGERPRINT_NOFINGER) continue; else break;}
      p=finger.image2Tz(1);
      if(p!=FINGERPRINT_OK){ server.send(200,"text/plain","❌ Error converting image Step 1"); return;}
      msg += " next"; break;
    case 2:
      msg = "Remove finger and place same finger again (Step 2)..."; delay(2000);
      p=-1; while(p!=FINGERPRINT_OK){ p=finger.getImage(); if(p==FINGERPRINT_NOFINGER) continue; else break;}
      p=finger.image2Tz(2);
      if(p!=FINGERPRINT_OK){ server.send(200,"text/plain","❌ Error converting image Step 2"); return;}
      msg += " next"; break;
    case 3:
      p=finger.createModel();
      if(p!=FINGERPRINT_OK){ server.send(200,"text/plain","❌ Error creating model"); return;}
      msg = "Model created successfully next"; break;
    case 4:
      p=finger.storeModel(enrollID);
      if(p!=FINGERPRINT_OK){ server.send(200,"text/plain","❌ Error storing fingerprint"); return;}
      msg = "✅ Fingerprint enrolled successfully"; break;
    default:
      msg = "✅ Enrollment completed";
  }

  server.send(200,"text/plain",msg);
}

// ================= SETUP =================
void setup() {
  Serial.begin(115200);
  pinMode(GREEN_LED,OUTPUT); pinMode(RED_LED,OUTPUT); pinMode(BUZZER,OUTPUT);
  digitalWrite(GREEN_LED,LOW); digitalWrite(RED_LED,LOW); digitalWrite(BUZZER,LOW);

  // Fingerprint sensor
  fingerSerial.begin(57600); finger.begin(57600);
  if(finger.verifyPassword()) Serial.println("Fingerprint sensor detected!"); 
  else { Serial.println("Sensor not found"); while(1) delay(1); }

  // WiFi + ESP-NOW
  WiFi.mode(WIFI_STA); WiFi.softAP(ssid,password);
  Serial.print("AP IP: "); Serial.println(WiFi.softAPIP());
  Serial.print("MAC: "); Serial.println(WiFi.macAddress());
  if(esp_now_init()!=0){ Serial.println("ESP-NOW init failed"); return; }
  esp_now_set_self_role(ESP_NOW_ROLE_SLAVE);
  esp_now_register_recv_cb(OnDataRecv);
  esp_now_add_peer(transmitterMAC,ESP_NOW_ROLE_CONTROLLER,1,NULL,0);

  // Web routes
  server.on("/",handleRoot);
  server.on("/verify",HTTP_GET,handleVerify);
  server.on(adminURL.c_str(),HTTP_GET,handleEnrollPage);
  server.on("/enroll_step",HTTP_GET,handleEnrollStep);
  server.begin();
  Serial.println("🌐 Web server started!");
}

// ================= LOOP =================
void loop() {
  server.handleClient();
  if(ledTimer>0 && millis()-ledTimer>LED_DURATION_MS){ 
    digitalWrite(GREEN_LED,LOW); 
    digitalWrite(RED_LED,LOW);
    digitalWrite(BUZZER,LOW);
    ledTimer=0;
  }
  delay(10);
}
