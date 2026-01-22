// ESP32 Secure File Encryptor (with One-Time QR Code AES Key)
// Features:
//  - Random AES-256 key & IV per upload
//  - QR code shows AES key
//  - Key auto-expires after 60s or first use
//  - AES-CBC encryption/decryption (PKCS#7)
//  - Web interface for Upload / Decrypt / Download
//  - Works with SD card (≤ 2MB files)
//
// Requirements: ESP32 core, SD, SPIFFS, mbedtls (bundled), QRCodeGenerator
// Wiring: SD CS -> GPIO 5 (default). Change SD_CS_PIN if needed.

#include <WiFi.h>
#include <WebServer.h>
#include <SD.h>
#include <SPI.h>
#include "SPIFFS.h"
#include "mbedtls/aes.h"
#include "QRCodeGenerator.h"

const char* ssid = "Gtech";
const char* password = "12345678";

WebServer server(80);
const int SD_CS_PIN = 5;

// limits & buffers
const size_t CHUNK_SZ = 512;
const size_t AES_BLOCK = 16;
const size_t MAX_FILE_SIZE_BYTES = 2UL * 1024UL * 1024UL; // 2 MB

// --- One-Time Key System ---
struct OneTimeKey {
  String keyHex;
  unsigned long createdAt;
  bool used;
} currentKey = {"", 0, true}; // initialize as used (none active)

// Expiry duration (in ms)
const unsigned long KEY_EXPIRY_MS = 60000; // 60 seconds

// ---------------- HTML pages ----------------
String uploadPage = R"rawliteral(
<!DOCTYPE html><html lang="en"><head><meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>ESP32 File Upload</title>
<style>
body{font-family:'Segoe UI',sans-serif;background:linear-gradient(135deg,#74ABE2,#5563DE);margin:0;padding:0;display:flex;justify-content:center;align-items:center;min-height:100vh}
.container{background:#fff;border-radius:15px;box-shadow:0 10px 25px rgba(0,0,0,0.2);padding:40px;width:90%;max-width:450px;text-align:center}
h1{margin-bottom:30px;color:#333}
input[type=file]{display:block;margin:20px auto}
input[type=submit]{background-color:#5563DE;color:#fff;border:none;padding:12px 30px;border-radius:8px;font-size:16px;cursor:pointer;transition:.3s}
input[type=submit]:hover{background-color:#3b46b2}
a{display:inline-block;margin-top:20px;color:#5563DE;text-decoration:none;font-weight:bold}
</style></head><body>
<div class="container">
<h1>ESP32 File Upload</h1>
<form method="POST" action="/upload" enctype="multipart/form-data">
  <input type="file" name="file" required><br>
  <input type="submit" value="Upload & Encrypt">
</form>
<a href='/decrypt'>Go to Decrypt Page</a>
</div></body></html>
)rawliteral";

String decryptPageHeader = R"rawliteral(
<!DOCTYPE html><html lang="en"><head><meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Decrypt File</title>
<style>
body{font-family:'Segoe UI',sans-serif;background:linear-gradient(135deg,#FFAF7B,#D76D77);margin:0;padding:0;display:flex;justify-content:center;align-items:center;min-height:100vh}
.container{background:#fff;border-radius:15px;box-shadow:0 10px 25px rgba(0,0,0,0.2);padding:40px;width:90%;max-width:500px;text-align:center}
h1{margin-bottom:25px;color:#333}
select,input[type=text]{width:80%;padding:12px;margin:10px 0;border-radius:8px;border:1px solid #ccc;font-size:15px}
input[type=submit]{background-color:#D76D77;color:#fff;border:none;padding:12px 30px;border-radius:8px;font-size:16px;cursor:pointer;transition:.3s}
input[type=submit]:hover{background-color:#b0494f}
a{display:inline-block;margin-top:20px;color:#D76D77;text-decoration:none;font-weight:bold}
</style></head><body>
<div class="container">
<h1>Decrypt File</h1>
<form method="POST" action="/decrypt">
Select File: <select name="file">
)rawliteral";

String decryptPageFooter = R"rawliteral(
</select><br><br>
AES Key (hex): <input type="text" name="key" placeholder="Enter 64-character AES key"><br><br>
<input type="submit" value="Decrypt">
</form>
<a href='/'>Go to Upload Page</a>
</div></body></html>
)rawliteral";

// ---------------- Utility ----------------
String bytesToHex(const uint8_t* data, size_t len){
  String s; s.reserve(len*2+1); char buf[3];
  for(size_t i=0;i<len;i++){ sprintf(buf,"%02X",data[i]); s+=buf; }
  return s;
}

void generateRandomBytes(uint8_t *buf, size_t len){
  for(size_t i=0;i<len;i+=4){
    uint32_t r = esp_random();
    size_t copy = min((size_t)4, len - i);
    memcpy(buf + i, &r, copy);
  }
}

// ---------------- AES ----------------
bool aesEncryptFileSD(const char* inPath, const char* outPath, const uint8_t key[32], const uint8_t iv[16]){
  File inFile = SD.open(inPath, FILE_READ);
  if(!inFile) return false;
  File outFile = SD.open(outPath, FILE_WRITE);
  if(!outFile){ inFile.close(); return false; }

  outFile.write(iv, 16);
  mbedtls_aes_context aes;
  mbedtls_aes_init(&aes);
  if(mbedtls_aes_setkey_enc(&aes, key, 256)!=0){ inFile.close(); outFile.close(); mbedtls_aes_free(&aes); return false; }

  uint8_t iv_local[16]; memcpy(iv_local, iv, 16);
  uint8_t inBuf[CHUNK_SZ + AES_BLOCK], outBuf[CHUNK_SZ + AES_BLOCK];
  while(true){
    size_t readBytes = inFile.read(inBuf, CHUNK_SZ);
    if(readBytes==0) break;
    bool isLast = (inFile.available()==0);
    if(!isLast){
      size_t toProcess = readBytes;
      if(toProcess % AES_BLOCK != 0){
        size_t pad = AES_BLOCK - (toProcess % AES_BLOCK);
        memset(inBuf + toProcess, 0, pad);
        toProcess += pad;
      }
      mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_ENCRYPT, toProcess, iv_local, inBuf, outBuf);
      outFile.write(outBuf, toProcess);
    } else {
      uint8_t pad = AES_BLOCK - (readBytes % AES_BLOCK);
      if(pad == 0) pad = AES_BLOCK;
      memset(inBuf + readBytes, pad, pad);
      mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_ENCRYPT, readBytes + pad, iv_local, inBuf, outBuf);
      outFile.write(outBuf, readBytes + pad);
      break;
    }
  }
  inFile.close(); outFile.close(); mbedtls_aes_free(&aes);
  return true;
}

bool aesDecryptFileSD(const char* inPath, const char* outPath, const uint8_t key[32]){
  File inFile = SD.open(inPath, FILE_READ);
  if(!inFile) return false;
  File outFile = SD.open(outPath, FILE_WRITE);
  if(!outFile){ inFile.close(); return false; }

  uint8_t iv_local[16];
  if(inFile.read(iv_local, 16)!=16){ inFile.close(); outFile.close(); return false; }

  mbedtls_aes_context aes;
  mbedtls_aes_init(&aes);
  if(mbedtls_aes_setkey_dec(&aes, key, 256)!=0){ inFile.close(); outFile.close(); mbedtls_aes_free(&aes); return false; }

  uint8_t inBuf[CHUNK_SZ + AES_BLOCK], outBuf[CHUNK_SZ + AES_BLOCK];
  size_t bytesRead;
  while((bytesRead=inFile.read(inBuf,CHUNK_SZ))>0){
    if(bytesRead % AES_BLOCK != 0){ inFile.close(); outFile.close(); mbedtls_aes_free(&aes); return false; }
    bool lastChunk = (inFile.available()==0);
    mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_DECRYPT, bytesRead, iv_local, inBuf, outBuf);
    if(lastChunk){
      uint8_t pad = outBuf[bytesRead-1];
      if(pad==0||pad> AES_BLOCK){ inFile.close(); outFile.close(); mbedtls_aes_free(&aes); return false; }
      for(size_t i=0;i<pad;i++) if(outBuf[bytesRead-1-i]!=pad){ inFile.close(); outFile.close(); mbedtls_aes_free(&aes); return false; }
      outFile.write(outBuf, bytesRead - pad);
    } else outFile.write(outBuf, bytesRead);
  }
  inFile.close(); outFile.close(); mbedtls_aes_free(&aes);
  return true;
}

// ---------------- QR ----------------
String generateQRCodeHTML(const String &data){
  QRCode qrcode;
  uint8_t qrcodeData[qrcode_getBufferSize(3)];
  qrcode_initText(&qrcode, qrcodeData, 3, 0, data.c_str());
  String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><title>QR Code</title>"
  "<style>body{text-align:center;font-family:Arial;background:#f7f7f7;padding:50px;}h2{color:#333;}pre{background:#fff;padding:20px;display:inline-block;border-radius:15px;}</style></head><body>";
  html += "<h2>Scan AES Key QR (Valid 60s / One Use)</h2><pre>";
  for(uint8_t y=0;y<qrcode.size;y++){
    for(uint8_t x=0;x<qrcode.size;x++) html += qrcode_getModule(&qrcode,x,y) ? "██" : "  ";
    html += "\n";
  }
  html += "</pre><p>Key (hex):<br><code>"+data+"</code></p><p><b>This QR expires after 60 seconds or one decryption.</b></p><a href='/'>Upload another file</a></body></html>";
  return html;
}

// ---------------- Handlers ----------------
void handleRoot(){ server.send(200,"text/html",uploadPage); }

void handleUpload(){
  HTTPUpload& upload = server.upload();
  static String filename; static size_t writtenSoFar;
  switch(upload.status){
    case UPLOAD_FILE_START:
      filename = upload.filename; writtenSoFar=0;
      if(filename.length()==0){ server.send(400,"text/plain","No filename"); return; }
      if(SD.exists("/"+filename)) SD.remove("/"+filename);
      break;
    case UPLOAD_FILE_WRITE:{
      writtenSoFar += upload.currentSize;
      if(writtenSoFar > MAX_FILE_SIZE_BYTES){ SD.remove("/"+filename); server.send(400,"text/plain","File too big"); return; }
      File f = SD.open("/"+filename, FILE_APPEND);
      if(f){ f.write(upload.buf, upload.currentSize); f.close(); }
      break;
    }
    case UPLOAD_FILE_END:{
      String encName = filename + ".enc";
      uint8_t aesKey[32], aesIV[16];
      generateRandomBytes(aesKey,32);
      generateRandomBytes(aesIV,16);
      bool ok = aesEncryptFileSD(("/"+filename).c_str(), ("/"+encName).c_str(), aesKey, aesIV);
      SD.remove("/"+filename);
      if(ok){
        currentKey.keyHex = bytesToHex(aesKey,32);
        currentKey.createdAt = millis();
        currentKey.used = false;
        server.send(200,"text/html", generateQRCodeHTML(currentKey.keyHex));
      } else server.send(500,"text/plain","Encryption failed");
      break;
    }
    case UPLOAD_FILE_ABORTED: server.send(400,"text/plain","Upload aborted"); break;
  }
}

void handleDecryptForm(){
  String page = decryptPageHeader;
  File root = SD.open("/");
  if(!root) page += "<option>No SD</option>";
  else{
    File f = root.openNextFile();
    while(f){
      String n = f.name(); if(n.endsWith(".enc")){
        if(n.startsWith("/")) n = n.substring(1);
        page += "<option>"+n+"</option>";
      }
      f.close(); f = root.openNextFile();
    }
    root.close();
  }
  page += decryptPageFooter;
  server.send(200,"text/html",page);
}

void handleDecrypt(){
  if(!server.hasArg("file")||!server.hasArg("key")){ server.send(400,"text/plain","Missing file or key"); return; }
  String fname=server.arg("file"), keyHex=server.arg("key");
  if(keyHex.length()!=64){ server.send(400,"text/plain","Key must be 64 hex chars"); return; }

  // check validity
  unsigned long now = millis();
  bool expired = (now - currentKey.createdAt > KEY_EXPIRY_MS);
  if(currentKey.used || expired || keyHex != currentKey.keyHex){
    server.send(403,"text/plain","QR Expired or Invalid");
    return;
  }

  uint8_t key[32];
  for(int i=0;i<32;i++) key[i]=(uint8_t) strtoul(keyHex.substring(i*2,i*2+2).c_str(),NULL,16);
  String outName=fname+".dec";
  if(aesDecryptFileSD(("/"+fname).c_str(),("/"+outName).c_str(),key)){
    currentKey.used = true; // invalidate
    String html = "<!DOCTYPE html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>File Decrypted</title><style>body{font-family:'Segoe UI',sans-serif;background:linear-gradient(135deg,#FFAF7B,#D76D77);display:flex;align-items:center;justify-content:center;min-height:100vh}.container{background:#fff;padding:30px;border-radius:12px;text-align:center}</style></head><body><div class='container'><h1>File Decrypted Successfully!</h1>";
    html += "<p>Decrypted file: <a href='/download?file="+outName+"'>"+outName+"</a></p><a href='/decrypt'>Decrypt Another File</a><br><a href='/'>Go to Upload Page</a></div></body></html>";
    server.send(200,"text/html",html);
  } else server.send(400,"text/plain","Decryption failed (wrong key or corrupted file)");
}

void handleDownload(){
  if(!server.hasArg("file")){ server.send(400,"text/plain","Missing file"); return; }
  String fname=server.arg("file"), path="/"+fname;
  if(!SD.exists(path)){ server.send(404,"text/plain","File not found"); return; }
  File f=SD.open(path,FILE_READ);
  server.sendHeader("Content-Disposition","attachment; filename=\""+fname+"\"");
  server.streamFile(f,"application/octet-stream");
  f.close();
}

// ---------------- Setup / Loop ----------------
void setup(){
  Serial.begin(115200);
  if(!SPIFFS.begin(true)) Serial.println("SPIFFS Mount Failed");
  if(!SD.begin(SD_CS_PIN)){ Serial.println("SD init failed"); while(true){ delay(1000);} }

  WiFi.begin(ssid,password);
  Serial.print("Connecting to WiFi");
  while(WiFi.status()!=WL_CONNECTED){ delay(500); Serial.print("."); }
  Serial.println("\nConnected! IP: "+WiFi.localIP().toString());

  server.on("/",HTTP_GET,handleRoot);
  server.on("/upload",HTTP_POST,[]{},handleUpload);
  server.on("/decrypt",HTTP_GET,handleDecryptForm);
  server.on("/decrypt",HTTP_POST,handleDecrypt);
  server.on("/download",HTTP_GET,handleDownload);

  server.begin();
  Serial.println("HTTP server started");
}

void loop(){ server.handleClient(); }
