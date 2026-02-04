// ===== Blynk credentials =====
#define BLYNK_TEMPLATE_ID        "TMPL3WBAMM_qo"
#define BLYNK_TEMPLATE_NAME      "Thermal Camera"
#define BLYNK_AUTH_TOKEN         "XzwJiiAOnFtZkQmD2CyPLD-tQ7LGMCjX"

#include <WiFi.h>
#include <Wire.h>
#include <Adafruit_AMG88xx.h>
#include "DHT.h"
#include <BlynkSimpleEsp32.h>

// ===== WiFi credentials =====
const char* ssid = "Gtech";
const char* password = "12345678";

// ===== Sensors =====
Adafruit_AMG88xx amg;
float pixels[64]; // 8x8 thermal pixels

#define DHTPIN 4
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);
float ambientTemp = 0, ambientHum = 0;

// Manual mode variables
bool manualMode = false;
float manualTemp = 25.0;
float manualHum  = 50.0;

// ===== Buzzer =====
#define BUZZER_PIN 23
float prevPixels[64];
float alertThreshold = 2.0; // temperature difference to trigger buzzer

// ===== Web server =====
WiFiServer server(80);

// ===== Display settings =====
String displayMode = "pixel";
int pixelDensity = 32;

void setup() {
  Serial.begin(115200);
  Wire.begin(21, 22);

  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  if (!amg.begin()) {
    Serial.println("AMG8833 not detected!");
    while (1);
  }
  dht.begin();

  for(int i = 0; i < 64; i++) prevPixels[i] = 0;

  Serial.print("Connecting to WiFi...");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("Connected!");
  Serial.print("IP Address: "); Serial.println(WiFi.localIP());

  Blynk.begin(BLYNK_AUTH_TOKEN, ssid, password);

  server.begin();
}

// ===== HTML page =====
String generateHTML() {
  String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<title>ESP32 Thermal Camera</title>
<style>
body { margin:0; font-family:'Segoe UI', sans-serif; background:#0f0f12; color:#eee;}
header { text-align:center; padding:15px; background:linear-gradient(90deg,#1b1b1f,#111); box-shadow:0 2px 10px rgba(0,0,0,0.5);}
.main-container { display:flex; justify-content:center; align-items:center; gap:20px; padding:15px; flex-wrap:wrap; position:relative; }
canvas { border-radius:12px; box-shadow:0 0 20px rgba(0,255,255,0.3);}
.controls { background: rgba(20,20,30,0.8); padding:15px; border-radius:12px; box-shadow:0 0 15px rgba(0,255,255,0.4); min-width:240px;}
.controls input, .controls select, .controls button { margin:5px; padding:6px 10px; border-radius:6px; border:none; font-size:14px; background:#222; color:#0ff;}
.controls button:hover { background:#0ff; color:#111; cursor:pointer; box-shadow:0 0 10px #0ff;}
.legend { width:100%; height:20px; display:flex; margin:10px 0; border-radius:6px; overflow:hidden; }
.legend div { flex:1; }
.switch-container { display:flex; align-items:center; gap:10px; margin-top:10px; }
.switch { width:50px; height:26px; background:#222; border-radius:20px; position:relative; cursor:pointer; transition:0.3s; }
.switch.neon { box-shadow: 0 0 5px #0ff, 0 0 10px #0ff, 0 0 20px #0ff; }
.slider { width:22px; height:22px; background:#0ff; border-radius:50%; position:absolute; top:2px; left:2px; transition:0.3s; }
.switch.active .slider { left:26px; background:#ff0; box-shadow: 0 0 5px #ff0,0 0 10px #ff0; }
</style>
</head>
<body>
<header>
<h1>ESP32 Thermal Camera</h1>
<p>Ambient Temp: <span id="tempSpan">--</span> °C &nbsp;&nbsp; Humidity: <span id="humSpan">--</span> %</p>
</header>

<div class="main-container">
  <canvas id="heatmap" width="320" height="320"></canvas>
  <div class="controls">
    <h3>Controls</h3>
    <p><b>DHT Mode:</b></p>
    <button onclick="setMode('auto')">Auto (DHT11)</button>
    <button onclick="setMode('manual')">Manual</button><br>
    Temp: <input type="number" id="manualTemp" step="0.5" value="25"><br>
    Humidity: <input type="number" id="manualHum" step="0.5" value="50"><br>
    <button onclick="applyManual()">Apply Manual Values</button><br><br>

    <div id="minMaxSection" style="display:none;">
      Min Temp: <input type="number" id="minInput" step="0.5" readonly><br>
      Max Temp: <input type="number" id="maxInput" step="0.5" readonly><br>
      <button onclick="applyRange()">Apply</button><br>
    </div>

    <div class="switch-container">
      <span>Mode:</span>
      <div id="modeSwitch" class="switch neon" onclick="toggleMode()">
        <div class="slider"></div>
      </div>
      <span id="modeLabel">Pixel</span>
    </div><br>

    Pixel Density: <select id="densitySelect" onchange="setDensity()">
      <option value="8">8x8</option>
      <option value="16">16x16</option>
      <option value="32" selected>32x32</option>
      <option value="64">64x64</option>
    </select>

    <div class="legend">
      <div style="background:blue"></div>
      <div style="background:cyan"></div>
      <div style="background:green"></div>
      <div style="background:yellow"></div>
      <div style="background:red"></div>
    </div>
    <p id="scaleText"></p>
  </div>
</div>

<script>
let pixels = [];
let minTemp, maxTemp;
let mode = 'pixel';
let density = 32;

const tempSpan = document.getElementById('tempSpan');
const humSpan = document.getElementById('humSpan');
const scaleText = document.getElementById('scaleText');
const canvas = document.getElementById('heatmap');
const ctx = canvas.getContext('2d');

function fetchData(){
  fetch('/data').then(r=>r.json()).then(json=>{
    pixels = json.pixels;
    density = json.pixelDensity;
    mode = json.displayMode;

    tempSpan.innerText = json.ambientTemp.toFixed(1);
    humSpan.innerText = json.ambientHum.toFixed(1);

    minTemp = json.minTemp;
    maxTemp = json.maxTemp;

    let minInput = document.getElementById('minInput');
    let maxInput = document.getElementById('maxInput');
    minInput.placeholder = minTemp.toFixed(1);
    maxInput.placeholder = maxTemp.toFixed(1);

    document.getElementById('densitySelect').value = density;
    document.getElementById('modeLabel').innerText = (mode==='pixel')?'Pixel':'Smooth';
    if(mode==='smooth'){ document.getElementById('modeSwitch').classList.add('active'); }
    else { document.getElementById('modeSwitch').classList.remove('active'); }

    drawHeatmap();
    scaleText.innerText = minTemp.toFixed(1) + ' °C → ' + maxTemp.toFixed(1) + ' °C';
  });
}

function drawHeatmap(){
  ctx.clearRect(0,0,canvas.width,canvas.height);
  if(!pixels.length) return;
  const block = canvas.width/density;

  let minV = minTemp;
  let maxV = maxTemp;

  if(mode==='pixel'){
    for(let y=0;y<density;y++){
      for(let x=0;x<density;x++){
        let gx=x*(7/(density-1)), gy=y*(7/(density-1));
        let x0=Math.floor(gx), y0=Math.floor(gy);
        let x1=Math.min(x0+1,7), y1=Math.min(y0+1,7);
        let dx=gx-x0, dy=gy-y0;
        let val=pixels[y0*8+x0]*(1-dx)*(1-dy)+pixels[y0*8+x1]*dx*(1-dy)+pixels[y1*8+x0]*(1-dx)*dy+pixels[y1*8+x1]*dx*dy;
        let norm=(val-minV)/(maxV-minV);
        norm=Math.max(0,Math.min(1,norm));
        let hue=(1-norm)*240;
        ctx.fillStyle=`hsl(${hue},100%,50%)`;
        ctx.fillRect(x*block,y*block,block,block);
      }
    }
  } else {
    for(let yy=0;yy<canvas.height;yy++){
      for(let xx=0;xx<canvas.width;xx++){
        let gx=xx/(canvas.width/7), gy=yy/(canvas.height/7);
        let x0=Math.floor(gx), y0=Math.floor(gy);
        let x1=Math.min(x0+1,7), y1=Math.min(y0+1,7);
        let dx=gx-x0, dy=gy-y0;
        let val=pixels[y0*8+x0]*(1-dx)*(1-dy)+pixels[y0*8+x1]*dx*(1-dy)+pixels[y1*8+x0]*(1-dx)*dy+pixels[y1*8+x1]*dx*dy;
        let norm=(val-minV)/(maxV-minV);
        norm=Math.max(0,Math.min(1,norm));
        let hue=(1-norm)*240;
        ctx.fillStyle=`hsl(${hue},100%,50%)`;
        ctx.fillRect(xx,yy,1,1);
      }
    }
  }
}

function applyRange(){}

function toggleMode(){
  mode = (mode==='pixel')?'smooth':'pixel';
  const switchEl = document.getElementById('modeSwitch');
  const labelEl = document.getElementById('modeLabel');
  if(mode==='pixel'){ switchEl.classList.remove('active'); labelEl.innerText='Pixel'; }
  else { switchEl.classList.add('active'); labelEl.innerText='Smooth'; }
  fetch(`/mode?set=${mode}`);
}

function setDensity(){
  density=parseInt(document.getElementById('densitySelect').value);
  fetch(`/density?set=${density}`);
}

function setMode(m){
  fetch(`/dhtmode?set=${m}`);
}

function applyManual(){
  let t = document.getElementById('manualTemp').value;
  let h = document.getElementById('manualHum').value;
  fetch(`/manual?temp=${t}&hum=${h}`);
}

setInterval(fetchData, 500);
fetchData();
</script>
</body>
</html>
)rawliteral";
  return html;
}

// ===== HTTP server =====
void loop() {
  Blynk.run();

  WiFiClient client = server.available();
  if(!client) return;
  while(!client.available()) delay(1);
  String req = client.readStringUntil('\r');
  client.flush();

  // Handle settings
  if(req.indexOf("GET /mode?set=")>=0){
    if(req.indexOf("set=pixel")>0) displayMode="pixel";
    else if(req.indexOf("set=smooth")>0) displayMode="smooth";
  } 
  else if(req.indexOf("GET /density?set=")>=0){
    int idx=req.indexOf("set=");
    if(idx>0) pixelDensity=req.substring(idx+4).toInt();
  }
  else if(req.indexOf("GET /dhtmode?set=")>=0){
    if(req.indexOf("set=auto")>0) manualMode = false;
    else if(req.indexOf("set=manual")>0) manualMode = true;
  }
  else if(req.indexOf("GET /manual?temp=")>=0){
    int tIndex = req.indexOf("temp=");
    int hIndex = req.indexOf("&hum=");
    if(tIndex>0 && hIndex>0){
      manualTemp = req.substring(tIndex+5,hIndex).toFloat();
      manualHum = req.substring(hIndex+5).toFloat();
    }
  }

  // Read sensors
  amg.readPixels(pixels);

  if(manualMode){
    ambientTemp = manualTemp;
    ambientHum  = manualHum;
  } else {
    ambientHum = dht.readHumidity();
    ambientTemp = dht.readTemperature();
    if(isnan(ambientHum)||isnan(ambientTemp)){ ambientHum=50; ambientTemp=25; }
  }

  // Buzzer alert + Blynk notification
  bool alert = false;
  for(int i=0;i<64;i++){
    if(abs(pixels[i]-prevPixels[i]) > alertThreshold){
      alert = true;
      break;
    }
  }
  if(alert){
    digitalWrite(BUZZER_PIN,HIGH);
    delay(500);
    digitalWrite(BUZZER_PIN,LOW);
    Blynk.logEvent("alert","Thermal anomaly detected!");
  }
  for(int i=0;i<64;i++) prevPixels[i] = pixels[i];

  // Serve /data
  if(req.indexOf("GET /data")>=0){
    float minTemp = ambientTemp - 1;
    float maxTemp = ambientTemp + 1.5;
    String data="{\"pixels\":[";
    for(int i=0;i<64;i++){ data+=String(pixels[i],1); if(i<63) data+=","; }
    data += "],\"ambientTemp\":"+String(ambientTemp,1)+",\"ambientHum\":"+String(ambientHum,1);
    data += ",\"minTemp\":"+String(minTemp,1)+",\"maxTemp\":"+String(maxTemp,1);
    data += ",\"pixelDensity\":"+String(pixelDensity)+",\"displayMode\":\""+displayMode+"\"}";
    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: application/json");
    client.println("Connection: close");
    client.println();
    client.println(data);
  } else {
    // Serve HTML
    String html = generateHTML();
    client.println("HTTP/1.1 200 OK");
    client.println("Content-type:text/html");
    client.println("Connection: close");
    client.println();
    client.println(html);
  }
}