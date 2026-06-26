/*
  ESP32 SMART PIGGY BANK — Teachable Machine Currency Detector
  - Serves HTTPS page so getUserMedia() works on mobile browsers.
  - Browser classifies currency note using Teachable Machine (TensorFlow.js).
  - ESP32 receives the label via /result?label=XXX, adds denomination to total.
  - 16x2 LCD I2C shows detected denomination and running total.

  GPIO 13 HARDWARE TRIGGER:
    - Connect switch between GPIO 13 and 3.3V (internal pull-down enabled).
    - Edge Detection: Fires instantly on the transition from LOW to HIGH.
    - No time delays/debounce, but won't machine-gun duplicate triggers.

  LCD WIRING (I2C):
    - SDA → GPIO 21, SCL → GPIO 22
    - VCC → 5V, GND → GND
    - Default I2C address: 0x27  (change to 0x3F if LCD stays blank)

    Enter Your teachable machine model url in line 233

  !! Replace MODEL_URL in the HTML with your Teachable Machine model URL !!
  !! Your model class names must be exactly: 10  20  100  500            !!

  
*/

#include <WiFi.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include "esp_https_server.h"

// ===== LCD =====
LiquidCrystal_I2C lcd(0x27, 16, 2);  // change to 0x3F if blank

// ===== WiFi =====
const char* ssid     = "";
const char* password = "";

// ===== GPIO =====
#define TRIGGER_PIN 13
#define OUTPUT_PIN  27  // <--- Added for the 4-second output trigger

// ===== Piggy Bank State =====
int currentAmount = 0;

// ===== Non-blocking pin 27 state tracking =====
volatile unsigned long pin27TurnOffTime = 0;
volatile bool pin27Active = false;

// ===== Non-blocking trigger state machine =====
enum TriggerState { IDLE, FIRE };
TriggerState trigState = IDLE;

volatile bool     triggerFired = false;
volatile uint32_t triggerCount = 0;

// Edge tracking variable (Zero debounce time delay, pure state tracking)
bool lastPinState = LOW;

httpd_handle_t server = NULL;

// ---------------------------------------------------------------
// Self-signed TLS certificate + key
// ---------------------------------------------------------------
static const char SERVER_CERT[] PROGMEM = R"CERT(
-----BEGIN CERTIFICATE-----
MIIDDTCCAfWgAwIBAgIUCOXw1rMyU/wcJEVnt7tvF2a07eYwDQYJKoZIhvcNAQEL
BQAwFjEUMBIGA1UEAwwLZXNwMzIubG9jYWwwHhcNMjYwNjE1MDUwMzM4WhcNMzYw
NjEyMDUwMzM4WjAWMRQwEgYDVQQDDAtlc3AzMi5sb2NhbDCCASIwDQYJKoZIhvcN
AQEBBQADggEPADCCAQoCggEBALnN8uT7HmU9eFEhzQ5SkHANK5X9je7upugOIUxa
8dcFNDh9fjcdQXKm+AUyv5BcrV1mG2gOrZNawohYIUZLTyq/GenHj3mIdp1V15fd
pkMp+bzMARd5QMcon7ghn8HsJY8fPZIgGyNUjTIhktVr0KOTMM4qjb6SvrSoSTxu
eAVEKXfTyqMdjIuajnAS+NTyW7m33zqybqsmHkF65P1U1hFomD6/72YJEKRR9Brc
lf61aLqnZcXV7CVJpnnXg/FO0a1hd0EgnvWAKRNOiEEJXq9X5kuBUApxw6p9DyF2
F/w8dMnvuGEzmUAnZSN7zzhHVhkpwQ1UNhJF12WaNe2F3m0CAwEAAaNTMFEwHQYD
VR0OBBYEFKKUsdCLsgOzAfbVu61wjrrr0OrBMB8GA1UdIwQYMBaAFKKUsdCLsgOz
AfbVu61wjrrr0OrBMA8GA1UdEwEB/wQFMAMBAf8wDQYJKoZIhvcNAQELBQADggEB
AB+RGnidCEzgbswEcTxlAZjT904qitamQeSd5vur8FBfrYGja8236w0XQIFBEbdQ
FV8eiOf3n1p/DXGOnL5EZ4EtulLNQ0H1yFJ473R9P8b57k0pzFWkCVOniRoEMCm5
FDAO5RkCWQERQR26WlIdtrMXtdjBTt/9uOww6eI0tW7f6nR30raPoPs3RJjNhPeg
4C7WLOAVz96e+n/Qp94oKHw9rKmbi8FlxEP46zFCdxvwDCMoq8cpOta0FxtLR3JF
GrRTHhcOXcGHyQvowi0p/oQe4GZLrk3dz2ROybFaTiXfXCNIxtRxji56cPtPTgAL
vNFYy8M6tPxluaMNMtOu55M=
-----END CERTIFICATE-----
)CERT";

static const char SERVER_KEY[] PROGMEM = R"KEY(
-----BEGIN PRIVATE KEY-----
MIIEvgIBADANBgkqhkiG9w0BAQEFAASCBKgwggSkAgEAAoIBAQC5zfLk+x5lPXhR
Ic0OUpBwDSuV/Y3u7qboDiFMWvHXBTQ4fX43HUFypvgFMr+QXK1dZhtoDq2TWsKI
WCFGS08qvxnpx495iHadVdeX3aZDKfm8zAEXeUDHKJ+4IZ/B7CWPHz2SIBsjVI0y
IZLVa9CjkzDOKo2+kr60qEk8bngFRCl308qjHYyLmo5wEvjU8lu5t986sm6rJh5B
euT9VNYRaJg+v+9mCRCkUfQa3JX+tWi6p2XF1ewlSaZ514PxTtGtYXdBIJ71gCkT
TohBCV6vV+ZLgVAKccOqfQ8hdhf8PHTJ77hhM5lAJ2Uje884R1YZKcENVDYSRddl
mjXthd5tAgMBAAECggEAGGRQhD3xNhI1RtVcoYo2OveHpsekB99dmu4r8eWohPVC
bz4OQTb+fgosWiZY3p9EBRRf+a+fms4Z5qNHLv649GAlCzsu+yHujGYWCPJInt2W
UvInHIlG9z4+hYIogmE5ZwoPX60GUwOJC7E7oPfn6mlqyU3t0Lxb+8Rx5NSNvgcE
9LlTXfAFzOahnXJkvPeq8N5K+5DG1oB2hbHQ8AOFPGyDtkdPoQlpZPybttTjV/KK
2hFozpjm1fFsk5QiztFgBmzSq4F7IAA8TD95+kvdf/FpCQWef4e9uXkgCQpNrwnk
eQDDEKVZN/lsl+WpDi+0O7ZQaiUW+peG5P7/gj6pQwKBgQDcD/ZYuHWcMz6QIdIn
cGj2R59XfqvyIY2bVGYP2P596rEgAy85qUYyBMKFwTscKfal5pHUR/Ek/rZ5EnKp
l75nI78/7ShLQGikhjKW9gp6lSpd2xr7aq/mIYV5WM09cAJY5a9kPtcn0Pao7Yg8
d31qq8K4vrqwNwKAMOk1VrqiTwKBgQDYJcjw3COBcwWsNVkEtb3m/AULfsvINj36
6OfS3ueK/WiomysgwcYV0ovD4jRnC6HcFmLgW6s33lYLYOp1Lt/phkLNhey7v1jD
KCvANBLoVU3sG0eulbj9nqnKHlhI02J089I7XZtGiEy0BLqE0TiJmCUEN4Y5DXXK
jTtKQYowgwKBgQDN4P0m+cPGU8bnT0zuauM37b/sOm0hRTTXKjml/+vv357AhVos
RuqOJxfZzPLBIZ1IjpLGGFxJsScD1DeD5JxUoAPwCa8V3/dGXOp9g0hAcMdHMZJn
vuM7mQbnhSXWobEAfDn/vi7KaFwrpLY9Y8jpADJXZtD/xSdIyPmVVkObTwKBgE0y
2ZzlEy1V1o3WE/AxtRy9oFOlusTMUsC91KalBE/JCEtH+FRfwQ7kPxT8QrkXF31S
5Ye6VeHDYDn6KGMoFcMDN/LNxWqdAefZ/h5MuwAOD6GncKezQ/oZZA0TX3bLQNwC
hXC8kwvS/IpDMhbj3uyN0ZK6/g58dzibzrRLyj5fAoGBAIesYijTUe1hqR83SdJx
Q6y0CT4XeTtbVYOmU+cvkPHVQEwTKx6BuDmIWIioboVWftJoi4F9+F9pVbYC3/Vy
orJmsyWGemvtVPa7nRECRjYpYNYAKrqMzmcCQLlfE8QuoUrGOTdqKSs+LSFe7t7U
W2yd5k6pMzv6gRI8IciTM8lt
-----END PRIVATE KEY-----
)KEY";

// [HTML block remains unchanged to preserve your UI script parameters...]
static const char PROGMEM INDEX_HTML[] = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8"/>
  <meta name="viewport" content="width=device-width,initial-scale=1"/>
  <title>ESP32 · Smart Piggy Bank</title>
  <script src="https://cdn.jsdelivr.net/npm/@tensorflow/tfjs@1.3.1/dist/tf.min.js"></script>
  <script src="https://cdn.jsdelivr.net/npm/@teachablemachine/image@0.8/dist/teachablemachine-image.min.js"></script>
  <style>
    @import url('https://fonts.googleapis.com/css2?family=Space+Grotesk:wght@400;600;700&family=JetBrains+Mono:wght@400;600&display=swap');
    :root{
      --bg:#0d0f14;--surface:#161a22;--border:#252b38;
      --accent:#f0c040;--accent2:#3b82f6;--text:#e8eaf0;
      --muted:#6b7585;--success:#22c55e;--radius:12px;
    }
    *,*::before,*::after{box-sizing:border-box;margin:0;padding:0}
    body{background:var(--bg);color:var(--text);font-family:'Space Grotesk',sans-serif;
      min-height:100vh;display:flex;flex-direction:column;align-items:center}
    header{width:100%;padding:18px 32px;background:var(--surface);
      border-bottom:1px solid var(--border);display:flex;align-items:center;gap:14px}
    .logo{width:36px;height:36px;background:var(--accent);border-radius:8px;
      display:flex;align-items:center;justify-content:center;font-size:18px;color:#000;font-weight:bold}
    header h1{font-size:1.1rem;font-weight:700}
    header p{font-size:.78rem;color:var(--muted);margin-top:1px}
    main{width:100%;max-width:960px;padding:32px 20px;
      display:grid;grid-template-columns:1fr 1fr;gap:24px}
    @media(max-width:640px){main{grid-template-columns:1fr}}
    .card{background:var(--surface);border:1px solid var(--border);
      border-radius:var(--radius);overflow:hidden;display:flex;flex-direction:column}
    .card-header{padding:14px 18px;border-bottom:1px solid var(--border);
      display:flex;align-items:center;gap:8px}
    .dot{width:8px;height:8px;border-radius:50%;background:var(--success);box-shadow:0 0 6px var(--success)}
    .dot-hw{width:8px;height:8px;border-radius:50%;background:var(--accent);box-shadow:0 0 6px var(--accent)}
    .card-header span{font-size:.8rem;font-weight:600;letter-spacing:.06em;
      text-transform:uppercase;color:var(--muted)}
    .stream-wrap{background:#000;display:flex;align-items:center;
      justify-content:center;min-height:240px;overflow:hidden;position:relative}
    video{width:100%;max-height:360px;object-fit:cover;
      transform-origin:center center;transition:transform .2s}
    .btn-container{padding:12px;display:flex;gap:8px;background:#11141a;flex-wrap:wrap}
    button{flex:1;padding:10px;background:var(--accent2);border:none;
      color:#fff;font-family:inherit;font-weight:600;border-radius:6px;
      cursor:pointer;transition:opacity .2s;min-width:80px}
    #analyzeBtn{background:var(--accent);color:#000}
    button:hover{opacity:.9}
    .zoom-row{padding:8px 12px;background:#0f1117;display:flex;align-items:center;gap:6px;flex-wrap:wrap}
    .zoom-row span{font-size:.75rem;color:var(--muted)}
    .zbtn{flex:0;padding:4px 10px;font-size:.75rem;background:#1e2330;
      border:1px solid var(--border);color:var(--text);border-radius:5px;cursor:pointer}
    .zbtn.active{background:var(--accent);color:#000;border-color:var(--accent)}
    .result-body{padding:20px 18px;display:flex;flex-direction:column;gap:12px}
    .badge{display:inline-flex;align-items:center;gap:6px;
      background:rgba(240,192,64,.12);border:1px solid rgba(240,192,64,.3);
      color:var(--accent);font-size:.75rem;font-weight:600;
      padding:4px 10px;border-radius:20px;align-self:flex-start}
    #hw-indicator{font-size:.75rem;font-family:'JetBrains Mono',monospace;
      padding:6px 12px;border-radius:6px;border:1px solid var(--border);
      background:rgba(255,255,255,.03);color:var(--muted);transition:all .3s}
    #hw-indicator.fired{border-color:var(--accent);color:var(--accent);
      background:rgba(240,192,64,.08)}
    #pred-text{font-family:'JetBrains Mono',monospace;font-size:.9rem;
      line-height:1.7;color:var(--success);background:rgba(255,255,255,.03);
      border:1px solid var(--border);border-radius:8px;padding:14px;
      min-height:80px;text-align:center;white-space:pre-wrap}
    #total-box{font-family:'JetBrains Mono',monospace;font-size:1.1rem;
      font-weight:700;color:var(--accent);background:rgba(240,192,64,.08);
      border:1px solid rgba(240,192,64,.3);border-radius:8px;padding:14px;
      text-align:center}
    .confidence-bar{height:6px;border-radius:3px;background:var(--border);margin-top:6px}
    .confidence-fill{height:100%;border-radius:3px;background:var(--success);transition:width .3s}
    .flash{animation:flashAnim .5s ease}
    @keyframes flashAnim{0%,100%{opacity:1}50%{opacity:.2}}
  </style>
</head>
<body>
<header>
  <div class="logo">&#8377;</div>
  <div>
    <h1>ESP32 &middot; Smart Piggy Bank</h1>
    <p>Teachable Machine currency detector &middot; GPIO 13 auto-trigger</p>
  </div>
</header>

<main>
  <div class="card">
    <div class="card-header"><div class="dot"></div><span>Device Camera</span></div>
    <div class="stream-wrap">
      <video id="webcam" autoplay playsinline muted></video>
    </div>
    <div class="zoom-row">
      <span>Zoom:</span>
      <button class="zbtn active" onclick="setZoom(1)">1×</button>
      <button class="zbtn" onclick="setZoom(1.5)">1.5×</button>
      <button class="zbtn" onclick="setZoom(2)">2×</button>
      <button class="zbtn" onclick="setZoom(3)">3×</button>
    </div>
    <div class="btn-container">
      <button onclick="startWebcam()">&#9654; Start Camera</button>
      <button onclick="switchCamera()">&#8635; Switch</button>
      <button id="analyzeBtn" onclick="manualCapture()">&#128247; Capture</button>
    </div>
  </div>

  <div class="card">
    <div class="card-header"><div class="dot-hw"></div><span>AI Result</span></div>
    <div class="result-body">
      <span class="badge">&#10024; Teachable Machine</span>
      <div id="hw-indicator">&#128268; GPIO 13: waiting for switch press...</div>
      <div id="pred-text">Loading model...</div>
      <div class="confidence-bar"><div class="confidence-fill" id="conf-fill" style="width:0%"></div></div>
      <div id="total-box">&#128012; Piggy Bank: &#8377; <span id="total-amount">0</span></div>
    </div>
  </div>
</main>

<canvas id="captureCanvas" style="display:none"></canvas>

<script>
  const MODEL_URL = "Enter your model url here";
  const VALID_DENOMS = ["10","20","100","500"];

  const video    = document.getElementById("webcam");
  const canvas   = document.getElementById("captureCanvas");
  const predBox  = document.getElementById("pred-text");
  const confFill = document.getElementById("conf-fill");
  const totalEl  = document.getElementById("total-amount");
  const hwInd    = document.getElementById("hw-indicator");

  let model             = null;
  let currentStream     = null;
  let useFrontCamera    = false;
  let currentZoom       = 1;
  let continuousRunning = false;
  let locked            = false;

  function setZoom(z) {
    currentZoom = z;
    video.style.transform = "scale(" + z + ")";
    document.querySelectorAll(".zbtn").forEach(b =>
      b.classList.toggle("active", parseFloat(b.textContent) === z)
    );
  }

  async function loadModel() {
    try {
      model = await tmImage.load(MODEL_URL + "model.json", MODEL_URL + "metadata.json");
      predBox.textContent = "Model ready.\nPoint at a note and press Capture,\nor pull GPIO 13 HIGH.";
    } catch(e) {
      predBox.textContent = "Model load error:\n" + e;
    }
  }

  async function startWebcam() {
    if (currentStream) currentStream.getTracks().forEach(t => t.stop());
    try {
      currentStream = await navigator.mediaDevices.getUserMedia({
        video: { facingMode: useFrontCamera ? "user" : "environment" },
        audio: false
      });
      video.srcObject = currentStream;
      video.onloadedmetadata = () => {
        canvas.width  = video.videoWidth;
        canvas.height = video.videoHeight;
        if (!continuousRunning) { continuousRunning = true; continuousPredict(); }
      };
    } catch(err) {
      predBox.textContent = "Camera error:\n" + err.message;
    }
  }

  function switchCamera() { useFrontCamera = !useFrontCamera; startWebcam(); }

  async function continuousPredict() {
    if (!model || !currentStream) { setTimeout(continuousPredict, 400); return; }
    canvas.width  = video.videoWidth  || 320;
    canvas.height = video.videoHeight || 240;
    canvas.getContext("2d").drawImage(video, 0, 0, canvas.width, canvas.height);
    try {
      const preds = await model.predict(canvas);
      let best = preds.reduce((a, b) => b.probability > a.probability ? b : a);
      if (!locked) {
        predBox.textContent = "Detected: Rs." + best.className +
                              "\nConfidence: " + (best.probability*100).toFixed(1) + "%";
        confFill.style.width = (best.probability*100).toFixed(1) + "%";
      }
    } catch(e) {}
    setTimeout(continuousPredict, 300);
  }

  async function captureAndSend(source) {
    if (!model || !currentStream || locked) return;
    locked = true;

    predBox.textContent = (source === "hw")
      ? "[ GPIO 13 ] Classifying note..."
      : "Classifying note...";

    if (source === "hw") {
      predBox.classList.add("flash");
      setTimeout(() => predBox.classList.remove("flash"), 600);
    }

    canvas.width  = video.videoWidth  || 320;
    canvas.height = video.videoHeight || 240;
    canvas.getContext("2d").drawImage(video, 0, 0, canvas.width, canvas.height);

    try {
      const preds = await model.predict(canvas);
      let best = preds.reduce((a, b) => b.probability > a.probability ? b : a);
      const pct   = (best.probability * 100).toFixed(1);
      const label = best.className;

      predBox.textContent = "Detected: Rs." + label + "\nConfidence: " + pct + "%";
      confFill.style.width = pct + "%";

      if (VALID_DENOMS.includes(label)) {
        const resp = await fetch("/result?label=" + encodeURIComponent(label) + "&conf=" + pct + "&source=" + source);
        if (resp.ok) {
          const data = await resp.json();
          totalEl.textContent = data.total;
          predBox.textContent = "✓ Added: Rs." + label +
                                "\nConfidence: " + pct + "%" +
                                "\nTotal: Rs." + data.total;
        }
      }
    } catch(e) {
      predBox.textContent = "Error:\n" + e;
    }
    locked = false;
  }

  function manualCapture() { captureAndSend("browser"); }

  async function pollTrigger() {
    try {
      const r    = await fetch("/trigger");
      const data = await r.json();
      if (data.fired) {
        hwInd.textContent = "⚡ GPIO 13 fired! (#" + data.count + ") — classifying...";
        hwInd.classList.add("fired");
        await captureAndSend("hw");
      } else {
        hwInd.textContent = "🔌 GPIO 13: waiting... (fired " + data.count + " time(s))";
        hwInd.classList.remove("fired");
      }
    } catch(e) {}
  }

  async function syncTotal() {
    try {
      const r = await fetch("/piggy");
      const d = await r.json();
      totalEl.textContent = d.total;
    } catch(e) {}
  }

  loadModel();
  startWebcam();
  
  setInterval(pollTrigger, 100);
  setInterval(syncTotal,   3000);
</script>
</body>
</html>
)rawliteral";

// ---------------------------------------------------------------
// LCD helpers
// ---------------------------------------------------------------
void lcdShowIdle() {
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("Smart Piggy Bank");
  lcd.setCursor(0, 1); lcd.print("Total: Rs."); lcd.print(currentAmount);
}

void lcdShowResult(int denom) {
  lcd.clear();
  lcd.setCursor(0, 0);
  if (denom > 0) { lcd.print("Added: Rs."); lcd.print(denom); }
  else           { lcd.print("Not detected"); }
  lcd.setCursor(0, 1); lcd.print("Total: Rs."); lcd.print(currentAmount);
}

int parseDenomination(const String& label) {
  if (label == "10")  return 10;
  if (label == "20")  return 20;
  if (label == "100") return 100;
  if (label == "500") return 500;
  return 0;
}

// ---------------------------------------------------------------
// HTTP Handlers
// ---------------------------------------------------------------
static esp_err_t index_handler(httpd_req_t *req) {
  httpd_resp_set_type(req, "text/html");
  return httpd_resp_send(req, (const char*)INDEX_HTML, strlen(INDEX_HTML));
}

static esp_err_t trigger_handler(httpd_req_t *req) {
  bool wasFired = triggerFired;
  if (triggerFired) triggerFired = false;
  char buf[64];
  snprintf(buf, sizeof(buf), "{\"fired\":%s,\"count\":%u}",
           wasFired ? "true" : "false", (unsigned)triggerCount);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_send(req, buf, strlen(buf));
  return ESP_OK;
}

static esp_err_t result_handler(httpd_req_t *req) {
  char query[64] = {0};
  if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No query");
    return ESP_FAIL;
  }
  char label[16]  = {0};
  char source[16] = "browser";
  char conf[10]   = "?";
  httpd_query_key_value(query, "label",  label,  sizeof(label));
  httpd_query_key_value(query, "source", source, sizeof(source));
  httpd_query_key_value(query, "conf",   conf,   sizeof(conf));

  int denom = parseDenomination(String(label));
  Serial.printf("Detected: Rs.%s | Added: Rs.%d | Total: Rs.%d\n", label, denom, currentAmount + denom);
  if (denom > 0) {
    currentAmount += denom;
    lcdShowResult(denom);

    // --- TRIGGER PIN 27 HIGH FOR 4 SECONDS ---
    digitalWrite(OUTPUT_PIN, HIGH);
    pin27TurnOffTime = millis() + 2000;
    pin27Active = true;
    
  } else {
    lcdShowResult(0);
  }

  char resp[32];
  snprintf(resp, sizeof(resp), "{\"total\":%d}", currentAmount);
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_type(req, "application/json");
  httpd_resp_send(req, resp, strlen(resp));
  return ESP_OK;
}

static esp_err_t piggy_handler(httpd_req_t *req) {
  char buf[32];
  snprintf(buf, sizeof(buf), "{\"total\":%d}", currentAmount);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_send(req, buf, strlen(buf));
  return ESP_OK;
}

// ---------------------------------------------------------------
// HTTPS Server
// ---------------------------------------------------------------
void startWebServer() {
  httpd_ssl_config_t conf = HTTPD_SSL_CONFIG_DEFAULT();
  conf.servercert     = (const uint8_t*)SERVER_CERT;
  conf.servercert_len = sizeof(SERVER_CERT);
  conf.prvtkey_pem    = (const uint8_t*)SERVER_KEY;
  conf.prvtkey_len    = sizeof(SERVER_KEY);
  conf.httpd.max_uri_handlers = 8;
  conf.httpd.stack_size       = 16384;
  conf.httpd.max_open_sockets = 4;

  if (httpd_ssl_start(&server, &conf) != ESP_OK) {
    Serial.println("[SERVER] Failed to start HTTPS server.");
    return;
  }
  httpd_uri_t uris[] = {
    { "/",        HTTP_GET, index_handler,   NULL },
    { "/trigger", HTTP_GET, trigger_handler, NULL },
    { "/result",  HTTP_GET, result_handler,  NULL },
    { "/piggy",   HTTP_GET, piggy_handler,   NULL },
  };
  for (auto& u : uris) httpd_register_uri_handler(server, &u);
  Serial.println("[SERVER] HTTPS server listening.");
}

// ---------------------------------------------------------------
// Setup
// ---------------------------------------------------------------
void setup() {
  Serial.begin(115200);

  Wire.begin(21, 22);
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("Smart Piggy Bank");
  lcd.setCursor(0, 1); lcd.print("Connecting WiFi.");

  pinMode(TRIGGER_PIN, INPUT_PULLDOWN);
  pinMode(OUTPUT_PIN, OUTPUT);          // <--- Initialize Pin 27 as Output
  digitalWrite(OUTPUT_PIN, LOW);        // Ensure it starts off

  WiFi.begin(ssid, password);
  Serial.print("Connecting Wi-Fi");
  int dots = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(500); Serial.print(".");
    lcd.setCursor(dots % 16, 1); lcd.print("."); dots++;
  }
  Serial.printf("\n[Wi-Fi] https://%s/\n", WiFi.localIP().toString().c_str());

  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("IP:");
  lcd.print(WiFi.localIP().toString().substring(0, 13));
  lcd.setCursor(0, 1); lcd.print("Total: Rs.0");
  delay(3000);

  startWebServer();
  lcdShowIdle();
}

// ---------------------------------------------------------------
// Loop — INSTANT TRIGGER WITH EDGE TRACKING (NO DEBOUNCE DELAY)
// ---------------------------------------------------------------
void loop() {
  // Non-blocking cleanup: turn off Pin 27 after 4 seconds have passed
  if (pin27Active && (millis() >= pin27TurnOffTime)) {
    digitalWrite(OUTPUT_PIN, LOW);
    pin27Active = false;
    Serial.println("[GPIO27] 4 seconds finished, setting LOW.");
  }

  bool pinNow = (digitalRead(TRIGGER_PIN) == HIGH);

  switch (trigState) {
    case IDLE:
      if (pinNow && !lastPinState) {
        triggerCount++;
        Serial.printf("[GPIO13] Trigger #%u — firing immediately\n", (unsigned)triggerCount);
        triggerFired = true;
        trigState    = FIRE;
      }
      break;

    case FIRE:
      if (!triggerFired) {
        trigState = IDLE;
      }
      break;
  }

  lastPinState = pinNow;
  delay(10);
}