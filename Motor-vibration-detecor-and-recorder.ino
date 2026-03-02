#define BLYNK_TEMPLATE_ID ""
#define BLYNK_TEMPLATE_NAME ""
#define BLYNK_AUTH_TOKEN ""

#include <WiFi.h>
#include <WebServer.h>
#include <BlynkSimpleEsp32.h>
#include <driver/i2s.h>
#include <SD.h>
#include <SPI.h>
#include <vector>

// --- การตั้งค่าขาเชื่อมต่อ ---
#define SD_CS   5
#define I2S_WS  15
#define I2S_SD  32
#define I2S_SCK 14
#define I2S_PORT I2S_NUM_0

// --- การตั้งค่าเสียง ---
#define SAMPLE_RATE 16000
#define RECORD_TIME 10
#define BIT_PER_SAMPLE 16
float AUDIO_GAIN = 0.6; 
int BIT_SHIFT = 14;

char auth[] = BLYNK_AUTH_TOKEN;
WebServer server(80);
File uploadFile;

// --- ตัวแปรสถานะและไฟล์ ---
String currentFileName;
bool isSDReady = false;
bool pendingRefresh = false;
bool triggerStartTimer = false;
bool triggerStopTimer = false;
String lastStatus = "Ready to REC";
const char* CONFIG_FILE = "/config.txt";

struct WiFiCredential {
  String ssid;
  String pass;
};
std::vector<WiFiCredential> wifiList;

struct WavHeader {
  char riff[4] = { 'R', 'I', 'F', 'F' };
  uint32_t file_size;
  char wave[4] = { 'W', 'A', 'V', 'E' };
  char fmt[4] = { 'f', 'm', 't', ' ' };
  uint32_t fmt_size = 16;
  uint16_t audio_format = 1;
  uint16_t num_channels = 1;
  uint32_t sample_rate = SAMPLE_RATE;
  uint32_t byte_rate = SAMPLE_RATE * 1 * (BIT_PER_SAMPLE / 8);
  uint16_t block_align = 1 * (BIT_PER_SAMPLE / 8);
  uint16_t bits_per_sample = BIT_PER_SAMPLE;
  char data[4] = { 'd', 'a', 't', 'a' };
  uint32_t data_size;
};

// --- ส่วนหน้าจอควบคุม (Web UI) ---
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html><head>
  <meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'>
  <title>Stethoscope Studio</title>
  <style>
    :root { --primary: #1a1a1a; --accent: #ff3b30; --success: #4cd964; --bg: #f2f2f7; --card: #ffffff; --blue: #007aff; }
    body { font-family: -apple-system, system-ui, sans-serif; margin: 0; background: var(--bg); color: #000; }
    .navbar { background: var(--primary); display: flex; justify-content: center; padding: 5px 0; position: sticky; top: 0; z-index: 100; }
    .navbar a { color: #8e8e93; padding: 15px 15px; text-decoration: none; font-size: 12px; font-weight: 600; text-transform: uppercase; letter-spacing: 1px; cursor: pointer; }
    .navbar a.active { color: #fff; border-bottom: 2px solid var(--blue); }
    
    .info-bar { display: flex; justify-content: space-around; background: #fff; padding: 12px; font-size: 10px; border-bottom: 0.5px solid #d1d1d6; }
    .info-item { font-weight: 700; flex: 1; text-align: center; color: #000; }
    .status-val { font-weight: 700; }
    
    .content { padding: 20px; display: none; max-width: 500px; margin: 0 auto; }
    .active { display: block; }
    .rec-container { background: var(--card); border-radius: 20px; padding: 30px 20px; box-shadow: 0 10px 30px rgba(0,0,0,0.05); margin-top: 20px; text-align: center; }
    
    .gain-box { background: #f9f9f9; padding: 20px; border-radius: 15px; margin: 20px 0; border: 1px solid #eee; }
    .gain-header { display: flex; justify-content: space-between; align-items: center; margin-bottom: 15px; }
    #gain-val-input { width: 60px; padding: 5px; border-radius: 8px; border: 1px solid #ddd; text-align: center; font-weight: bold; }
    .slider { -webkit-appearance: none; width: 100%; height: 8px; border-radius: 5px; background: #ddd; outline: none; margin: 15px 0; }
    .slider::-webkit-slider-thumb { -webkit-appearance: none; appearance: none; width: 24px; height: 24px; border-radius: 50%; background: var(--blue); cursor: pointer; box-shadow: 0 2px 5px rgba(0,0,0,0.2); }

    #btn-record { width: 100px; height: 100px; background: var(--accent); border-radius: 50%; border: none; color: white; font-weight: bold; font-size: 18px; cursor: pointer; box-shadow: 0 0 0 8px rgba(255,59,48,0.1); transition: 0.2s; }
    #btn-record:disabled { background: #c7c7cc; box-shadow: none; transform: scale(0.95); }
    .file-card { background: #fff; padding: 16px; border-radius: 16px; margin-bottom: 12px; text-align: left; box-shadow: 0 2px 8px rgba(0,0,0,0.04); border-left: 4px solid var(--blue); position: relative; }
    audio { width: 100%; height: 35px; margin-top: 10px; }
    .btn-del { color: var(--accent); font-size: 12px; font-weight: 600; cursor: pointer; margin-top: 10px; display: inline-block; }
    input[type="text"], textarea { width: 100%; padding: 14px; border-radius: 12px; border: 1px solid #d1d1d6; margin-top: 10px; box-sizing: border-box; font-size: 16px; }
    textarea { font-family: monospace; font-size: 12px; height: 200px; resize: vertical; }
    .save-btn { background: var(--blue); color: white; border: none; padding: 12px 20px; border-radius: 10px; width: 100%; margin-top: 10px; font-weight: 600; cursor: pointer; }
    .config-box { text-align: left; background: #fff; padding: 20px; border-radius: 16px; border: 1px solid #eee; }

    .modal { display: none; position: fixed; z-index: 1000; left: 0; top: 0; width: 100%; height: 100%; background: rgba(0,0,0,0.5); }
    .modal-content { background: white; margin: 30% auto; padding: 20px; border-radius: 15px; width: 80%; max-width: 300px; text-align: center; }
    .modal-btns { display: flex; justify-content: space-around; margin-top: 20px; }
    .btn-modal { padding: 10px 20px; border-radius: 8px; border: none; cursor: pointer; font-weight: 600; }
    .btn-confirm { background: var(--accent); color: white; }
    .btn-cancel { background: #e5e5ea; color: #000; }
  </style>
</head><body>
  <div class="navbar">
    <a id="t-rec" class="active" onclick="showPage('rec-page', 't-rec')">บันทึก</a>
    <a id="t-file" onclick="showPage('file-page', 't-file')">ไฟล์</a>
    <a id="t-cfg" onclick="showPage('cfg-page', 't-cfg')">ตั้งค่า</a>
  </div>
  
  <div class="info-bar">
    <div id="wifi-status" class="info-item">📡 WiFi: <span class="status-val">...</span></div>
    <div id="sd-status" class="info-item">💾 SD: <span class="status-val">...</span></div>
    <div id="mic-status" class="info-item">🎙️ MIC: <span class="status-val">...</span></div>
  </div>

  <div id="rec-page" class="content active">
    <div class="rec-container">
      <div id="rec-filename" style="font-weight:600; color:var(--blue); margin-bottom:10px;">...</div>
      <div class="gain-box">
        <div class="gain-header">
            <span style="font-size:13px; color:#666;">AUDIO GAIN</span>
            <input type="number" id="gain-val-input" min="0" max="1" step="0.01" value="0.60">
        </div>
        <input type="range" id="gain-slider" min="0" max="1" step="0.01" value="0.60" class="slider">
      </div>
      <button id="btn-record" onclick="startRec()">REC</button>
      <div id="rec-status" style="margin-top:20px; font-size:13px; color:#8e8e93;">สถานะ: ...</div>
      <input type="text" id="renameInput" placeholder="กรุณาตั้งชื่อไฟล์..." style="text-align: center;">
      <button class="save-btn" onclick="validateAndRename()">เปลี่ยนชื่อ</button>
    </div>
  </div>

  <div id="file-page" class="content">
    <div id="fileList">กำลังโหลดรายการไฟล์...</div>
  </div>

  <div id="cfg-page" class="content">
    <div class="config-box">
      <h3 style="margin-top:0; font-size:16px;">⚙️ แก้ไขไฟล์ Config</h3>
      <textarea id="configText" placeholder="กำลังโหลดข้อมูล..."></textarea>
      <button class="save-btn" onclick="saveConfigText()">บันทึกการตั้งค่า</button>
    </div>
  </div>

  <div id="confirmModal" class="modal">
    <div class="modal-content">
      <div id="modalMsg" style="font-size:14px; margin-bottom:10px;">ชื่อไฟล์นี้มีอยู่แล้ว ต้องการเขียนทับหรือไม่?</div>
      <div class="modal-btns">
        <button class="btn-modal btn-cancel" onclick="closeModal()">ยกเลิก</button>
        <button class="btn-modal btn-confirm" id="modalConfirmBtn">ตกลง</button>
      </div>
    </div>
  </div>

<script>
  const gainInput = document.getElementById('gain-val-input');
  const gainSlider = document.getElementById('gain-slider');
  const renameInput = document.getElementById('renameInput');
  const recFilenameDisplay = document.getElementById('rec-filename');
  const confirmModal = document.getElementById('confirmModal');
  const modalConfirmBtn = document.getElementById('modalConfirmBtn');
  
  let isFirstLoad = true;
  let existingFiles = [];

  function updateRenamePlaceholder() {
    let currentText = recFilenameDisplay.innerText.replace(".wav", "");
    if(currentText && currentText !== "...") {
       renameInput.value = currentText;
    }
  }

  function showPage(p, t) {
    document.querySelectorAll('.content').forEach(el => el.classList.remove('active'));
    document.querySelectorAll('.navbar a').forEach(el => el.classList.remove('active'));
    document.getElementById(p).classList.add('active');
    document.getElementById(t).classList.add('active');
    if(p == 'rec-page') updateRenamePlaceholder();
    if(p == 'file-page') loadFiles();
    if(p == 'cfg-page') loadConfigContent();
  }

  function startRec() {
    document.getElementById('btn-record').disabled = true;
    fetch('/start_rec').catch(err => {
      document.getElementById('rec-status').innerText = "สถานะ: การเชื่อมต่อผิดพลาด";
      document.getElementById('btn-record').disabled = false;
    });
  }

  gainSlider.oninput = function() { gainInput.value = parseFloat(this.value).toFixed(2); }
  gainSlider.onchange = function() { sendGain(this.value); }
  gainInput.onchange = function() {
    let v = Math.min(1, Math.max(0, this.value));
    this.value = v.toFixed(2);
    gainSlider.value = v;
    sendGain(v);
  }

  function sendGain(val) {
    fetch('/set_gain?value=' + val).then(r => console.log('Gain updated'));
  }

  function validateAndRename() {
    const name = renameInput.value.trim();
    if(!name) return;
    const fullName = name + ".wav";
    if (existingFiles.includes(fullName)) {
      document.getElementById('modalMsg').innerText = "ชื่อไฟล์ '" + fullName + "' มีอยู่แล้ว ต้องการใช้ชื่อนี้และเขียนทับหรือไม่?";
      confirmModal.style.display = "block";
      modalConfirmBtn.onclick = function() {
        closeModal();
        updateName(name);
      };
    } else {
      updateName(name);
    }
  }

  function closeModal() { confirmModal.style.display = "none"; }

  function updateName(name) {
    fetch('/rename?name=' + encodeURIComponent(name)).then(() => {
      recFilenameDisplay.innerText = name + ".wav";
      loadFiles();
    });
  }

  function loadFiles() {
    fetch('/list').then(r => r.text()).then(html => {
      document.getElementById('fileList').innerHTML = html;
      const tempDiv = document.createElement('div');
      tempDiv.innerHTML = html;
      existingFiles = Array.from(tempDiv.querySelectorAll('strong')).map(el => el.innerText.trim());
    });
  }

  function loadConfigContent() {
    fetch('/get_config').then(r => r.text()).then(txt => {
      document.getElementById('configText').value = txt;
    });
  }

  function saveConfigText() {
    const txt = document.getElementById('configText').value;
    fetch('/save_config_raw', { method: 'POST', body: txt }).then(r => {
      if(r.ok) alert('บันทึกสำเร็จ');
      else alert('เกิดข้อผิดพลาดในการบันทึก');
    });
  }

  function deleteFile(name) {
    if(confirm('ลบไฟล์ ' + name + '?')) {
      fetch('/delete?file=' + name).then(() => loadFiles());
    }
  }

  if (!!window.EventSource) {
    var source = new EventSource('/events');
    source.onmessage = function(e) {
      if (!e.data) return;
      const lines = e.data.split('\n');
      lines.forEach(line => {
        if (line === "refresh") {
          loadFiles();
        } else if (line === "start_timer") {
          document.getElementById('btn-record').disabled = true;
        } else if (line === "stop_timer") {
          document.getElementById('btn-record').disabled = false;
        } else if (line.startsWith("status:")) {
          document.getElementById('rec-status').innerText = "สถานะ: " + line.replace("status:", "");
        } else if (line.startsWith("mic:")) {
          let val = line.replace("mic:", "");
          let span = document.querySelector('#mic-status .status-val');
          span.innerText = val;
          span.style.color = (val === "พร้อมใช้งาน") ? "var(--success)" : "var(--accent)";
        } else if (line.startsWith("filename:")) {
          let fname = line.replace("filename:", "");
          recFilenameDisplay.innerText = fname + ".wav";
          if (isFirstLoad) {
            renameInput.value = fname;
            isFirstLoad = false;
          }
        } else if (line.startsWith("wifi:")) {
          let val = line.replace("wifi:", "");
          let span = document.querySelector('#wifi-status .status-val');
          if (val === "Disconnected") {
            span.innerText = "ไม่ได้เชื่อมต่อ";
            span.style.color = "var(--accent)";
          } else {
            span.innerText = val;
            span.style.color = "var(--success)";
          }
        } else if (line.startsWith("sd:")) {
          let val = line.replace("sd:", "");
          let span = document.querySelector('#sd-status .status-val');
          if (val === "Ready") {
            span.innerText = "ใช้งานได้";
            span.style.color = "var(--success)";
          } else {
            span.innerText = "ใช้งานไม่ได้";
            span.style.color = "var(--accent)";
          }
        } else if (line.startsWith("gain:")) {
          let g = parseFloat(line.replace("gain:", ""));
          gainInput.value = g.toFixed(2);
          gainSlider.value = g;
        }
      });
    };
  }
  window.onload = loadFiles;
</script>
</body></html>)rawliteral";

// --- ฟังก์ชันการทำงานพื้นฐาน ---

void sendStatus(String s) { lastStatus = s; }

void handleRoot() { server.send_P(200, "text/html", index_html); }

void handleEvents() {
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/event-stream", "");
  
  String wifiStat = (WiFi.status() == WL_CONNECTED) ? WiFi.SSID() : "Disconnected";
  String sdStat = (SD.begin(SD_CS)) ? "Ready" : "No Card";
  String micStat = "พร้อมใช้งาน";
  
  String msg = "";
  msg += "data: status:" + lastStatus + "\n";
  msg += "data: mic:" + micStat + "\n";
  msg += "data: filename:" + currentFileName + "\n";
  msg += "data: wifi:" + wifiStat + "\n";
  msg += "data: sd:" + sdStat + "\n";
  msg += "data: gain:" + String(AUDIO_GAIN) + "\n";
  
  if (triggerStartTimer) { msg += "data: start_timer\n"; triggerStartTimer = false; }
  if (triggerStopTimer) { msg += "data: stop_timer\n"; triggerStopTimer = false; }
  if (pendingRefresh) { msg += "data: refresh\n"; pendingRefresh = false; }
  
  msg += "\n\n";
  server.sendContent(msg);
}

void saveConfig() {
  if (!SD.begin(SD_CS)) return;
  File file = SD.open(CONFIG_FILE, FILE_WRITE);
  if (file) {
    file.println("# Filename\n" + currentFileName);
    file.println("# AudioGain\n" + String(AUDIO_GAIN));
    file.println("# WiFi (Format: SSID,Password)");
    for (auto const& wifi : wifiList) {
      if(wifi.ssid.length() > 0) file.println(wifi.ssid + "," + wifi.pass);
    }
    file.close();
  }
}

void loadConfig() {
  if (!SD.begin(SD_CS)) { isSDReady = false; return; }
  isSDReady = true;
  if (!SD.exists(CONFIG_FILE)) return;
  File file = SD.open(CONFIG_FILE, FILE_READ);
  if (file) {
    wifiList.clear();
    String currentSection = "";
    while (file.available()) {
      String line = file.readStringUntil('\n');
      line.trim();
      if (line.length() == 0) continue;
      if (line.startsWith("#")) { currentSection = line; continue; }
      if (currentSection.indexOf("Filename") > -1) currentFileName = line;
      else if (currentSection.indexOf("AudioGain") > -1) AUDIO_GAIN = line.toFloat();
      else if (currentSection.indexOf("WiFi") > -1) {
        int comma = line.indexOf(',');
        if (comma != -1) {
          WiFiCredential wc; 
          wc.ssid = line.substring(0, comma); 
          wc.pass = line.substring(comma+1);
          wifiList.push_back(wc);
        }
      }
    }
    file.close();
  }
}

void prioritizeActiveWiFi() {
  String activeSSID = WiFi.SSID();
  int foundIndex = -1;
  for (int i = 0; i < (int)wifiList.size(); i++) {
    if (wifiList[i].ssid == activeSSID) {
      foundIndex = i;
      break;
    }
  }
  if (foundIndex > 0) {
    WiFiCredential active = wifiList[foundIndex];
    wifiList.erase(wifiList.begin() + foundIndex);
    wifiList.insert(wifiList.begin(), active);
    saveConfig();
  }
}

void handleGetConfig() {
  if (SD.exists(CONFIG_FILE)) {
    File f = SD.open(CONFIG_FILE, "r");
    server.streamFile(f, "text/plain");
    f.close();
  } else { server.send(200, "text/plain", "# No Config Found"); }
}

void handleSaveConfigRaw() {
  String content = server.arg("plain");
  File f = SD.open(CONFIG_FILE, FILE_WRITE);
  if (f) {
    f.print(content);
    f.close();
    loadConfig(); 
    server.send(200, "text/plain", "OK");
  } else { server.send(500, "text/plain", "SD Error"); }
}

void handleStartRec() {
  server.send(200, "text/plain", "OK");
  recordAudio();
}

void handleSetGain() {
  if (server.hasArg("value")) {
    AUDIO_GAIN = server.arg("value").toFloat();
    saveConfig();
  }
  server.send(200, "text/plain", "OK");
}

void handleRename() {
  if (server.hasArg("name")) {
    currentFileName = server.arg("name");
    saveConfig();
    Blynk.virtualWrite(V4, currentFileName);
  }
  server.send(200, "text/plain", "OK");
}

void recordAudio() {
  isSDReady = SD.begin(SD_CS);
  if (!isSDReady) { sendStatus("Error: No SD Card"); return; }
  triggerStartTimer = true;
  String path = "/" + currentFileName + ".wav";
  if (SD.exists(path)) SD.remove(path);
  File file = SD.open(path, FILE_WRITE);
  if (!file) { sendStatus("Error: File Write Fail"); triggerStopTimer = true; return; }
  sendStatus("กำลังบันทึก...");
  Blynk.virtualWrite(V2, "Recording...");
  Blynk.virtualWrite(V1, 1);
  for (int i = 0; i < 44; i++) file.write(0);
  uint32_t totalSamples = (uint32_t)SAMPLE_RATE * RECORD_TIME;
  uint32_t samplesCount = 0;
  size_t bytesRead;
  int32_t rawData;
  while (samplesCount < totalSamples) {
    if (i2s_read(I2S_PORT, &rawData, sizeof(rawData), &bytesRead, portMAX_DELAY) == ESP_OK) {
      int16_t sample = (int16_t)constrain((rawData >> BIT_SHIFT) * AUDIO_GAIN, -32768, 32767);
      file.write((uint8_t*)&sample, sizeof(sample));
      samplesCount++;
    }
  }
  WavHeader h;
  h.file_size = 36 + (samplesCount * 2);
  h.data_size = samplesCount * 2;
  file.seek(0);
  file.write((uint8_t*)&h, sizeof(h));
  file.close();
  triggerStopTimer = true;
  sendStatus("บันทึกแล้ว: " + currentFileName + ".wav");
  Blynk.virtualWrite(V2, String("Saved: ") + currentFileName + ".wav");
  Blynk.virtualWrite(V1, 0);
  pendingRefresh = true;
}

void handleList() {
  String html = "";
  File root = SD.open("/");
  File file = root.openNextFile();
  if(!file) html = "<div style='padding:20px'>ไม่พบไฟล์ในเครื่อง</div>";
  while(file){
    String n = String(file.name());
    if(n.startsWith("/")) n.remove(0,1);
    if(n != "config.txt") {
      html += "<div class='file-card'><div><strong>" + n + "</strong></div>";
      if(n.endsWith(".wav")) html += "<audio controls preload='none' src='/download?file=" + n + "'></audio>";
      html += "<div><a href='/download?file=" + n + "' style='font-size:12px'>[ดาวน์โหลด]</a>";
      html += "<span class='btn-del' onclick=\"deleteFile('" + n + "')\" style='margin-left:15px'>[ลบไฟล์]</span></div></div>";
    }
    file = root.openNextFile();
  }
  server.send(200, "text/html", html);
}

void handleDownload() {
  if (server.hasArg("file")) {
    String fileName = server.arg("file");
    String fullPath = "/" + fileName;
    if (SD.exists(fullPath)) {
      File f = SD.open(fullPath, "r");
      server.sendHeader("Content-Disposition", "attachment; filename=\"" + fileName + "\"");
      server.streamFile(f, "application/octet-stream");
      f.close(); 
      return;
    }
  }
  server.send(404, "text/plain", "File Not Found");
}

void handleDelete() {
  if (server.hasArg("file")) {
    SD.remove("/" + server.arg("file"));
    server.send(200, "text/plain", "Deleted");
  }
}

void setupI2S() {
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_RIGHT,
    .communication_format = (i2s_comm_format_t)(I2S_COMM_FORMAT_I2S | I2S_COMM_FORMAT_I2S_MSB),
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8, .dma_buf_len = 1024, .use_apll = false
  };
  i2s_pin_config_t pin_config = { .bck_io_num = I2S_SCK, .ws_io_num = I2S_WS, .data_out_num = I2S_PIN_NO_CHANGE, .data_in_num = I2S_SD };
  i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
  i2s_set_pin(I2S_PORT, &pin_config);
}

void connectMultiWiFi() {
  WiFi.mode(WIFI_STA);
  bool connected = false;
  while (!connected) {
    loadConfig();
    if (wifiList.empty()) { delay(5000); continue; }
    for (int i = 0; i < (int)wifiList.size(); i++) {
      WiFi.disconnect(); delay(200);
      WiFi.begin(wifiList[i].ssid.c_str(), wifiList[i].pass.c_str());
      int retry = 0;
      while (WiFi.status() != WL_CONNECTED && retry < 15) { delay(500); retry++; }
      if (WiFi.status() == WL_CONNECTED) {
        prioritizeActiveWiFi();
        Blynk.config(auth); Blynk.connect();
        server.on("/", handleRoot);
        server.on("/events", handleEvents);
        server.on("/list", handleList);
        server.on("/download", handleDownload);
        server.on("/delete", handleDelete);
        server.on("/start_rec", handleStartRec);
        server.on("/rename", handleRename);
        server.on("/set_gain", handleSet_gain); 
        server.on("/get_config", handleGetConfig);
        server.on("/save_config_raw", HTTP_POST, handleSaveConfigRaw);
        server.begin();
        connected = true; break;
      }
    }
    if (!connected) delay(2000);
  }
}

void handleSet_gain() {
  if (server.hasArg("value")) {
    AUDIO_GAIN = server.arg("value").toFloat();
    saveConfig();
  }
  server.send(200, "text/plain", "OK");
}

BLYNK_CONNECTED() {
  Blynk.virtualWrite(V4, currentFileName);
  Blynk.virtualWrite(V1, 0);
}

BLYNK_WRITE(V3) {
  String newName = param.asString();
  if (newName.length() > 0) { 
    currentFileName = newName; 
    Blynk.virtualWrite(V4, currentFileName); 
    saveConfig(); 
  }
}

BLYNK_WRITE(V1) {
  if (param.asInt() == 1) recordAudio();
}

void setup() {
  Serial.begin(115200);
  setupI2S();
  if (SD.begin(SD_CS)) { isSDReady = true; loadConfig(); } else { isSDReady = false; }
  connectMultiWiFi();
}

void loop() {
  if (WiFi.status() == WL_CONNECTED) {
    Blynk.run();
    server.handleClient();
  } else {
    connectMultiWiFi();
  }
}
