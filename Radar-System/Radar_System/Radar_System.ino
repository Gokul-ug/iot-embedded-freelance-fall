#define BLYNK_TEMPLATE_NAME "Radar system"
#define BLYNK_TEMPLATE_ID "TMPL3iVwVCaNU"
#define BLYNK_AUTH_TOKEN "6KrYcEoT07lO4wLDMqz7HPDtLoar_47E"

#include <WiFi.h>
#include <WebServer.h>
#include <ESP32Servo.h>
#include <BlynkSimpleEsp32.h>

#define TRIG_PIN 4
#define ECHO_PIN 5
#define BUZZER_PIN 13
#define SERVO_PIN 12

Servo myServo;
const char* ssid = "Gtech";          // Wi-Fi SSID
const char* password = "12345678";   // Wi-Fi Password

WebServer server(88);

int angle = 0;
bool increasing = true;
float distance = 0;
String objectData = "";

int detectionRange = 150; // cm
unsigned long lastAlertTime = 0; // prevent flooding

// ---------- HTML Radar UI ----------
String radarUI() {
  return R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<title>ESP32 Radar</title>
<style>
body { margin:0; overflow:hidden; background:black; }
canvas { display:block; margin:auto; }
#count {
  position: absolute;
  top: 10px;
  left: 10px;
  color: red;
  font-size: 24px;
  font-weight: bold;
  z-index: 10;
}
#rangeBox {
  position: absolute;
  top: 50px;
  left: 10px;
  color: white;
  font-size: 18px;
}
</style>
</head>
<body>
<div id="count">Objects: 0</div>
<div id="rangeBox">
  Range: <span id="rangeVal">100</span> cm<br>
  <input type="range" id="rangeSlider" min="50" max="200" value="100" step="10">
</div>
<canvas id="radar"></canvas>
<script>
const canvas = document.getElementById("radar");
const ctx = canvas.getContext("2d");

// keep square canvas to avoid stretching
const size = Math.min(window.innerWidth, window.innerHeight) - 40;
canvas.width = size;
canvas.height = size;

const centerX = canvas.width / 2;
const centerY = canvas.height;
const radius = canvas.width / 2 - 40;

let currentAngle = 0;
let direction = 1;
let objects = [];
let maxRange = 100; // default

document.getElementById("rangeSlider").oninput = function() {
  maxRange = this.value;
  document.getElementById("rangeVal").innerText = maxRange;
  fetch("/setRange?val=" + maxRange); // send to ESP32
};

function drawRadar() {
  ctx.clearRect(0, 0, canvas.width, canvas.height);

  // Grid
  ctx.strokeStyle = "green";
  ctx.lineWidth = 1;
  for (let r = 50; r < radius; r += 50) {
    ctx.beginPath();
    ctx.arc(centerX, centerY, r, Math.PI, 2 * Math.PI);
    ctx.stroke();
  }

  // Degree lines
  for (let a = 0; a <= 180; a += 30) {
    const rad = (a * Math.PI) / 180;
    const x = centerX + radius * Math.cos(rad);
    const y = centerY - radius * Math.sin(rad);
    ctx.beginPath();
    ctx.moveTo(centerX, centerY);
    ctx.lineTo(x, y);
    ctx.stroke();
    ctx.fillStyle = 'lime';
    ctx.font = "14px Arial";
    ctx.fillText(a + "°", x - 20, y - 5);
  }

  // Sweep line
  const rad = (currentAngle * Math.PI) / 180;
  let x = centerX + radius * Math.cos(rad);
  let y = centerY - radius * Math.sin(rad);
  ctx.strokeStyle = 'lime';
  ctx.lineWidth = 2;
  ctx.beginPath();
  ctx.moveTo(centerX, centerY);
  ctx.lineTo(x, y);
  ctx.stroke();

  // Objects
  let count = 0;
  for (let obj of objects) {
    const a = parseInt(obj.angle);
    const d = parseInt(obj.distance);
    let scaled = (d / maxRange) * radius;
    if (scaled > radius) scaled = radius;  // cap at radar edge
    const angleRad = (a * Math.PI) / 180;
    const ox = centerX + (scaled * Math.cos(angleRad));
    const oy = centerY - (scaled * Math.sin(angleRad));
    ctx.fillStyle = 'red';
    ctx.beginPath();
    ctx.arc(ox, oy, 3, 0, 2 * Math.PI); // 🔴 smaller points
    ctx.fill();
    count++;
  }
  document.getElementById("count").innerText = "Objects: " + count;
}

function animate() {
  drawRadar();
  currentAngle += direction;
  if (currentAngle >= 180 || currentAngle <= 0) direction *= -1;
  requestAnimationFrame(animate);
}

setInterval(() => {
  fetch("/data")
    .then(res => res.text())
    .then(data => {
      objects = [];
      if (data.trim() != "") {
        const lines = data.trim().split(";");
        for (let line of lines) {
          if (line != "") {
            const [angle, distance] = line.split(",");
            objects.push({ angle, distance });
          }
        }
      }
    });
}, 200);

animate();
</script>
</body>
</html>
)rawliteral";
}

// ---------- Measure Distance ----------
float getDistance() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  long duration = pulseIn(ECHO_PIN, HIGH, 20000);
  float d = duration * 0.034 / 2;
  return (d > 200) ? 200 : d; // cap at 200cm
}

// ---------- Setup ----------
void setup() {
  Serial.begin(115200);
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  myServo.attach(SERVO_PIN);

  // Connect to Wi-Fi
  Serial.print("Connecting to WiFi");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected!");
  Serial.print("ESP32 IP Address: ");
  Serial.println(WiFi.localIP());

  // Start Blynk
  Blynk.begin(BLYNK_AUTH_TOKEN, ssid, password);

  // Local Web UI
  server.on("/", []() {
    server.send(200, "text/html", radarUI());
  });

  server.on("/data", []() {
    server.send(200, "text/plain", objectData);
  });

  server.on("/setRange", []() {
    if (server.hasArg("val")) {
      detectionRange = server.arg("val").toInt();
      Serial.print("Range updated to: ");
      Serial.println(detectionRange);
    }
    server.send(200, "text/plain", "OK");
  });

  server.begin();
  Serial.println("Web server started.");
}

// ---------- Loop ----------
void loop() {
  Blynk.run();
  server.handleClient();

  myServo.write(angle);
  delay(10);  // smoother & faster sweep
  distance = getDistance();

  // Serial debug
  Serial.print("Angle: ");
  Serial.print(angle);
  Serial.print("°  Distance: ");
  Serial.print(distance);
  Serial.println(" cm");

  // Object detected
  if (distance < detectionRange) {
    objectData += String(angle) + "," + String(distance) + ";";

    // 🚨 Alert zone (≤ 30 cm)
    if (distance <= 30 && millis() - lastAlertTime > 5000) {
      String msg = "⚠ Alert! Object at " + String(angle) + "° , " + String(distance) + " cm";
      Serial.println("Sending Blynk Alert: " + msg);
      Blynk.logEvent("alert", msg);
      lastAlertTime = millis();

      // 🔔 Beep buzzer only during alert
      digitalWrite(BUZZER_PIN, HIGH);
      delay(500);  // buzzer ON for 0.5 sec
      digitalWrite(BUZZER_PIN, LOW);
    }
  }

  // Sweep
  if (increasing) {
    angle++;
    if (angle >= 180) {
      increasing = false;
      objectData = ""; // clear after full sweep
    }
  } else {
    angle--;
    if (angle <= 0) {
      increasing = true;
      objectData = ""; // clear after full sweep
    }
  }
}
