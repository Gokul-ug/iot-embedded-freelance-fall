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

// Store uploaded file content
String uploadedContent = "";
String uploadedFilename = "";

// AES key for demo (32 bytes = AES-256)
uint8_t AES_KEY[32] = {
  0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,
  0x09,0x0A,0x0B,0x0C,0x0D,0x0E,0x0F,0x10,
  0x11,0x12,0x13,0x14,0x15,0x16,0x17,0x18,
  0x19,0x1A,0x1B,0x1C,0x1D,0x1E,0x1F,0x20
};

// Fixed IV for demo
uint8_t AES_IV[16] = {
  0xA1,0xA2,0xA3,0xA4,0xA5,0xA6,0xA7,0xA8,
  0xA9,0xAA,0xAB,0xAC,0xAD,0xAE,0xAF,0xB0
};

// ------------------- Utility Functions -------------------
String bytesToHex(const uint8_t* data, size_t len){
  String s = "";
  for(size_t i=0;i<len;i++){
    char buf[3]; sprintf(buf,"%02X",data[i]);
    s += buf;
  }
  return s;
}

bool aesEncryptFile(const char* inPath, const char* outPath){
  File inFile = SD.open(inPath, FILE_READ);
  if(!inFile) return false;
  File outFile = SD.open(outPath, FILE_WRITE);
  if(!outFile){ inFile.close(); return false; }

  outFile.write(AES_IV,16);

  mbedtls_aes_context aes;
  mbedtls_aes_init(&aes);
  mbedtls_aes_setkey_enc(&aes, AES_KEY, 256);

  uint8_t buf[512];
  uint8_t outBuf[528];
  uint8_t iv[16]; memcpy(iv,AES_IV,16);

  while(inFile.available()){
    size_t readBytes = inFile.read(buf,512);
    bool lastChunk = inFile.available() == 0;
    size_t paddedLen = readBytes;

    if(lastChunk){
      uint8_t pad = 16 - (readBytes%16);
      if(pad==0) pad=16;
      paddedLen = readBytes + pad;
      uint8_t* tmp = (uint8_t*)malloc(paddedLen);
      memcpy(tmp,buf,readBytes); memset(tmp+readBytes,pad,pad);
      mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_ENCRYPT, paddedLen, iv, tmp, outBuf);
      outFile.write(outBuf,paddedLen);
      free(tmp);
    }else{
      if(readBytes%16!=0){
        uint8_t* tmp = (uint8_t*)malloc(readBytes+(16-readBytes%16));
        memcpy(tmp,buf,readBytes); memset(tmp+readBytes,0,16-readBytes%16);
        mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_ENCRYPT, readBytes+(16-readBytes%16), iv, tmp, outBuf);
        outFile.write(outBuf, readBytes+(16-readBytes%16));
        free(tmp);
      }else{
        mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_ENCRYPT, readBytes, iv, buf, outBuf);
        outFile.write(outBuf, readBytes);
      }
    }
  }

  inFile.close(); outFile.close();
  mbedtls_aes_free(&aes);
  return true;
}

bool aesDecryptFile(const char* inPath, const char* outPath, const uint8_t key[32]){
  File inFile = SD.open(inPath, FILE_READ);
  if(!inFile) return false;
  File outFile = SD.open(outPath, FILE_WRITE);
  if(!outFile){ inFile.close(); return false; }

  uint8_t iv[16]; inFile.read(iv,16);

  mbedtls_aes_context aes;
  mbedtls_aes_init(&aes);
  mbedtls_aes_setkey_dec(&aes, key, 256);

  uint8_t buf[512+16], outBuf[512+16];
  size_t bytesRead;
  while((bytesRead=inFile.read(buf,512))>0){
    mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_DECRYPT, bytesRead, iv, buf, outBuf);
    if(inFile.available()==0){
      uint8_t pad = outBuf[bytesRead-1];
      outFile.write(outBuf, bytesRead-pad);
    }else outFile.write(outBuf, bytesRead);
  }

  inFile.close(); outFile.close();
  mbedtls_aes_free(&aes);
  return true;
}

// ------------------- QR Code Generation -------------------
String generateQRCodeHTML(const String &data) {
    QRCode qrcode;
    uint8_t qrcodeData[qrcode_getBufferSize(3)];
    qrcode_initText(&qrcode, qrcodeData, 3, 0, data.c_str());

    String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><title>QR Code</title>";
    html += "<style>body{text-align:center;font-family:Arial;background:#f7f7f7;padding:50px;}h2{color:#333;}";
    html += "pre{background:#fff;padding:20px;display:inline-block;border-radius:15px;}</style></head><body>";
    html += "<h2>Scan AES Key QR</h2><pre>";

    for (uint8_t y = 0; y < qrcode.size; y++) {
        for (uint8_t x = 0; x < qrcode.size; x++) {
            html += qrcode_getModule(&qrcode, x, y) ? "██" : "  ";
        }
        html += "\n";
    }

    html += "</pre><br><a href='/'>Upload another file</a></body></html>";
    return html;
}

// ------------------- Modern Upload Page -------------------
String uploadPage = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>ESP32 File Upload</title>
<style>
body { font-family: 'Segoe UI',sans-serif; background: linear-gradient(135deg,#74ABE2,#5563DE); margin:0; padding:0; display:flex; justify-content:center; align-items:center; min-height:100vh; }
.container { background:#fff; border-radius:15px; box-shadow:0 10px 25px rgba(0,0,0,0.2); padding:40px; width:90%; max-width:450px; text-align:center; }
h1 { margin-bottom:30px; color:#333; }
input[type="file"] { display:block; margin:20px auto; }
input[type="submit"] { background-color:#5563DE; color:#fff; border:none; padding:12px 30px; border-radius:8px; font-size:16px; cursor:pointer; transition:0.3s; }
input[type="submit"]:hover { background-color:#3b46b2; }
a { display:inline-block; margin-top:20px; color:#5563DE; text-decoration:none; font-weight:bold; }
a:hover { color:#3b46b2; }
</style>
</head>
<body>
<div class="container">
  <h1>ESP32 File Upload</h1>
  <form method="POST" action="/upload" enctype="multipart/form-data">
    <input type="file" name="file" required><br>
    <input type="submit" value="Upload & Encrypt">
  </form>
  <a href='/decrypt'>Go to Decrypt Page</a>
</div>
</body>
</html>
)rawliteral";

// ------------------- Modern Decrypt Page -------------------
String decryptPageHeader = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Decrypt File</title>
<style>
body { font-family: 'Segoe UI',sans-serif; background: linear-gradient(135deg,#FFAF7B,#D76D77); margin:0; padding:0; display:flex; justify-content:center; align-items:center; min-height:100vh; }
.container { background:#fff; border-radius:15px; box-shadow:0 10px 25px rgba(0,0,0,0.2); padding:40px; width:90%; max-width:500px; text-align:center; }
h1 { margin-bottom:25px; color:#333; }
select,input[type="text"] { width:80%; padding:12px; margin:10px 0; border-radius:8px; border:1px solid #ccc; font-size:15px; }
input[type="submit"] { background-color:#D76D77; color:#fff; border:none; padding:12px 30px; border-radius:8px; font-size:16px; cursor:pointer; transition:0.3s; }
input[type="submit"]:hover { background-color:#b0494f; }
a { display:inline-block; margin-top:20px; color:#D76D77; text-decoration:none; font-weight:bold; }
a:hover { color:#b0494f; }
</style>
</head>
<body>
<div class="container">
<h1>Decrypt File</h1>
<form method="POST" action="/decrypt">
Select File: <select name="file">
)rawliteral";

// ------------------- Handlers -------------------
void handleRoot(){ server.send(200,"text/html",uploadPage); }

void handleUpload(){
    HTTPUpload& upload = server.upload();
    switch(upload.status){
        case UPLOAD_FILE_START:
            uploadedFilename = upload.filename;
            uploadedContent = "";
            SD.remove("/"+uploadedFilename);
            break;
        case UPLOAD_FILE_WRITE:{
            File f = SD.open("/"+uploadedFilename, FILE_APPEND);
            if(f){ f.write(upload.buf, upload.currentSize); f.close(); }
            break;
        }
        case UPLOAD_FILE_END:{
            String encFile = uploadedFilename + ".enc";
            if(aesEncryptFile(("/"+uploadedFilename).c_str(), ("/"+encFile).c_str())){
                SD.remove("/"+uploadedFilename);
                String keyHex = bytesToHex(AES_KEY,32);
                server.send(200,"text/html",generateQRCodeHTML(keyHex));
            }else server.send(500,"text/plain","Encryption failed");
            break;
        }
        case UPLOAD_FILE_ABORTED: server.send(400,"text/plain","Upload aborted"); break;
    }
}

void handleDecrypt(){
    if(!server.hasArg("file")||!server.hasArg("key")){
        server.send(400,"text/plain","Missing file or key"); return;
    }
    String fname = server.arg("file");
    String keyHex = server.arg("key");
    uint8_t key[32];
    for(int i=0;i<32;i++) key[i]=(uint8_t) strtoul(keyHex.substring(i*2,i*2+2).c_str(),NULL,16);
    String outFile = fname + ".dec";

    if(aesDecryptFile(("/"+fname).c_str(),("/"+outFile).c_str(),key)){
        // Modern UI for decrypted file page
        String html = R"rawliteral(
        <!DOCTYPE html>
        <html lang="en">
        <head>
        <meta charset="UTF-8">
        <meta name="viewport" content="width=device-width, initial-scale=1.0">
        <title>File Decrypted</title>
        <style>
        body { font-family: 'Segoe UI',sans-serif; background: linear-gradient(135deg,#FFAF7B,#D76D77); margin:0; padding:0; display:flex; justify-content:center; align-items:center; min-height:100vh; }
        .container { background:#fff; border-radius:15px; box-shadow:0 10px 25px rgba(0,0,0,0.2); padding:40px; width:90%; max-width:500px; text-align:center; }
        h1 { margin-bottom:25px; color:#333; }
        a { display:inline-block; margin-top:20px; color:#D76D77; text-decoration:none; font-weight:bold; }
        a:hover { color:#b0494f; }
        </style>
        </head>
        <body>
        <div class="container">
        <h1>File Decrypted Successfully!</h1>
        <p>Decrypted file: <a href='/download?file=)rawliteral" + outFile + R"rawliteral('>)rawliteral" + outFile + R"rawliteral(</a></p>
        <a href='/decrypt'>Decrypt Another File</a><br>
        <a href='/'>Go to Upload Page</a>
        </div>
        </body>
        </html>
        )rawliteral";

        server.send(200,"text/html",html);
    } else {
        server.send(400,"text/plain","Decryption failed (wrong key?)");
    }
}

void handleDecryptForm(){
    String page = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Decrypt File</title>
<style>
body { font-family: 'Segoe UI',sans-serif; background: linear-gradient(135deg,#FFAF7B,#D76D77); margin:0; padding:0; display:flex; justify-content:center; align-items:center; min-height:100vh; }
.container { background:#fff; border-radius:15px; box-shadow:0 10px 25px rgba(0,0,0,0.2); padding:40px; width:90%; max-width:500px; text-align:center; }
h1 { margin-bottom:25px; color:#333; }
select,input[type="text"] { width:80%; padding:12px; margin:10px 0; border-radius:8px; border:1px solid #ccc; font-size:15px; }
input[type="submit"] { background-color:#D76D77; color:#fff; border:none; padding:12px 30px; border-radius:8px; font-size:16px; cursor:pointer; transition:0.3s; }
input[type="submit"]:hover { background-color:#b0494f; }
a { display:inline-block; margin-top:20px; color:#D76D77; text-decoration:none; font-weight:bold; }
a:hover { color:#b0494f; }
</style>
</head>
<body>
<div class="container">
<h1>Decrypt File</h1>
<form method="POST" action="/decrypt">
Select File: <select name="file">
)rawliteral";

    // Dynamically add .enc files from SD
    File root = SD.open("/");
    File f = root.openNextFile();
    while(f){
        String name = f.name();
        if(name.endsWith(".enc")) page += "<option>" + name + "</option>";
        f = root.openNextFile();
    }

    page += R"rawliteral(
</select><br><br>
AES Key (hex): <input type="text" name="key" placeholder="Enter 64-character AES key"><br><br>
<input type="submit" value="Decrypt">
</form>
<a href='/'>Go to Upload Page</a>
</div>
</body>
</html>
)rawliteral";

    server.send(200,"text/html",page);
}

void handleDownload(){
    if(!server.hasArg("file")){ server.send(400,"text/plain","Missing file"); return; }
    String fname=server.arg("file");
    if(!SD.exists("/"+fname)){ server.send(404,"text/plain","File not found"); return; }
    File f = SD.open("/"+fname, FILE_READ);
    server.sendHeader("Content-Disposition","attachment; filename=\""+fname+"\"");
    server.streamFile(f,"application/octet-stream");
    f.close();
}

// ------------------- Setup & Loop -------------------
void setup(){
    Serial.begin(115200);
    if(!SPIFFS.begin(true)){ Serial.println("SPIFFS Mount Failed"); return; }
    if(!SD.begin(SD_CS_PIN)){ Serial.println("SD init failed"); return; }

    WiFi.begin(ssid,password);
    Serial.print("Connecting to WiFi");
    while(WiFi.status()!=WL_CONNECTED){ delay(500); Serial.print("."); }
    Serial.println("\nConnected! IP: "+WiFi.localIP().toString());

    server.on("/", HTTP_GET, handleRoot);
    server.on("/upload", HTTP_POST, [](){}, handleUpload);
    server.on("/decrypt", HTTP_GET, handleDecryptForm);
    server.on("/decrypt", HTTP_POST, handleDecrypt);
    server.on("/download", HTTP_GET, handleDownload);

    server.begin();
    Serial.println("HTTP server started");
}

void loop(){ server.handleClient(); }
