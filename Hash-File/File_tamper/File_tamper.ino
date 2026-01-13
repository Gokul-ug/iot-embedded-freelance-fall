/* ESP32 Firmware Tamper Detector — Upgraded
   Features added:
   - NTP real timestamps (configTime)
   - CSV logging to SD with timestamp & event type
   - Backup copies saved to /firmware_backup/
   - Auto-self-heal (restore from latest backup when tampered)
   - Blynk notifications + Blynk.logEvent("alert", ...)
   - Chart.js dashboard (served in main page) that fetches /stats JSON
   NOTE: Replace BLYNK_AUTH_TOKEN, ssid, password
*/

#define BLYNK_TEMPLATE_ID "TMPL3V-Ryox0B"
#define BLYNK_TEMPLATE_NAME "File Tamper Detection"
#define BLYNK_AUTH_TOKEN "z6xxIm08wz7V3LZaGiEyxeNWbm9OXQ33"

#include <WiFi.h>
#include <WebServer.h>
#include <SPI.h>
#include <SD.h>
#include "mbedtls/sha256.h"
#include <time.h>
#include <BlynkSimpleEsp32.h>

char auth[] = "BLYNK_AUTH_TOKEN"; // <-- replace
char ssid[] = "Hash";              // <-- replace
char password[] = "12345678";      // <-- replace

WebServer server(80);

// SD and pins
const int chipSelect = 5; // SD CS
const int ledPin = 2;     // Tamper LED

// Paths and globals
const char* FIRMWARE_DIR   = "/firmware";
const char* BACKUP_DIR     = "/firmware_backup";
const char* LOG_CSV_PATH   = "/firmware_events.csv"; // CSV log
String lastMessage = "";           // UI banner
unsigned long bootMillis = 0;

// Utilities ------------------------------------------------
String toHexString(const uint8_t *hash, size_t len) {
  String hex = "";
  for (size_t i = 0; i < len; i++) {
    char buf[3];
    sprintf(buf, "%02x", hash[i]);
    hex += buf;
  }
  return hex;
}

bool computeFileSHA256(const char* path, uint8_t outHash[32]) {
  File f = SD.open(path, FILE_READ);
  if (!f) return false;

  mbedtls_sha256_context ctx;
  mbedtls_sha256_init(&ctx);
  mbedtls_sha256_starts(&ctx, 0);

  static const size_t CHUNK = 2048;
  uint8_t buf[CHUNK];
  while (true) {
    int n = f.read(buf, CHUNK);
    if (n < 0) { f.close(); mbedtls_sha256_free(&ctx); return false; }
    if (n == 0) break;
    mbedtls_sha256_update(&ctx, buf, n);
  }

  mbedtls_sha256_finish(&ctx, outHash);
  mbedtls_sha256_free(&ctx);
  f.close();
  return true;
}

String sanitizeFilename(const String& fname) {
  String safe = "";
  for (size_t i = 0; i < fname.length(); i++) {
    char c = fname[i];
    if ((c >= 'a' && c <= 'z') ||
        (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') ||
        c == '.' || c == '_' || c == '-') {
      safe += c;
    }
  }
  if (safe.length() == 0) safe = "firmware.bin";
  return safe;
}

// Timestamp helpers (NTP) -----------------------------------
void initTime() {
  // Use NTP pool, set timezone by TZ environment (example Asia/Kolkata)
  // You can change TZ to your region as needed.
  // For IST (Asia/Kolkata): "IST-5:30"
  configTime(5*3600 + 1800, 0, "pool.ntp.org", "time.nist.gov"); // offset will be applied manually
  // Wait for time
  Serial.print("Waiting for NTP time");
  int tries = 0;
  while (time(nullptr) < 24 * 3600 && tries < 10) {
    Serial.print(".");
    delay(1000);
    tries++;
  }
  Serial.println();
}

String nowStr() {
  time_t nowt = time(nullptr);
  if (nowt < 24*3600) {
    // fallback to millis if time not synced
    return String(millis());
  }
  struct tm timeinfo;
  gmtime_r(&nowt, &timeinfo);
  // Convert gmtime to desired timezone (Asia/Kolkata UTC+5:30)
  // Alternatively adjust configTime with TZ; simple add offset:
  time_t tz_offset = 5*3600 + 1800; // 5h30m
  nowt += tz_offset;
  localtime_r(&nowt, &timeinfo);
  char buf[32];
  strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &timeinfo);
  return String(buf);
}

// CSV logging ------------------------------------------------
bool ensureLogHeader() {
  File f = SD.open(LOG_CSV_PATH, FILE_READ);
  if (!f) {
    File out = SD.open(LOG_CSV_PATH, FILE_WRITE);
    if (!out) return false;
    out.println("timestamp,event,file,size_bytes,sha256,result,details");
    out.close();
    return true;
  } else {
    f.close();
    return true;
  }
}

void logEventCSV(const String &event, const String &file, unsigned long sizeBytes, const String &sha, const String &result, const String &details) {
  if (!SD.exists(LOG_CSV_PATH)) ensureLogHeader();
  File f = SD.open(LOG_CSV_PATH, FILE_APPEND);
  if (!f) return;
  String line = "\"" + nowStr() + "\"," + event + ",\"" + file + "\"," + String(sizeBytes) + ",\"" + sha + "\"," + result + ",\"" + details + "\"";
  f.println(line);
  f.close();
  Serial.println("LOG: " + line);
}

// Backup helpers --------------------------------------------
bool ensureDir(const char* path) {
  if (!SD.exists(path)) return SD.mkdir(path);
  return true;
}

String backupFilenameWithTs(const String &orig) {
  String t = nowStr();
  t.replace(" ", "_"); t.replace(":", "-");
  String base = orig;
  int dot = base.lastIndexOf('.');
  String name = (dot > 0) ? base.substring(0, dot) : base;
  String ext = (dot > 0) ? base.substring(dot) : "";
  return name + "_" + t + ext;
}

bool createBackup(const String &srcPath, const String &destName) {
  if (!ensureDir(BACKUP_DIR)) return false;
  File src = SD.open(srcPath, FILE_READ);
  if (!src) return false;
  String destPath = String(BACKUP_DIR) + "/" + destName;
  File dst = SD.open(destPath, FILE_WRITE);
  if (!dst) { src.close(); return false; }
  // copy
  const size_t CHUNK = 1024;
  uint8_t buf[CHUNK];
  while (true) {
    int r = src.read(buf, CHUNK);
    if (r <= 0) break;
    dst.write(buf, r);
  }
  src.close();
  dst.close();
  return true;
}

// Find latest backup for a given firmware base name (returns path or "")
String findLatestBackup(const String &filename) {
  String base = filename;
  int dot = base.lastIndexOf('.');
  if (dot > 0) base = base.substring(0, dot);
  File dir = SD.open(BACKUP_DIR);
  if (!dir) return "";
  String latestPath = "";
  time_t latestTime = 0;
  File f = dir.openNextFile();
  while (f) {
    if (!f.isDirectory()) {
      String n = String(f.name());
      // check name starts with base
      if (n.startsWith(base)) {
        String p = String(BACKUP_DIR) + "/" + n;
        // Here no easy mtime, so choose last by lexicographic (timestamp in name)
        if (latestPath == "" || n > latestPath) {
          latestPath = p;
        }
      }
    }
    f = dir.openNextFile();
  }
  dir.close();
  return latestPath;
}

// Restore helper: copies backup to firmware path
bool restoreFromBackup(const String &backupPath, const String &targetPath) {
  if (!SD.exists(backupPath)) return false;
  File src = SD.open(backupPath, FILE_READ);
  if (!src) return false;
  // remove old target and create new
  if (SD.exists(targetPath)) SD.remove(targetPath);
  File dst = SD.open(targetPath, FILE_WRITE);
  if (!dst) { src.close(); return false; }
  const size_t CHUNK = 1024;
  uint8_t buf[CHUNK];
  while (true) {
    int r = src.read(buf, CHUNK);
    if (r <= 0) break;
    dst.write(buf, r);
  }
  src.close();
  dst.close();
  return true;
}

// Logging + backup during upload flow -----------------------
bool upsertLog(const String& filename, const String& hash) {
  // Keep your original firmware log as text file for hash lookups
  // Append new entry or replace same-name entry (simple implementation)
  // We'll implement same behavior as before: rewrite a temp file
  const char* TMP_PATH = "/firmware_log.tmp";
  if (!SD.exists(LOG_CSV_PATH)) { // ensure CSV exists done elsewhere
    // nothing, CSV log is separate; but ensure text log file exists
  }
  // Simple append to file-based text log used by getLoggedHash
  File log = SD.open("/firmware_log.txt", FILE_APPEND);
  if (!log) {
    log = SD.open("/firmware_log.txt", FILE_WRITE);
    if (!log) return false;
  }
  log.printf("%s,%s\n", filename.c_str(), hash.c_str());
  log.close();
  return true;
}

// For retrieving last logged hash: read last matching line (better than original: last occurrence)
String getLoggedHash(const String& filename) {
  File log = SD.open("/firmware_log.txt", FILE_READ);
  if (!log) return "";
  String found = "";
  while (log.available()) {
    String line = log.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) continue;
    int comma = line.indexOf(',');
    if (comma <= 0) continue;
    String name = line.substring(0, comma);
    String hash = line.substring(comma + 1);
    if (name == filename) {
      found = hash; // keep updating to get last occurrence
    }
  }
  log.close();
  return found;
}

// UI and file listing ---------------------------------------
String htmlEscape(const String& s) {
  String o;
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '&') o += "&amp;";
    else if (c == '<') o += "&lt;";
    else if (c == '>') o += "&gt;";
    else o += c;
  }
  return o;
}

String buildFileRowsScan(bool &anyTampered) {
  anyTampered = false;
  String rows = "";
  File dir = SD.open(FIRMWARE_DIR);
  if (!dir) return rows;

  File f = dir.openNextFile();
  while (f) {
    if (!f.isDirectory()) {
      String path = String(FIRMWARE_DIR) + "/" + String(f.name());
      String expected = getLoggedHash(String(f.name()));
      uint8_t curHashRaw[32];
      String curHash = "";
      bool ok = computeFileSHA256(path.c_str(), curHashRaw);
      if (ok) curHash = toHexString(curHashRaw, 32);

      String status, statusColor;
      if (expected == "") { status = "Not Logged"; statusColor = "#9E9E9E"; }
      else if (!ok) { status = "Read/Hash Error"; statusColor = "#FF9800"; }
      else if (expected.equalsIgnoreCase(curHash)) { status = "Authentic"; statusColor = "#2E7D32"; }
      else { status = "Tampered"; statusColor = "#C62828"; anyTampered = true; }

      String truncExpected = (expected.length() > 12) ? expected.substring(0,12) + "…" : expected;
      String truncCurHash  = (curHash.length() > 12) ? curHash.substring(0,12) + "…" : curHash;

      rows += "<tr>";
      rows += "<td>" + htmlEscape(path) + "</td>";
      rows += "<td>" + String(f.size()) + "</td>";
      rows += "<td><code>" + (expected == "" ? "-" : truncExpected) + "</code></td>";
      rows += "<td><code>" + (curHash == "" ? "-" : truncCurHash) + "</code></td>";
      rows += "<td><span style='font-weight:600;color:" + statusColor + "'>" + status + "</span></td>";
      rows += "</tr>";

      // If tampered, attempt auto-restore (self-heal)
      if (status == "Tampered") {
        String backupPath = findLatestBackup(String(f.name()));
        if (backupPath != "") {
          bool res = restoreFromBackup(backupPath, path);
          if (res) {
            logEventCSV("restore", String(f.name()), f.size(), curHash, "restored", backupPath);
            // Notify via Blynk and Blynk.logEvent("alert", ...)
            String msg = "Restored " + String(f.name()) + " from backup: " + backupPath;
            Blynk.notify(msg.c_str());
            Blynk.logEvent("alert", msg.c_str());
            Serial.println("Auto-restore successful: " + msg);
          } else {
            String msg = "Restore failed for " + String(f.name());
            Blynk.notify(msg.c_str());
            Blynk.logEvent("alert", msg.c_str());
            Serial.println(msg);
            logEventCSV("restore", String(f.name()), f.size(), curHash, "restore_failed", backupPath);
          }
        } else {
          String msg = "No backup found for " + String(f.name());
          Blynk.notify(msg.c_str());
          Blynk.logEvent("alert", msg.c_str());
          Serial.println(msg);
          logEventCSV("restore", String(f.name()), f.size(), curHash, "no_backup", "");
        }
      }

    }
    f = dir.openNextFile();
  }
  dir.close();
  return rows;
}

// Build main page with Chart.js placeholder -------------------
String mainPageHTML(String message = "", String tableRows = "") {
  if (tableRows == "") {
    bool dummy=false;
    tableRows = buildFileRowsScan(dummy);
  }

  String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<title>ESP32 Firmware Tamper Detector - Dashboard</title>
<meta name="viewport" content="width=device-width, initial-scale=1">
<script src="https://cdn.jsdelivr.net/npm/chart.js"></script>
<style>
:root { --card:#fff; --bg:#f0f2f5; --text:#333; --primary:#2196F3; --accent:#4CAF50; }
*{box-sizing:border-box;}
body{font-family:'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; background:var(--bg); margin:0; padding:18px; display:flex; flex-direction:column; align-items:center;}
h1{margin:8px 0 10px; color:var(--text); font-size:24px; text-align:center;}
.card{background:var(--card); border-radius:12px; box-shadow:0 4px 12px rgba(0,0,0,0.08); padding:18px; width:100%; max-width:1100px; margin-bottom:12px;}
.row{display:flex; gap:12px;}
.col{flex:1;}
canvas{background:#fff; border-radius:8px; padding:10px;}
.banner{padding:10px 12px; border-radius:8px; font-weight:600; color:#1b5e20; background:#e8f5e9; margin-bottom:8px; word-break:break-word;}
.banner.error{color:#b71c1c; background:#ffebee;}
.files-wrapper{overflow-x:auto; margin-top:10px;}
table{width:100%; border-collapse:collapse; min-width:820px; font-size:14px;}
th,td{padding:8px; border-bottom:1px solid #e0e0e0; text-align:left;}
th{background:#0d47a1; color:#fff; position:sticky; top:0;}
code{font-family:ui-monospace,SFMono-Regular,Menlo,Consolas,monospace; font-size:13px;}
</style>
</head>
<body>
<h1>ESP32 Firmware Tamper Detector</h1>
)rawliteral";

  if (message.length()) {
    bool isError = message.indexOf("Tampered") >= 0 || message.indexOf("error") >= 0 || message.indexOf("Error") >= 0;
    html += "<div class='banner";
    if (isError) html += " error";
    html += "'>" + htmlEscape(message) + "</div>";
  }

  // Upload forms (kept)
  html += R"rawliteral(
<div class="card">
<h3>Upload Firmware (Log Hash)</h3>
<form method="POST" action="/upload" enctype="multipart/form-data">
<input type="file" name="upload" required>
<div style="height:8px"></div>
<button style="width:100%;padding:10px;border-radius:8px;background:#4CAF50;color:#fff;border:none;" type="submit">Upload & Log Hash</button>
</form>
</div>

<div class="card">
<h3>Scan Firmware Files</h3>
<form method="POST" action="/scan">
<button style="width:100%;padding:10px;border-radius:8px;background:#2196F3;color:#fff;border:none;" type="submit">Scan Now</button>
</form>
</div>

<div class="card">
<h3>Analytics</h3>
<div class="row">
<div class="col card"><canvas id="uploadsChart" height="150"></canvas></div>
<div class="col card"><canvas id="tamperChart" height="150"></canvas></div>
</div>
<div style="height:8px"></div>
<div class="row">
<div class="col card"><strong>Device Uptime:</strong> <span id="uptime">--</span></div>
<div class="col card"><strong>Last Sync:</strong> <span id="lastsync">--</span></div>
</div>
</div>

<div class="card">
<h3>Firmware Inventory</h3>
<div class="files-wrapper">
<table>
<tr><th>File</th><th>Size (bytes)</th><th>Logged Hash</th><th>Current Hash</th><th>Status</th></tr>
)rawliteral";

  html += tableRows;
  html += R"rawliteral(
</table></div></div>

<script>
async function fetchStats() {
  try {
    let res = await fetch('/stats');
    let j = await res.json();
    // populate uptime and lastsync
    document.getElementById('uptime').innerText = j.uptime;
    document.getElementById('lastsync').innerText = j.lastSync;
    // draw uploads chart
    const uploadCtx = document.getElementById('uploadsChart').getContext('2d');
    const tamperCtx = document.getElementById('tamperChart').getContext('2d');

    const uploadsData = {
      labels: j.labels,
      datasets: [{ label: 'Uploads', data: j.uploadCounts, tension:0.3 }]
    };
    const tamperData = {
      labels: j.labels,
      datasets: [{ label: 'Tamper Events', data: j.tamperCounts, tension:0.3 }]
    };

    if (window.uploadChart) window.uploadChart.destroy();
    if (window.tamperChart) window.tamperChart.destroy();

    window.uploadChart = new Chart(uploadCtx, { type:'line', data:uploadsData, options:{responsive:true} });
    window.tamperChart = new Chart(tamperCtx, { type:'line', data:tamperData, options:{responsive:true} });

  } catch(e) {
    console.error(e);
  }
}
fetchStats();
setInterval(fetchStats, 15000); // refresh every 15s
</script>

</body></html>
)rawliteral";

  return html;
}

// Web routes, upload and scan (integrated with previous logic) ------------
File uploadFile;
mbedtls_sha256_context upCtx;
bool upStarted = false;
String upTargetFile = "";
String upFullPath = "";
uint8_t upHash[32];

void setupUploadRoute() {
  server.on("/upload", HTTP_POST,
    []() {
      // empty stub for multi-part
    },
    []() {
      HTTPUpload& upload = server.upload();

      if (upload.status == UPLOAD_FILE_START) {
        String fname = sanitizeFilename(upload.filename);
        upTargetFile = fname;
        upFullPath = String(FIRMWARE_DIR) + "/" + fname;

        if (!SD.exists(FIRMWARE_DIR)) SD.mkdir(FIRMWARE_DIR);
        if (SD.exists(upFullPath)) SD.remove(upFullPath);

        uploadFile = SD.open(upFullPath, FILE_WRITE);
        if (!uploadFile) {
          lastMessage = "ERROR: Cannot open file for writing.";
          Serial.println(lastMessage);
          return;
        }

        mbedtls_sha256_init(&upCtx);
        mbedtls_sha256_starts(&upCtx, 0);
        upStarted = true;
        Serial.println("Upload start: " + fname);

      } else if (upload.status == UPLOAD_FILE_WRITE) {
        if (uploadFile) {
          uploadFile.write(upload.buf, upload.currentSize);
          mbedtls_sha256_update(&upCtx, upload.buf, upload.currentSize);
        }

      } else if (upload.status == UPLOAD_FILE_END) {
        if (uploadFile) uploadFile.close();
        Serial.println("Upload finished. Size: " + String(upload.totalSize));

        mbedtls_sha256_finish(&upCtx, upHash);
        mbedtls_sha256_free(&upCtx);
        String hex = toHexString(upHash, 32);

        // upsert old textual log for hash lookups and create backup
        if (upTargetFile.length() && upsertLog(upTargetFile, hex)) {
          // create backup copy (timestamped)
          String backupName = backupFilenameWithTs(upTargetFile);
          bool backed = createBackup(upFullPath, backupName);
          if (backed) {
            logEventCSV("upload", upTargetFile, upload.totalSize, hex, "logged_and_backed", backupName);
          } else {
            logEventCSV("upload", upTargetFile, upload.totalSize, hex, "logged_no_backup", "");
          }

          // Blynk notify & logEvent
          String msg = "Uploaded: " + upTargetFile + " • Hash: " + hex;
          Blynk.virtualWrite(V0, msg); // optional V0 display
          Blynk.notify(msg.c_str());
          Blynk.logEvent("alert", msg.c_str());

          lastMessage = "Uploaded: " + upTargetFile + " • Logged Hash: " + hex;
        } else {
          lastMessage = "Upload complete, but failed to log hash.";
          logEventCSV("upload", upTargetFile, upload.totalSize, hex, "logged_failed", "");
        }

        Serial.println(lastMessage);
        upStarted = false;
        upTargetFile = "";
        upFullPath = "";

        server.send(200, "text/html", mainPageHTML(lastMessage));

      } else if (upload.status == UPLOAD_FILE_ABORTED) {
        if (uploadFile) uploadFile.close();
        if (upFullPath.length() && SD.exists(upFullPath)) SD.remove(upFullPath);
        mbedtls_sha256_free(&upCtx);
        upStarted = false;
        lastMessage = "ERROR: Upload aborted.";
        Serial.println(lastMessage);
      }
    }
  );
}

void setupScanRoute() {
  server.on("/scan", HTTP_POST, []() {
    bool anyTampered = false;
    String rows = buildFileRowsScan(anyTampered);
    if (anyTampered) {
      digitalWrite(ledPin, HIGH);
      lastMessage = "Scan complete: Tampered firmware detected.";
      // Log overall scan event
      logEventCSV("scan", "-", 0, "-", "tamper_detected", "See per-file logs");
      String msg = "Tamper detected on device!";
      Blynk.notify(msg.c_str());
      Blynk.logEvent("alert", msg.c_str());
    } else {
      digitalWrite(ledPin, LOW);
      lastMessage = "Scan complete: All firmware authentic.";
      logEventCSV("scan", "-", 0, "-", "all_authentic", "");
    }
    server.send(200, "text/html", mainPageHTML(lastMessage, rows));
    lastMessage = "";
  });
}

// Stats endpoint used by Chart.js --------------------------------
String readCSVlines(int maxLines) {
  File f = SD.open(LOG_CSV_PATH, FILE_READ);
  if (!f) return "";
  // skip header
  f.readStringUntil('\n');
  String out = "";
  int c = 0;
  while (f.available() && c < maxLines) {
    String line = f.readStringUntil('\n');
    if (line.length()) {
      out += line + "\n";
      c++;
    }
  }
  f.close();
  return out;
}

void handleStats() {
  // Build a simple JSON summary: labels (times), uploadCounts, tamperCounts, uptime, lastSync
  // We'll read last 100 CSV entries and aggregate by minute/hour label
  const int MAX_READ = 200;
  File f = SD.open(LOG_CSV_PATH, FILE_READ);
  if (!f) {
    server.send(200, "application/json", "{\"labels\":[],\"uploadCounts\":[],\"tamperCounts\":[],\"uptime\":\"--\",\"lastSync\":\"--\"}");
    return;
  }
  // Read entire file to memory (beware large files) — but ok for demo sizes
  // Skip header
  String header = f.readStringUntil('\n');
  std::vector<String> labelsVec;
  std::vector<int> uploadsVec;
  std::vector<int> tamperVec;
  // We'll aggregate by HH:MM labels (last 12 points)
  std::map<String, std::pair<int,int>> agg; // label -> (uploads, tampers)
  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) continue;
    // parse CSV: timestamp,event,file,size,sha256,result,details
    // naive split by , but timestamp and file and sha are quoted
    // We'll do a simple parse assuming format we wrote
    int firstQ = line.indexOf('"');
    int secondQ = line.indexOf('"', firstQ+1);
    String ts = (firstQ>=0 && secondQ>firstQ) ? line.substring(firstQ+1, secondQ) : String(millis());
    // event is next token after comma
    int pos = secondQ+2;
    int comma2 = line.indexOf(',', pos);
    String event = (comma2>pos) ? line.substring(pos, comma2) : "";
    // result - find the token after sha and size; we'll search for result by scanning tokens
    // For aggregation we only need to detect uploads and tamper/restore
    String lowerEvent = event;
    lowerEvent.toLowerCase();
    // build label HH:MM from ts if possible
    String label = ts;
    if (ts.length() >= 16) label = ts.substring(11,16); // HH:MM
    // update agg
    auto it = agg.find(label);
    if (it == agg.end()) agg[label] = {0,0};
    if (lowerEvent.indexOf("upload") >= 0) agg[label].first++;
    if (lowerEvent.indexOf("restore") >= 0 || lowerEvent.indexOf("tamper") >= 0 || lowerEvent.indexOf("scan") >= 0) {
      // for tamperCount, count tamper-specific results
      if (line.indexOf(",Tampered,") >= 0 || line.indexOf(",tamper_detected,") >= 0 || lowerEvent.indexOf("restore")>=0) agg[label].second++;
    }
  }
  f.close();
  // prepare last N labels (sorted)
  std::vector<String> keys;
  for (auto &kv : agg) keys.push_back(kv.first);
  std::sort(keys.begin(), keys.end()); // ascending
  // limit to last 12
  int start = 0;
  if ((int)keys.size() > 12) start = keys.size() - 12;
  String json = "{";
  json += "\"labels\":[";
  for (int i = start; i < (int)keys.size(); i++) {
    if (i>start) json += ",";
    json += "\"" + keys[i] + "\"";
  }
  json += "],";
  json += "\"uploadCounts\":[";
  for (int i = start; i < (int)keys.size(); i++) {
    if (i>start) json += ",";
    json += String(agg[keys[i]].first);
  }
  json += "],";
  json += "\"tamperCounts\":[";
  for (int i = start; i < (int)keys.size(); i++) {
    if (i>start) json += ",";
    json += String(agg[keys[i]].second);
  }
  json += "],";
  // uptime
  unsigned long upsecs = (millis() - bootMillis) / 1000;
  unsigned long h = upsecs/3600; unsigned long m = (upsecs%3600)/60; unsigned long s = upsecs%60;
  char upbuf[64];
  sprintf(upbuf, "%lu:%02lu:%02lu", h, m, s);
  json += "\"uptime\":\"" + String(upbuf) + "\",";
  json += "\"lastSync\":\"" + nowStr() + "\"";
  json += "}";
  server.send(200, "application/json", json);
}

// Root handler ------------------------------------------------
void handleRoot() {
  server.send(200, "text/html", mainPageHTML(lastMessage));
  lastMessage = "";
}

// Setup routine ------------------------------------------------
void setup() {
  Serial.begin(115200);
  pinMode(ledPin, OUTPUT);
  digitalWrite(ledPin, LOW);

  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  int nettries = 0;
  while (WiFi.status() != WL_CONNECTED && nettries < 30) { delay(500); Serial.print("."); nettries++; }
  if (WiFi.status() == WL_CONNECTED) Serial.println("\nConnected! IP: " + WiFi.localIP().toString());
  else Serial.println("\nWiFi connect failed, continuing (will attempt reconnection later).");

  // Blynk
  Blynk.begin(auth, ssid, password);

  // NTP
  initTime();

  // SD
  if (!SD.begin(chipSelect)) { Serial.println("SD Card Mount Failed!"); while(1) delay(1000); }
  Serial.println("SD Card initialized.");
  if (!SD.exists(FIRMWARE_DIR)) SD.mkdir(FIRMWARE_DIR);
  if (!SD.exists(BACKUP_DIR)) SD.mkdir(BACKUP_DIR);
  ensureLogHeader();

  // create firmware_log.txt if absent
  if (!SD.exists("/firmware_log.txt")) { File t = SD.open("/firmware_log.txt", FILE_WRITE); if (t) t.close(); }

  // web routes
  server.on("/", HTTP_GET, handleRoot);
  setupUploadRoute();
  setupScanRoute();
  server.on("/stats", HTTP_GET, handleStats);
  server.begin();
  Serial.println("Server started");

  bootMillis = millis();
}

// Loop --------------------------------------------------------
void loop() {
  server.handleClient();
  Blynk.run();
}
