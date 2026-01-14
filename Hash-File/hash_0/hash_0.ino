#include <WiFi.h>
#include <WebServer.h>
#include <SPI.h>
#include <SD.h>
#include "mbedtls/sha256.h"

const char* ssid = "Hash";
const char* password = "12345678";

WebServer server(80);

const int chipSelect = 5; // SD card CS
const int ledPin = 2;     // LED for verification

mbedtls_sha256_context ctx;
uint8_t hashResult[32];
String calculatedHash = "";
File uploadFile;
String tempFileName = "/temp.upload";
String uploadedExt = "";
String originalFileName = "";

// ---------------- Convert Hash to Hex String ----------------
String toHexString(uint8_t *hash, size_t len) {
  String hex = "";
  for (size_t i = 0; i < len; i++) {
    char buf[3];
    sprintf(buf, "%02x", hash[i]);
    hex += buf;
  }
  return hex;
}

// ---------------- Upload & Store Page HTML ----------------
String uploadPageHTML(String hash = "", String result = "") {
  String html = R"rawliteral(
  <!DOCTYPE html>
  <html>
  <head>
    <title>Upload & Store</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
   <style>
  body {
    font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
    background: #f0f2f5;
    display: flex;
    flex-direction: column;
    align-items: center;
    margin: 0;
    padding: 20px;
  }

  h2 {
    margin-bottom: 25px;
    color: #333;
    text-align: center;
    font-size: 28px;
  }

  .card {
    background: #fff;
    border-radius: 12px;
    box-shadow: 0 4px 12px rgba(0,0,0,0.1);
    padding: 30px;
    width: 100%;
    max-width: 1000px;
    box-sizing: border-box;
    margin-bottom: 25px;
  }

  input[type="file"], input[type="text"], textarea {
    width: 100%;
    padding: 14px;
    margin: 12px 0;
    border-radius: 6px;
    border: 1px solid #ccc;
    font-size: 18px;
    box-sizing: border-box;
  }

  textarea {
    height: 60px;
    overflow-x: auto;
    font-family: monospace;
  }

  button {
    width: 100%;
    padding: 14px;
    border: none;
    border-radius: 6px;
    background: #4CAF50;
    color: white;
    font-size: 18px;
    cursor: pointer;
    margin-top: 12px;
  }

  button:hover { background: #45a049; }

  .files-wrapper {
    overflow-x: auto;
    margin-top: 20px;
  }

  .files-table {
    width: 100%;
    border-collapse: collapse;
    min-width: 600px;
    font-size: 18px;
  }

  .files-table th, .files-table td {
    padding: 10px;
    text-align: center;
    border-bottom: 1px solid #ddd;
  }

  .files-table th { background-color: #007bff; color: white; }

  a {
    color: #007bff;
    text-decoration: none;
    display: block;
    text-align: center;
    margin-top: 15px;
    font-size: 18px;
  }

  a:hover { text-decoration: underline; }

  p.result {
    text-align: center;
    font-weight: bold;
    color: #333;
    font-size: 20px;
    word-break: break-word;
  }
</style>

  </head>
  <body>
    <h2>Upload File & Generate SHA-256</h2>
    <div class="card">
      <form method="POST" action="/upload" enctype="multipart/form-data">
        <input type="file" name="upload">
        <button type="submit">Generate Hash</button>
      </form>
      <textarea readonly>)rawliteral";
  html += hash;
  html += R"rawliteral(</textarea>
      <form method="POST" action="/store">
        <button type="submit">Store File on SD</button>
      </form>
      <p class="result>)rawliteral";
  html += result;
  html += R"rawliteral(</p>
    </div>
    <div class="card">
      <h3>Files on SD Card</h3>
      <div class="files-wrapper">
        <table class="files-table">
          <tr><th>File Name</th><th>Size (bytes)</th></tr>
  )rawliteral";

  // List files from SD
  File root = SD.open("/");
  File file = root.openNextFile();
  while (file) {
    html += "<tr><td>" + String(file.name()) + "</td><td>" + String(file.size()) + "</td></tr>";
    file = root.openNextFile();
  }

  html += R"rawliteral(</table>
      </div>
    </div>
    <a href='/verifypage'>Go to Verification Page</a>
  </body>
  </html>
  )rawliteral";

  return html;
}

// ---------------- Verification Page HTML ----------------
String verifyPageHTML(String result = "") {
  String html = R"rawliteral(
  <!DOCTYPE html>
  <html>
  <head>
    <title>Verify File</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
      body {
        font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
        background: #f0f2f5;
        display: flex;
        flex-direction: column;
        align-items: center;
        margin: 0;
        padding: 20px;
      }

      h2 {
        margin-bottom: 25px;
        color: #333;
        text-align: center;
        font-size: 28px;
      }

      .card {
        background: #fff;
        border-radius: 12px;
        box-shadow: 0 4px 12px rgba(0,0,0,0.1);
        padding: 30px;
        width: 100%;
        max-width: 1000px;
        box-sizing: border-box;
        margin-bottom: 25px;
      }

      input[type="file"], input[type="text"] {
        width: 100%;
        padding: 14px;
        margin: 12px 0;
        border-radius: 6px;
        border: 1px solid #ccc;
        font-size: 18px;
        box-sizing: border-box;
      }

      button {
        width: 100%;
        padding: 14px;
        border: none;
        border-radius: 6px;
        background: #2196F3;
        color: white;
        font-size: 18px;
        cursor: pointer;
        margin-top: 12px;
      }

      button:hover { background: #0b7dda; }

      p.result {
        text-align: center;
        font-weight: bold;
        color: #333;
        font-size: 20px;
        word-break: break-word;
        margin-top: 15px;
      }

      a {
        color: #007bff;
        text-decoration: none;
        display: block;
        text-align: center;
        margin-top: 15px;
        font-size: 18px;
      }

      a:hover { text-decoration: underline; }

      .hash-display {
        text-align: center;
        font-family: monospace;
        margin-top: 10px;
        font-size: 16px;
        word-break: break-word;
        color: #555;
      }
    </style>
  </head>
  <body>
    <h2>Verify File Integrity</h2>
    <div class="card">
      <form method="POST" action="/verify" enctype="multipart/form-data">
        <input type="file" name="upload">
        <input type="text" name="expected" placeholder="Enter Expected SHA-256 Hash">
        <button type="submit">Verify</button>
      </form>
      <p class="result">)rawliteral";
  html += result;
  html += R"rawliteral(</p>
      <div class="hash-display">Calculated Hash: )rawliteral";
  html += calculatedHash;
  html += R"rawliteral(</div>
    </div>
    <a href='/'>Go to Upload & Store Page</a>
  </body>
  </html>
  )rawliteral";

  return html;
}

// ---------------- Setup ----------------
void setup() {
  Serial.begin(115200);
  pinMode(ledPin, OUTPUT);
  digitalWrite(ledPin, LOW);

  // WiFi
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConnected! IP: " + WiFi.localIP().toString());

  // SD Card
  if (!SD.begin(chipSelect)) {
    Serial.println("SD Card Mount Failed!");
    while (1);
  }
  Serial.println("SD Card initialized.");

  // ---------------- Routes ----------------
  server.on("/", HTTP_GET, []() { server.send(200, "text/html", uploadPageHTML()); });
  server.on("/verifypage", HTTP_GET, []() { server.send(200, "text/html", verifyPageHTML()); });

  // ---------------- Upload Handler ----------------
  server.on("/upload", HTTP_POST,
    []() {
      calculatedHash = toHexString(hashResult, 32);
      server.send(200, "text/html", uploadPageHTML(calculatedHash));
    },
    []() {
      HTTPUpload& upload = server.upload();
      if (upload.status == UPLOAD_FILE_START) {
        Serial.println("Upload start: " + upload.filename);
        originalFileName = upload.filename;
        int dotIndex = upload.filename.lastIndexOf('.');
        if (dotIndex > 0) uploadedExt = upload.filename.substring(dotIndex);
        else uploadedExt = ".bin";
        uploadFile = SD.open(tempFileName, FILE_WRITE);
        mbedtls_sha256_init(&ctx);
        mbedtls_sha256_starts(&ctx, 0);
      } else if (upload.status == UPLOAD_FILE_WRITE) {
        if (uploadFile) uploadFile.write(upload.buf, upload.currentSize);
        mbedtls_sha256_update(&ctx, upload.buf, upload.currentSize);
      } else if (upload.status == UPLOAD_FILE_END) {
        if (uploadFile) uploadFile.close();
        mbedtls_sha256_finish(&ctx, hashResult);
        mbedtls_sha256_free(&ctx);
        Serial.println("Upload finished. Size: " + String(upload.totalSize));
      }
    }
  );

  // ---------------- Store File Handler ----------------
  server.on("/store", HTTP_POST, []() {
    String finalName = "/" + originalFileName + "_(" + toHexString(hashResult,32) + ")" + uploadedExt;
    if (SD.exists(finalName)) SD.remove(finalName);
    SD.rename(tempFileName, finalName);
    String result = "File stored on SD as: " + finalName;
    server.send(200, "text/html", uploadPageHTML(calculatedHash, result));
  });

  // ---------------- Verification Handler ----------------
  server.on("/verify", HTTP_POST,
    []() {
      String expected = server.arg("expected");
      Serial.println("Expected: " + expected);
      Serial.println("Calculated: " + calculatedHash);

      String result;
      if (expected.equalsIgnoreCase(calculatedHash)) {
        result = "<span style='color:green'> Hash Match: File is authentic</span>";
        digitalWrite(ledPin, LOW);
      } else {
        result = "<span style='color:red'> Hash Mismatch: File is tampered!</span>";
        digitalWrite(ledPin, HIGH);
      }
      server.send(200, "text/html", verifyPageHTML(result));
    },
    []() {
      HTTPUpload& upload = server.upload();
      if (upload.status == UPLOAD_FILE_START) {
        Serial.println("Verification upload start: " + upload.filename);
        uploadFile = SD.open(tempFileName, FILE_WRITE);
        mbedtls_sha256_init(&ctx);
        mbedtls_sha256_starts(&ctx, 0);
        int dotIndex = upload.filename.lastIndexOf('.');
        if (dotIndex > 0) uploadedExt = upload.filename.substring(dotIndex);
        else uploadedExt = ".bin";
      } else if (upload.status == UPLOAD_FILE_WRITE) {
        if (uploadFile) uploadFile.write(upload.buf, upload.currentSize);
        mbedtls_sha256_update(&ctx, upload.buf, upload.currentSize);
      } else if (upload.status == UPLOAD_FILE_END) {
        if (uploadFile) uploadFile.close();
        mbedtls_sha256_finish(&ctx, hashResult);
        mbedtls_sha256_free(&ctx);
        calculatedHash = toHexString(hashResult, 32);
        Serial.println("Verification hash: " + calculatedHash);
      }
    }
  );

  server.begin();
  Serial.println("Server started");
}

// ---------------- Loop ----------------
void loop() {
  server.handleClient();
}
