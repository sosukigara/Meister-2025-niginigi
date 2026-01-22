// 動作確認済みバージョン (Once it works!)
#include <Adafruit_NeoPixel.h>
#include <Arduino.h>
#include <ESP32Servo.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <time.h>
#include <vector>

// --- UI Content ---
const char *html_main = R"rawliteral(
<!DOCTYPE html>
<html lang="ja">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0, user-scalable=no, viewport-fit=cover">
<title>Onigiri Journal</title>
<title>Onigiri Journal</title>

<style>

:root {
  --bg: #f2f2f7;
  --card-bg: #ffffff;
  --text-main: #1c1c1e;
  --text-sub: #8e8e93;
  --accent-purple: #1a7f37;
  --accent-blue: #2ea043;
  --danger: #ff3b30;
  --yt-red: #ff0000;
  --success: #2ea043;
  
  --shadow: 0 4px 12px rgba(0,0,0,0.03);
  --radius: 16px;
}

* {
  -webkit-tap-highlight-color: transparent;
  user-select: none;
  -webkit-touch-callout: none;
  box-sizing: border-box;
}

  background: var(--bg);
  color: var(--text-main);
  font-family: sans-serif;
  margin: 0; 
  padding: 0;
  height: 100dvh;
  width: 100vw;
  overflow: hidden; /* No scroll on body */
}

/* Connection Lost State */
body.offline { opacity: 0.6; pointer-events: none; }
body.offline::after {
  content: "再接続中...";
  position: fixed; top: 50%; left: 50%; transform: translate(-50%, -50%);
  background: rgba(0,0,0,0.8); color: white; padding: 12px 24px;
  border-radius: 30px; font-weight: bold; pointer-events: none; z-index: 2000;
}

/* Header */
.header { 
  margin-bottom: 12px; padding-top: 0; 
  display: flex; align-items: center; /* 縦位置揃え */
  gap: 10px; flex-wrap: wrap;
} 
.header h1 {
  font-size: clamp(1.5rem, 5vw, 1.9rem); /* Responsive Font Size */
  font-weight: 800; margin: 0; letter-spacing: -0.02em;
}
.header-actions {
  display: flex; gap: 8px;
}
.btn-icon {
  background: none; border: none; padding: 8px;
  color: var(--text-main); cursor: pointer; border-radius: 50%;
  display: flex; align-items: center; justify-content: center;
}
.btn-icon:active { background: rgba(0,0,0,0.05); }

/* Status Dot */
.conn-dot {
  width: 10px; height: 10px; background: var(--success);
  border-radius: 50%; margin-right: 8px;
  transition: background 0.3s;
}
.offline .conn-dot { background: var(--danger); }

/* Main View Container */
#view-main, #view-settings, #view-history {
  display: flex;
  flex-direction: column;
  height: 100dvh;
  padding-top: max(20px, env(safe-area-inset-top));
  padding-bottom: max(20px, env(safe-area-inset-bottom));
  padding-left: max(20px, env(safe-area-inset-left));
  padding-right: max(20px, env(safe-area-inset-right));
  box-sizing: border-box;
}

/* Cards */
.card {
  background: var(--card-bg);
  border-radius: var(--radius);
  padding: clamp(12px, 2vh, 20px);
  margin-bottom: 2vh;
  box-shadow: var(--shadow);
  overflow: hidden;
  flex-shrink: 1; /* Allow shrinking */
}

/* Monitor Card */
.card-monitor {
  display: flex; flex-direction: column;
  position: relative;
  padding-bottom: 1vh; /* Reduced padding */
  flex-grow: 1;
  justify-content: center;
}
.monitor-row { 
  display: flex; flex-direction: column; 
  align-items: center; justify-content: center;
  gap: 4px; margin-bottom: 8px; /* Reduced gap/margin */
}
.status-badge {
  background: #f2f2f7; color: var(--text-sub);
  padding: 6px 14px; border-radius: 24px;
  font-size: 0.9rem; font-weight: 700;
  transition: 0.3s;
}
.running .status-badge { background: #fee2e2; color: var(--yt-red); }

/* Time Display */
.time-big {
  font-size: clamp(2.5rem, 13vw, 4.0rem); /* Further reduced size (9/10) */
  font-weight: 800; 
  font-variant-numeric: tabular-nums; 
  letter-spacing: -2px; line-height: 1;
  opacity: 0; 
  transition: opacity 0.3s;
}
body.ready .time-big { opacity: 1; }

/* Progress Bar */
.yt-progress-container {
  width: 100%; height: 8px; 
  background: #e5e5ea; position: relative;
  border-radius: 4px; overflow: hidden;
  /* opacity: 0; removed for always visible */
}
/* .running .yt-progress-container { opacity: 1; } removed */ 

.yt-progress-fill {
  position: absolute; left: 0; top: 0; height: 100%;
  background: var(--yt-red); border-radius: 4px;
  width: 0%; 
}

/* Preset Buttons */
.card-preset h3 { margin: 0 0 10px 0; font-size: 1rem; color: var(--text-sub); text-transform: uppercase; letter-spacing: 0.05em; }
.preset-grid { display: grid; grid-template-columns: 1fr 1fr 1fr; gap: 10px; }
.preset-btn {
  background: var(--bg); border: 2px solid transparent;
  padding: 22px 4px; border-radius: 14px; /* padding increased */
  font-size: 0.95rem; font-weight: 700; color: var(--text-main);
  text-align: center; cursor: pointer; transition: 0.2s;
  white-space: nowrap; /* 折り返し防止 */
}
.preset-btn:active { transform: scale(0.98); }
.preset-btn.active {
  background: #fff; border-color: var(--accent-blue); color: var(--accent-blue);
  box-shadow: 0 4px 10px rgba(0,122,255,0.15);
}

/* Settings List */
.card-settings { padding: 18px; } 
.setting-item {
  padding: 10px 0; display: flex; flex-direction: column; 
  border-bottom: 1px solid #f2f2f7;
}
.setting-item:last-child { border-bottom: none; }
.s-header {
  display: flex; justify-content: space-between; align-items: center;
  margin-bottom: 10px;
}
.s-label { font-size: 1rem; font-weight: 700; color: var(--text-main); }
.s-val { font-size: 1.1rem; font-weight: 700; color: var(--accent-purple); font-variant-numeric: tabular-nums; }

/* Sliders */
input[type=range] {
  -webkit-appearance: none; width: 100%; height: 44px; 
  background: transparent; cursor: pointer; margin: 0;
}
input[type=range]:focus { outline: none; }
input[type=range]::-webkit-slider-runnable-track {
  width: 100%; height: 14px; 
  background: #e5e5ea; 
  border-radius: 7px;
}
#inp-str::-webkit-slider-runnable-track {
  background: linear-gradient(90deg, #e5e5ea 0%, #56d364 50%, #1a7f37 100%);
}

input[type=range]::-webkit-slider-thumb {
  -webkit-appearance: none; height: 32px; width: 32px;
  border-radius: 50%; background: #ffffff;
  border: 0.5px solid rgba(0,0,0,0.04);
  box-shadow: 0 4px 10px rgba(0,0,0,0.15);
  margin-top: -9px; transition: transform 0.1s;
}
input[type=range]:active::-webkit-slider-thumb { transform: scale(1.1); background: #f2f2f7; }

/* Count Buttons */
.chk-group { display: flex; gap: 10px; justify-content: flex-end; }
.chk-btn {
  width: 48px; height: 48px; border-radius: 50%;
  background: #f2f2f7; color: var(--text-sub);
  display: flex; align-items: center; justify-content: center;
  font-size: 1.2rem; font-weight: 700; cursor: pointer; transition: 0.2s;
}
.chk-btn.active {
  background: var(--accent-blue); color: white;
  box-shadow: 0 4px 10px rgba(0,0,0,0.2); transform: scale(1.05);
}

/* History List */
.history-row {
  display: flex; justify-content: space-between; align-items: center;
  padding: 12px 0; border-bottom: 1px solid #f2f2f7;
}
.history-row:last-child { border-bottom: none; }
.h-info { display: flex; flex-direction: column; gap: 2px; }
.h-preset { font-weight: 700; font-size: 1rem; color: var(--text-main); }
.h-detail { font-size: 0.85rem; color: var(--text-sub); }
.h-time { font-family: monospace; font-size: 0.9rem; color: var(--accent-purple); font-weight: 600; }

/* Bottom Bar */
.bottom-bar {
  margin-top: auto; /* Push to bottom */
  margin-bottom: 24px; /* Increased bottom spacing */
  display: flex; gap: 12px;
  filter: drop-shadow(0 10px 20px rgba(0,0,0,0.1));
  flex-shrink: 0; /* Keep size */
}
.action-btn {
  flex: 1; height: 68px; border-radius: 34px; border: none;
  font-size: 1.2rem; font-weight: 800;
  display: flex; align-items: center; justify-content: center; gap: 8px;
  cursor: pointer; box-shadow: inset 0 1px 1px rgba(255,255,255,0.4);
  transition: transform 0.1s;
}
.action-btn:active { transform: scale(0.98); }
.btn-start { background: var(--accent-blue); color: white; }
.btn-stop { background: var(--danger); color: white; display: none; }
.running .btn-start { display: none; }
.running .btn-stop { display: flex; }

/* Toggle Switch */
.switch { position: relative; display: inline-block; width: 50px; height: 28px; }
.switch input { opacity: 0; width: 0; height: 0; }
.slider {
  position: absolute; cursor: pointer; top: 0; left: 0; right: 0; bottom: 0;
  background-color: #e5e5ea; transition: .4s; border-radius: 28px;
}
.slider:before {
  position: absolute; content: ""; height: 20px; width: 20px;
  left: 4px; bottom: 4px; background-color: white; transition: .4s; border-radius: 50%;
  box-shadow: 0 2px 4px rgba(0,0,0,0.2);
}
input:checked + .slider { background-color: var(--accent-blue); }
input:checked + .slider:before { transform: translateX(22px); }

/* Completion Modal */
.completion-modal {
  position: fixed; top: 0; left: 0; right: 0; bottom: 0;
  background: rgba(0,0,0,0.7); /* 少し薄暗く */
  backdrop-filter: blur(4px);   /* 背景ぼかしでリッチに */
  -webkit-backdrop-filter: blur(4px);
  display: none; align-items: center; justify-content: center;
  z-index: 1000;
  animation: fadeIn 0.3s;
}
.completion-modal.show { display: flex; }

.completion-content {
  background: var(--card-bg);
  border-radius: 28px;
  padding: 32px 28px; /* パディング調整 */
  text-align: center;
  max-width: 85%; 
  width: min(340px, 90vw); /* Prevent overflow */
  box-shadow: 0 20px 40px rgba(0,0,0,0.2);
  animation: scaleIn 0.3s cubic-bezier(0.34, 1.56, 0.64, 1);
}

.completion-title {
  font-size: 1.6rem; /* 少し控えめに */
  font-weight: 800;
  color: var(--text-main);
  margin-bottom: 24px; /* 間隔を広げる */
}

.completion-details {
  text-align: left;
  background: var(--bg);
  padding: 16px 20px;
  border-radius: 16px;
  margin-bottom: 24px;
}

.detail-row {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding: 10px 0;
  border-bottom: 1px solid rgba(0,0,0,0.05);
}
.detail-row:last-child { border-bottom: none; }

.detail-label {
  font-size: 0.9rem;
  color: var(--text-sub);
  font-weight: 600;
}

.detail-value {
  font-size: 1.0rem;
  color: var(--text-main);
  font-weight: 700;
}

.completion-btn {
  background: var(--text-main); /* 黒ボタンで引き締める */
  color: white;
  border: none;
  padding: 16px 32px;
  border-radius: 30px;
  font-size: 1.0rem;
  font-weight: 700;
  cursor: pointer;
  transition: transform 0.2s;
  width: 100%;
}
.completion-btn:active { transform: scale(0.96); opacity: 0.9; }

@keyframes fadeIn {
  from { opacity: 0; }
  to { opacity: 1; }
}
@keyframes scaleIn {
  from { transform: scale(0.9); opacity: 0; }
  to { transform: scale(1); opacity: 1; }
}

/* Custom Modal & Toast */
.modal-overlay {
  position: fixed; top: 0; left: 0; right: 0; bottom: 0;
  background: rgba(0,0,0,0.6); backdrop-filter: blur(2px);
  display: flex; align-items: center; justify-content: center;
  z-index: 3000; opacity: 0; pointer-events: none; transition: 0.2s;
}
.modal-overlay.active { opacity: 1; pointer-events: auto; }
.modal-box {
  background: var(--card-bg); width: 85%; max-width: 320px;
  border-radius: 20px; padding: 24px; text-align: center;
  box-shadow: 0 10px 30px rgba(0,0,0,0.2);
  transform: scale(0.95); transition: 0.2s;
}
.modal-overlay.active .modal-box { transform: scale(1); }
.modal-title { font-weight: 800; font-size: 1.1rem; margin-bottom: 12px; }
.modal-input {
  width: 100%; padding: 12px; border: 1px solid #ddd; border-radius: 12px;
  font-size: 1rem; margin-bottom: 20px; box-sizing: border-box;
}
.modal-actions { display: flex; gap: 10px; justify-content: center; }
.modal-btn {
  flex: 1; padding: 12px; border-radius: 12px; border: none;
  font-weight: 700; cursor: pointer; font-size: 0.95rem;
}
.btn-cancel { background: #f2f2f7; color: var(--text-main); }
.btn-ok { background: var(--accent-blue); color: white; }
.btn-danger { background: var(--danger); color: white; }

.toast {
  position: fixed; bottom: 100px; left: 50%; transform: translateX(-50%) translateY(20px);
  background: rgba(0,0,0,0.85); color: white; padding: 10px 20px;
  border-radius: 30px; font-weight: 600; font-size: 0.9rem;
  opacity: 0; transition: 0.3s; pointer-events: none; z-index: 4000;
  white-space: nowrap; box-shadow: 0 4px 10px rgba(0,0,0,0.2);
}
.toast.show { opacity: 1; transform: translateX(-50%) translateY(0); }
  to { opacity: 1; }
}

@keyframes scaleIn {
  from { transform: scale(0.9); opacity: 1; }
  to { transform: scale(1); opacity: 1; }
}

/* Responsive */
@media (max-height: 750px) {
  /* body padding is handled by main styles now */
  .header { margin-bottom: 10px; }
  /* h1 font size handled by clamp */
  .card { margin-bottom: 10px; } /* padding handled by clamp */
  .time-big { font-size: 3.5rem; }
  .status-badge { font-size: 0.8rem; padding: 4px 10px; }
  .action-btn { height: 56px; font-size: 1.1rem; }
}
</style>
</head>
<body>

<!-- VIEW: MAIN -->
<div id="view-main">
  <div class="header">
    <div style="display:flex; align-items:center; gap:8px;">
      <div class="conn-dot" id="conn-dot"></div>
      <h1 style="line-height:1; margin:0;">にぎにぎ</h1>
      <span style="font-size:0.75rem; color:var(--text-sub); font-family:monospace; padding-top:4px;">v1.60</span>
    </div>

    <!-- Sensor Control -->
    <div style="display:flex; align-items:center; gap:6px; margin-left:auto; margin-right:4px; background:#fff; padding:6px 12px; border-radius:30px; border:1px solid #eee;">
      <span style="font-size:0.8rem; font-weight:bold; color:var(--text-sub);">センサー</span>
      <label class="switch" style="transform:scale(0.7); margin:0;">
        <input type="checkbox" id="chk-sensor" onchange="toggleSensor(this)">
        <span class="slider round"></span>
      </label>
      <span id="sensor-lbl" style="font-size:0.8rem; font-weight:700; color:var(--text-sub); min-width:24px;">OFF</span>
    </div>

    <div class="header-actions">
        <!-- History Icon -->
        <button class="btn-icon" onclick="showHistory()" style="font-weight:700; padding:8px 12px; border:1px solid #ddd; border-radius:12px; background:#f5f5f5; height:36px; display:flex; align-items:center;">
          履歴
        </button>
        <!-- Settings Icon -->
        <button class="btn-icon" onclick="showSettings()" style="font-weight:700; padding:8px 12px; border:1px solid #ddd; border-radius:12px; background:#f5f5f5; height:36px; display:flex; align-items:center;">
          設定
        </button>
    </div>
  </div>

  <!-- 1. Monitor -->
  <div class="card card-monitor">
    <div class="monitor-row">
      <div class="status-badge" id="status-badge">待機中</div>
      <div class="time-big" id="time-display">--:--</div>
    </div>
    <div class="yt-progress-container">
      <div class="yt-progress-fill" id="yt-fill"></div>
    </div>
  </div>

  <!-- 2. Preset -->
  <div class="card card-preset">
    <h3>プリセット</h3>
    <div class="preset-grid" style="grid-template-columns: 1fr 1fr 1fr;">
      <div class="preset-btn" onclick="setPreset('soft',this)" data-name="やわらか">やわらか</div>
      <div class="preset-btn active" onclick="setPreset('normal',this)" data-name="ふつう">ふつう</div>
      <div class="preset-btn" onclick="setPreset('kosen',this)" data-name="高専生用">高専生用</div>
    </div>
  </div>

  <!-- 3. Settings (Basic) -->
  <div class="card card-settings">
    <div class="setting-item">
      <div class="s-header">
        <span class="s-label">握りの強さ</span>
        <span class="s-val" id="str-disp">50%</span>
      </div>
      <input type="range" id="inp-str" min="0" max="100" value="50" oninput="updVal('str-disp', this.value, '%')" onchange="saveStr(this.value)">
    </div>
    
    <div class="setting-item" style="flex-direction:row; align-items:center; justify-content:space-between; padding:16px 0;">
      <span class="s-label">回数</span>
      <div class="chk-group">
        <div class="chk-btn" onclick="setCount(1,this)">1</div>
        <div class="chk-btn" onclick="setCount(2,this)">2</div>
        <div class="chk-btn" onclick="setCount(3,this)">3</div>
        <div class="chk-btn" onclick="setCount(4,this)">4</div>
        <div class="chk-btn" onclick="setCount(5,this)">5</div>
      </div>
    </div>
  </div>

  <!-- Bottom Actions -->
  <div class="bottom-bar">
    <button class="action-btn btn-start" onclick="start()">
      開始
    </button>
    <button class="action-btn btn-stop" onclick="stop()">
      停止
    </button>
  </div>
</div>

<!-- VIEW: SETTINGS -->
<div id="view-settings" style="display:none; padding-bottom:40px;">
  <div class="header" style="display:flex; align-items:center; gap:10px;">
    <button onclick="showMain()" style="background:none; border:none; color:var(--text-main); cursor:pointer; padding:0; font-size:2rem; font-weight:bold;">
      ←
    </button>
    <h1>詳細設定</h1>
  </div>
  
  <div class="card card-settings">
    <div class="setting-item">
      <span class="s-label">システム情報</span>
      <div style="margin-top:8px; font-size:0.9rem; color:var(--text-sub);">
        <div>Version: <span style="font-family:monospace;">1.59</span></div>
        <div>Build: <span style="font-family:monospace;">{{BUILD_TIME}}</span></div>
        <div>IP: <span style="font-family:monospace;" id="ip-disp">...</span></div>
      </div>
    </div>
    
    <div class="setting-item" style="flex-direction:row; align-items:center; justify-content:space-between;">
      <span class="s-label">保持時間 (秒)</span>
      <input type="number" id="inp-hold" value="0.5" step="0.1" style="width:80px; padding:12px; border-radius:12px; border:1px solid #ddd; text-align:center; font-size:1.1rem; font-weight:700;" onchange="saveHold(this.value)">
    </div>

    <!-- 手動調整 -->
    <div class="setting-item">
      <div class="s-header">
        <span class="s-label">全サーボ同時調整 (90-270度)</span>
        <span class="s-val" id="all-servo-val">270°</span>
      </div>
      <input type="range" min="90" max="270" value="270" step="1" oninput="setAllServos(this.value)" style="width:100%;">
    </div>

    <div class="setting-item">
      <div class="s-header">
        <span class="s-label">サーボ1 (GPIO 18)</span>
        <span class="s-val" id="servo1-val">270°</span>
      </div>
      <input type="range" id="inp-servo-1" min="90" max="270" value="270" step="1" oninput="setServo(1, this.value)" style="width:100%;">
    </div>

    <div class="setting-item">
      <div class="s-header">
        <span class="s-label">サーボ2 (GPIO 26)</span>
        <span class="s-val" id="servo2-val">270°</span>
      </div>
      <input type="range" id="inp-servo-2" min="90" max="270" value="270" step="1" oninput="setServo(2, this.value)" style="width:100%;">
    </div>

    <div class="setting-item">
      <div class="s-header">
        <span class="s-label">サーボ3 (GPIO 27)</span>
        <span class="s-val" id="servo3-val">270°</span>
      </div>
      <input type="range" id="inp-servo-3" min="90" max="270" value="270" step="1" oninput="setServo(3, this.value)" style="width:100%;">
    </div>

    <div class="setting-item" style="flex-direction:row; align-items:center; justify-content:space-between;">
      <span class="s-label">到達時間 (秒)</span>
      <input type="number" id="inp-reach" value="0.5" step="0.1" style="width:80px; padding:12px; border-radius:12px; border:1px solid #ddd; text-align:center; font-size:1.1rem; font-weight:700;" onchange="saveReach(this.value)">
    </div>

    </div>

    <!-- LED Control -->
    <div class="setting-item">
      <div class="s-header">
        <span class="s-label">LED制御 (Pin 13)</span>
      </div>
      <div style="display:flex; justify-content:space-between; align-items:center; margin-bottom:10px;">
        <span style="font-size:0.9rem; color:var(--text-sub);">手動モード</span>
        <label class="switch" style="transform:scale(0.8);">
          <input type="checkbox" id="chk-led-manual" onchange="toggleLedMode(this)">
          <span class="slider round"></span>
        </label>
      </div>
      <div style="display:flex; gap:10px; align-items:center;">
        <input type="color" id="inp-led-color" value="#ff0000" style="height:40px; border:none; background:none; cursor:pointer;" onchange="setLedColorHex(this.value)">
        <div style="display:flex; gap:8px;">
          <button onclick="setLedColor(255,0,0)" style="background:#ffdddd; color:#ff0000; border:none; padding:8px 12px; border-radius:8px; font-weight:bold;">赤</button>
          <button onclick="setLedColor(0,255,0)" style="background:#ddffdd; color:#00aa00; border:none; padding:8px 12px; border-radius:8px; font-weight:bold;">緑</button>
          <button onclick="setLedColor(0,0,255)" style="background:#ddddff; color:#0000ff; border:none; padding:8px 12px; border-radius:8px; font-weight:bold;">青</button>
          <button onclick="setLedColor(0,0,0)" style="background:#eee; color:#333; border:none; padding:8px 12px; border-radius:8px; font-weight:bold;">OFF</button>
        </div>
      </div>
      <div style="margin-top:12px;">
        <div class="s-header">
           <span class="s-label">LED使用数</span>
           <span class="s-val" id="led-cnt-disp">35</span>
        </div>
        <input type="range" id="inp-led-cnt" min="1" max="35" value="35" step="1" oninput="document.getElementById('led-cnt-disp').innerText=this.value; fetch('/api/settings?led_cnt='+this.value)">
      </div>
    </div>
    
    <!-- 個別サーボ位置補正 -->
    <div class="setting-item">
      <div class="s-header">
        <span class="s-label">サーボ1 位置補正</span>
        <span class="s-val" id="servo1-offset-disp">0</span>
      </div>
      <input type="range" min="-90" max="90" value="0" step="1" id="inp-servo1-offset" oninput="updateServoOffset(1, this.value)" style="width:100%;">
    </div>
    
    <div class="setting-item">
      <div class="s-header">
        <span class="s-label">サーボ2 位置補正</span>
        <span class="s-val" id="servo2-offset-disp">0</span>
      </div>
      <input type="range" min="-90" max="90" value="0" step="1" id="inp-servo2-offset" oninput="updateServoOffset(2, this.value)" style="width:100%;">
    </div>
    
    <div class="setting-item">
      <div class="s-header">
        <span class="s-label">サーボ3 位置補正</span>
        <span class="s-val" id="servo3-offset-disp">0</span>
      </div>
      <input type="range" min="-90" max="90" value="0" step="1" id="inp-servo3-offset" oninput="updateServoOffset(3, this.value)" style="width:100%;">
    </div>

    <!-- 角度制限 -->
    <div class="setting-item">
      <div class="s-header">
        <span class="s-label">角度制限 (Min / Max)</span>
      </div>
      
      <div style="margin-top:10px;">
        <div style="font-size:0.9rem; margin-bottom:4px;">サーボ1</div>
        <div style="display:flex; gap:10px;">
          <input type="number" id="min-angle-1" min="0" max="270" step="1" style="width:100%; padding:8px; border-radius:8px; border:1px solid #ddd; text-align:center;" onchange="updateServoLimit(1)">
          <input type="number" id="max-angle-1" min="0" max="270" step="1" style="width:100%; padding:8px; border-radius:8px; border:1px solid #ddd; text-align:center;" onchange="updateServoLimit(1)">
        </div>
      </div>
      <div style="margin-top:10px;">
        <div style="font-size:0.9rem; margin-bottom:4px;">サーボ2</div>
        <div style="display:flex; gap:10px;">
          <input type="number" id="min-angle-2" min="0" max="270" step="1" style="width:100%; padding:8px; border-radius:8px; border:1px solid #ddd; text-align:center;" onchange="updateServoLimit(2)">
          <input type="number" id="max-angle-2" min="0" max="270" step="1" style="width:100%; padding:8px; border-radius:8px; border:1px solid #ddd; text-align:center;" onchange="updateServoLimit(2)">
        </div>
      </div>
      <div style="margin-top:10px;">
        <div style="font-size:0.9rem; margin-bottom:4px;">サーボ3</div>
        <div style="display:flex; gap:10px;">
          <input type="number" id="min-angle-3" min="0" max="270" step="1" style="width:100%; padding:8px; border-radius:8px; border:1px solid #ddd; text-align:center;" onchange="updateServoLimit(3)">
          <input type="number" id="max-angle-3" min="0" max="270" step="1" style="width:100%; padding:8px; border-radius:8px; border:1px solid #ddd; text-align:center;" onchange="updateServoLimit(3)">
        </div>
      </div>
    </div>
    
    <!-- センサー距離表示 -->
    <div class="setting-item">
      <span class="s-label">センサー距離</span>
      <div style="font-size:1.5rem; font-weight:700; color:var(--accent-purple); margin-top:8px;">
        <span id="distance-disp">--</span> cm
      </div>
    </div>
    
    <div class="setting-item">
      <div class="s-header">
        <span class="s-label">センサーしきい値 (cm)</span>
        <span class="s-val" id="sth-disp">10.0</span>
      </div>
      <input type="range" min="1" max="50" step="0.5" value="10" id="inp-sth" oninput="saveSth(this.value)" style="width:100%;">
    </div>
    <!-- マイプリセット -->
    <div class="setting-item" style="border-top: 4px solid #f2f2f7; margin-top:20px; padding-top:20px;">
      <div class="s-header">
        <span class="s-label">マイプリセット (保存/読込)</span>
        <button onclick="saveMyPreset()" style="padding:6px 12px; border-radius:16px; background:var(--accent-blue); color:white; border:none; font-weight:bold; font-size:0.85rem;">保存</button>
      </div>
      <div id="my-preset-list" style="display:flex; flex-direction:column; gap:10px;">
        <!-- JSで生成 -->
      </div>
    </div>
  </div>
</div>

<!-- VIEW: HISTORY -->
<div id="view-history" style="display:none; padding-bottom:40px;">
  <div class="header" style="display:flex; align-items:center; gap:10px;">
    <button onclick="showMain()" style="background:none; border:none; color:var(--text-main); cursor:pointer; padding:0; font-size:2rem; font-weight:bold;">
       ←
    </button>
    <h1>履歴</h1>
  </div>
  <div id="history-list" class="card" style="padding:10px 20px;">
    <div style="text-align:center; padding:20px; color:#888;">読み込み中...</div>
  </div>
</div>

<!-- Completion Modal -->
<div class="completion-modal" id="completion-modal">
  <div class="completion-content">
    <div class="completion-title">完成！</div>
    <div class="completion-details">
      <div class="detail-row">
        <span class="detail-label">プリセット</span>
        <span class="detail-value" id="detail-preset">-</span>
      </div>
      <div class="detail-row">
        <span class="detail-label">強さ</span>
        <span class="detail-value" id="detail-strength">-</span>
      </div>
      <div class="detail-row">
        <span class="detail-label">回数</span>
        <span class="detail-value" id="detail-count">-</span>
      </div>
    </div>
    <button class="completion-btn" onclick="closeCompletionModal()">OK</button>
  </div>
</div>

<!-- Custom Modals -->
<div class="modal-overlay" id="prompt-modal">
  <div class="modal-box">
    <div class="modal-title" id="prompt-msg">入力</div>
    <input type="text" class="modal-input" id="prompt-input" autocomplete="off">
    <div class="modal-actions">
      <button class="modal-btn btn-cancel" onclick="closePrompt(null)">キャンセル</button>
      <button class="modal-btn btn-ok" onclick="closePrompt(true)">OK</button>
    </div>
  </div>
</div>

<div class="modal-overlay" id="confirm-modal">
  <div class="modal-box">
    <div class="modal-title" id="confirm-msg">確認</div>
    <div class="modal-actions">
      <button class="modal-btn btn-cancel" onclick="closeConfirm(false)">いいえ</button>
      <button class="modal-btn btn-ok" id="confirm-ok-btn" onclick="closeConfirm(true)">はい</button>
    </div>
  </div>
</div>

<div class="toast" id="toast">保存しました</div>

<script>
// --- Custom Modal Logic ---
let promptResolve = null;
function showPrompt(msg, defaultVal = "") {
  return new Promise(resolve => {
    document.getElementById('prompt-msg').innerText = msg;
    const inp = document.getElementById('prompt-input');
    inp.value = defaultVal;
    promptResolve = resolve;
    document.getElementById('prompt-modal').classList.add('active');
    setTimeout(() => inp.focus(), 100);
  });
}
function closePrompt(isOk) {
  document.getElementById('prompt-modal').classList.remove('active');
  const val = document.getElementById('prompt-input').value;
  if (promptResolve) promptResolve(isOk ? val : null);
  promptResolve = null;
}

let confirmResolve = null;
function showConfirm(msg, isDanger = false) {
  return new Promise(resolve => {
    document.getElementById('confirm-msg').innerText = msg;
    const btn = document.getElementById('confirm-ok-btn');
    if(isDanger) { btn.className = "modal-btn btn-danger"; } 
    else { btn.className = "modal-btn btn-ok"; }
    
    confirmResolve = resolve;
    document.getElementById('confirm-modal').classList.add('active');
  });
}
function closeConfirm(isOk) {
  document.getElementById('confirm-modal').classList.remove('active');
  if (confirmResolve) confirmResolve(isOk);
  confirmResolve = null;
}

function showToast(msg) {
  const el = document.getElementById('toast');
  el.innerText = msg;
  el.classList.add('show');
  setTimeout(() => el.classList.remove('show'), 2000);
}

// Global Vars
let isRunning = false;
let isStarting = false;
let isManualStop = false;
let hasSessionFinished = false; // Debounce flag -> true when finished, reset on start
let sessionStartTime = 0;
let sessionTotalDur = 0; 
let tgtCount = 3;
let currentPresetName = "Custom";
let lastStatus = "IDLE";
let lastStartAction = 0; 
let debounceTimer = null; // レスポンス改善用
 

// 完了画面用
let runPreset = "";
let runStrength = 0;
let runCount = 0;

document.getElementById('ip-disp').innerText = window.location.hostname;

function fetchSettings() {
  fetch('/api/settings?load=1')
    .then(r=>r.json())
    .then(d=>{
      if(d.hold) document.getElementById('inp-hold').value = d.hold;
      if(d.reach) document.getElementById('inp-reach').value = d.reach;
      if(d.pin13 !== undefined) document.getElementById('chk-pin13').checked = (d.pin13 == 1);
      if(d.sensor !== undefined) {
        document.getElementById('chk-sensor').checked = (d.sensor == 1);
        updSensorLbl(d.sensor == 1);
      }
      if(d.sth) {
        document.getElementById('inp-sth').value = d.sth;
        document.getElementById('sth-disp').innerText = d.sth;
      }
      
      updTimeDisp();
      renderMyPresets(); // Load presets
      setTimeout(() => document.body.classList.add('ready'), 50);
    }).catch(e => {
        updTimeDisp();
        document.body.classList.add('ready');
    });
}
fetchSettings();

function saveHold(v) { fetch('/api/settings?hold=' + v).then(()=>updTimeDisp()); }
function saveReach(v) { fetch('/api/settings?reach=' + v).then(()=>updTimeDisp()); }
function togglePin13(el) { fetch('/api/pin13?val=' + (el.checked ? 1 : 0)); }
function toggleSensor(el) { 
  updSensorLbl(el.checked);
  fetch('/api/sensor_mode?val=' + (el.checked ? 1 : 0)); 
}
function saveSth(v) {
  document.getElementById('sth-disp').innerText = v;
  fetch('/api/settings?sth=' + v);
}
function saveStr(v) { fetch('/api/settings?str=' + v); }
function updSensorLbl(isOn) {
  const el = document.getElementById('sensor-lbl');
  if(isOn) { el.innerText="ON"; el.style.color="var(--accent-blue)"; }
  else { el.innerText="OFF"; el.style.color="var(--text-sub)"; }
}

function manualServo(pct) {
  document.getElementById('man-val').innerText = pct + "%";
  fetch('/api/manual?val=' + pct);
}

function toggleLedMode(el) {
  const mode = el.checked ? "manual" : "auto";
  fetch('/api/led_mode?mode=' + mode);
}
function setLedColorHex(hex) {
  fetch('/api/led?color=' + encodeURIComponent(hex));
  document.getElementById('chk-led-manual').checked = true;
}
function setLedColor(r,g,b) {
  fetch(`/api/led?r=${r}&g=${g}&b=${b}`);
  document.getElementById('chk-led-manual').checked = true;
  // カラーピッカーの値も更新（近似）
  const hex = "#" + ((1 << 24) + (r << 16) + (g << 8) + b).toString(16).slice(1);
  document.getElementById('inp-led-color').value = hex;
}

function updVal(id, v, unit) { document.getElementById(id).innerText = v + unit; }
function fmtTime(s) {
  let min = Math.floor(s / 60);
  let sec = Math.floor(s % 60);
  return min + ":" + (sec < 10 ? "0" : "") + sec;
}

function updTimeDisp() {
  if (isRunning) return; 
  const h = parseFloat(document.getElementById('inp-hold').value) || 0.5;
  const r = parseFloat(document.getElementById('inp-reach').value) || 0.5;
  // 理論値(WAIT除外) + マージン(0.3s init) + サイクルオーバーヘッド(0.4s)
  // Backend: (targetCount * (reach*2 + hold + 0.4)) + 0.4
  const total = (tgtCount * ((r * 2) + h + 0.4)) + 0.4;
  document.getElementById('time-display').innerText = fmtTime(Math.ceil(total));
  // sessionTotalDur = total; // 修正
}

function setCount(n, el) {
  tgtCount = n;
  document.querySelectorAll('.chk-btn').forEach(b => b.classList.remove('active'));
  el.classList.add('active');
  if(!isRunning) updTimeDisp();
}

function setPreset(mode, el) {
  document.querySelectorAll('.preset-btn').forEach(b => b.classList.remove('active'));
  el.classList.add('active');
  currentPresetName = el.getAttribute('data-name');
  
  const s = document.getElementById('inp-str');
  let count = 3;

  if(mode==='soft') { 
    s.value=30; 
    count = 2;
  }
  if(mode==='normal') { 
    s.value=50; 
    count = 3;
  }
  if(mode==='kosen') { 
    s.value=90; 
    count = 5;
  }
  
  updVal('str-disp', s.value, '%');

  tgtCount = count;
  document.querySelectorAll('.chk-btn').forEach(b => {
    if(parseInt(b.innerText) === count) {
      b.classList.add('active');
    } else {
      b.classList.remove('active');
    }
  });

  // Sync params with debounce
  if (debounceTimer) clearTimeout(debounceTimer);
  debounceTimer = setTimeout(() => {
    fetch(`/api/settings?str=${s.value}&cnt=${count}`).catch(()=>{});
  }, 500);

  if(!isRunning) updTimeDisp();
}

function showSettings() {
  document.getElementById('view-main').style.display = 'none';
  document.getElementById('view-settings').style.display = 'block';
  document.getElementById('view-history').style.display = 'none';
}
function showHistory() {
  document.getElementById('view-main').style.display = 'none';
  document.getElementById('view-settings').style.display = 'none';
  document.getElementById('view-history').style.display = 'block';
  fetchHistory();
}
function showMain() {
  document.getElementById('view-settings').style.display = 'none';
  document.getElementById('view-history').style.display = 'none';
  document.getElementById('view-main').style.display = 'block';
}

function fetchHistory() {
    const list = document.getElementById('history-list');
    fetch('/api/history')
        .then(r => r.json())
        .then(data => {
            list.innerHTML = "";
            if(data.length === 0) {
                list.innerHTML = "<div style='padding:20px; text-align:center; color:#888;'>履歴はありません</div>";
                return;
            }
            // 新しい順に表示
            data.reverse().forEach(item => {
                const row = document.createElement('div');
                row.className = 'history-row';
                row.innerHTML = `
                  <div class="h-info">
                     <span class="h-preset">${item.preset}</span>
                     <div class="h-detail">強さ:${item.strength}% / ${item.count}回</div>
                  </div>
                  <div class="h-time">${item.time}</div>
                `;
                list.appendChild(row);
            });
        })
        .catch(e => {
            list.innerHTML = "<div style='color:red; text-align:center;'>読み込みエラー</div>";
        });
}

function start() {
  const s = document.getElementById('inp-str').value;
  
  isManualStop = false;
  isRunning = true;
  isStarting = true; // Flag to prevent race condition
  document.body.classList.add('running');
  document.getElementById('status-badge').innerText = "準備中..."; 
  
  lastStartAction = Date.now();
  sessionStartTime = lastStartAction;
  
  runPreset = currentPresetName;
  runStrength = s;
  runCount = tgtCount;

  const h = parseFloat(document.getElementById('inp-hold').value) || 0.5;
  const r = parseFloat(document.getElementById('inp-reach').value) || 0.5;
  // 理論値(WAIT除外) + マージン
  sessionTotalDur = (tgtCount * ((r * 2) + h)) + 0.5;
  if(sessionTotalDur < 1) sessionTotalDur = 1;

  // プリセット名も送信
  // プリセット名も送信
  fetch(`/api/start?str=${s}&cnt=${tgtCount}&preset=${encodeURIComponent(runPreset)}`)
    .then(() => setTimeout(() => isStarting = false, 5000)) // タイムアウト延長
    .catch(()=>{ isStarting = false; });
}

function stop() { 
  isManualStop = true;
  fetch('/api/stop').catch(()=>{}); 
}

function setOnline(isOnline) {
  if(isOnline) {
    document.body.classList.remove('offline');
  } else {
    document.body.classList.add('offline');
  }
}

// --- SMOOTH ANIMATION LOOP ---
function animateLoop() {
  if (isRunning && sessionTotalDur > 0) {
    const now = Date.now();
    const elapsedSec = (now - sessionStartTime) / 1000;
    const remaining = Math.max(0, sessionTotalDur - elapsedSec);
    
    document.getElementById('time-display').innerText = fmtTime(Math.ceil(remaining));

    let pct = (elapsedSec / sessionTotalDur) * 100;
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    document.getElementById('yt-fill').style.width = pct + "%";
    
    // 残り時間が0になったら即座に完了処理 (UX優先)
    if (remaining <= 0 && !isManualStop) { 
      finishSession(); 
    }
  }
  requestAnimationFrame(animateLoop);
}
requestAnimationFrame(animateLoop);

// --- SYNC WITH SERVER (Adaptive Polling) ---
function syncStatus() {
  fetch('/api/status')
    .then(r => r.json())
    .then(d => {
      setOnline(true);
      
      if(d.state !== 'IDLE') {
        isStarting = false; // 状態遷移を確認したらフラグ解除
        
        // Reset finished flag if we are running again (and not FINISHED_BLINK)
        if (d.state !== 'FINISHED_BLINK') {
            hasSessionFinished = false; 
        }

        if (!isRunning) {
          isRunning = true;
          document.body.classList.add('running');
          sessionStartTime = Date.now() - (d.elap || 0);
        }
        // サーバーからの所要時間で更新
        if(d.dur && d.dur > 0) sessionTotalDur = d.dur;
        
        // 詳細情報の同期 (完了画面用)
        if(d.preset) runPreset = d.preset;
        if(d.str) runStrength = d.str;
        if(d.total) runCount = d.total;

        let txt = "動作中";
        if(d.state === 'PREPARE_SQUEEZE') txt = "準備中...";
        if(d.state === 'SQUEEZING') txt = "握り中";
        if(d.state === 'HOLDING') txt = "保持中";
        if(d.state === 'RELEASING') txt = "解放中";
        document.getElementById('status-badge').innerText = `${txt} (${d.cycle+1}/${d.total})`;

        sessionTotalDur = d.dur || 1; 
        const estimatedStart = Date.now() - (d.elap || 0);
        if (Math.abs(estimatedStart - sessionStartTime) > 500) {
           sessionStartTime = estimatedStart;
        }

      } else {
        // IDLEに戻った = 動作完了
        // FINISHED_BLINK状態も "IDLEではない" のでここには来ない = 完了画面は出ない
        // FINISHED_BLINK -> IDLE に遷移した瞬間にここに来る
        if (isRunning && !isStarting) {
          finishSession();
        }
      }
      lastStatus = d.state;
    })
    .catch(e => {
      setOnline(false);
    })
    .finally(() => {
        // 動作中は高速ポーリング(200ms), 待機中は低速(1000ms)
        const nextInterval = (isRunning || isStarting) ? 200 : 1000;
        setTimeout(syncStatus, nextInterval);
    });
}
// Start Polling
syncStatus();

function finishSession() {
  if (!isRunning) return; // 既に完了済みなら何もしない
  
  // Always clean up UI state
  isRunning = false;
  document.body.classList.remove('running');
  document.getElementById('status-badge').innerText = "待機中";
  document.getElementById('yt-fill').style.width = '0%';
  updTimeDisp(); 
  
  if (hasSessionFinished) return; // Prevent double modal
  hasSessionFinished = true;

  if (!isManualStop) {
    showCompletionModal();
  }
  isManualStop = false;
}

function showCompletionModal() {
  const modal = document.getElementById('completion-modal');
  document.getElementById('detail-preset').innerText = runPreset;
  document.getElementById('detail-strength').innerText = runStrength + '%';
  document.getElementById('detail-count').innerText = runCount + '回';
  modal.classList.add('show');
}

function closeCompletionModal() {
  document.getElementById('completion-modal').classList.remove('show');
}

function setAllServos(angle) {
  document.getElementById('all-servo-val').innerText = angle + '°';
  for(let i=1; i<=3; i++) {
    document.getElementById('servo'+i+'-val').innerText = angle + '°';
    const el = document.getElementById('inp-servo-'+i);
    if(el) el.value = angle;
  }
  fetch(`/api/servo_all?angle=${angle}`);
}

function setServo(servoNum, angle) {
  document.getElementById(`servo${servoNum}-val`).innerText = angle + '°';
  fetch(`/api/servo_individual?servo=${servoNum}&angle=${angle}`);
}

// 個別サーボ位置補正
function updateServoOffset(servoNum, value) {
  document.getElementById(`servo${servoNum}-offset-disp`).innerText = value;
  fetch(`/api/servo_offset?servo=${servoNum}&value=${value}`)
    .then(r => r.text())
    .then(d => console.log(`Servo ${servoNum} offset:`, value));
}

// 初期化時に個別サーボオフセット読み込み
function fetchServoOffsets() {
  for (let i = 1; i <= 3; i++) {
    fetch(`/api/servo_offset?servo=${i}`)
      .then(r => r.json())
      .then(data => {
        const offset = data.offset || 0;
        document.getElementById(`inp-servo${i}-offset`).value = offset;
        document.getElementById(`servo${i}-offset-disp`).innerText = offset;
      })
      .catch(e => console.error(`Failed to fetch servo ${i} offset:`, e));
  }
}

// --- My Presets (LocalStorage) ---
function saveMyPreset() {
  const defaultName = "設定 " + (new Date().toLocaleString());
  showPrompt("設定名を入力してください", defaultName).then(name => {
    if(!name) return; // Cancelled
    
    const preset = {
      name: name,
      hold: document.getElementById('inp-hold').value,
      reach: document.getElementById('inp-reach').value,
      str: document.getElementById('inp-str').value,
      count: tgtCount,
      sth: document.getElementById('inp-sth').value
    };
    
    const list = JSON.parse(localStorage.getItem('onigiri_presets') || "[]");
    list.push(preset);
    localStorage.setItem('onigiri_presets', JSON.stringify(list));
    renderMyPresets();
    showToast("保存しました");
  });
}

function loadMyPreset(idx) {
  const list = JSON.parse(localStorage.getItem('onigiri_presets') || "[]");
  const p = list[idx];
  if(!p) return;
  
  showConfirm(`「${p.name}」を読み込みますか？`).then(ok => {
    if(!ok) return;

    document.getElementById('inp-hold').value = p.hold;
    document.getElementById('inp-reach').value = p.reach;
    document.getElementById('inp-str').value = p.str;
    document.getElementById('inp-sth').value = p.sth || 10;
    
    // Apply changes
    saveHold(p.hold);
    saveReach(p.reach);
    saveSth(p.sth || 10);
    // Sync Strength & Count
    fetch(`/api/settings?str=${p.str}&cnt=${p.count}`).catch(()=>{});
    
    // UI update
    updVal('str-disp', p.str, '%');
    document.getElementById('sth-disp').innerText = p.sth || 10;
    
    // Set Count
    tgtCount = p.count;
    document.querySelectorAll('.chk-btn').forEach(b => {
        if(parseInt(b.innerText) == p.count) b.classList.add('active');
        else b.classList.remove('active');
    });
    
    updTimeDisp();
    showToast("読み込みました");
  });
}

function deleteMyPreset(idx) {
  showConfirm("本当に削除しますか？", true).then(ok => {
    if(!ok) return;
    const list = JSON.parse(localStorage.getItem('onigiri_presets') || "[]");
    list.splice(idx, 1);
    localStorage.setItem('onigiri_presets', JSON.stringify(list));
    renderMyPresets();
    showToast("削除しました");
  });
}

function renderMyPresets() {
  const list = JSON.parse(localStorage.getItem('onigiri_presets') || "[]");
  const container = document.getElementById('my-preset-list');
  container.innerHTML = "";
  
  if(list.length === 0) {
    container.innerHTML = "<div style='color:#ccc; font-size:0.85rem; text-align:center;'>保存された設定はありません</div>";
    return;
  }
  
  list.forEach((p, i) => {
    const row = document.createElement('div');
    row.style = "display:flex; justify-content:space-between; align-items:center; background:#fff; padding:10px; border-radius:12px; border:1px solid #eee;";
    row.innerHTML = `
      <div style="font-weight:bold; font-size:0.95rem;">${p.name}</div>
      <div style="display:flex; gap:8px;">
        <button onclick="loadMyPreset(${i})" style="background:#56d364; color:white; border:none; padding:6px 12px; border-radius:14px; font-weight:bold; font-size:0.8rem;">適用</button>
        <button onclick="deleteMyPreset(${i})" style="background:#ff3b30; color:white; border:none; padding:6px 12px; border-radius:14px; font-weight:bold; font-size:0.8rem;">削除</button>
      </div>
    `;
    container.appendChild(row);
  });
}

// センサー距離更新（1秒ごと）
setInterval(() => {
  fetch('/api/distance')
    .then(r => r.json())
    .then(data => {
      document.getElementById('distance-disp').innerText = data.distance.toFixed(1);
    })
    .catch(e => console.error('Distance fetch error:', e));
}, 1000);

// 起動時に個別オフセット読み込み
fetchServoOffsets();
fetchServoLimits();

// 角度制限更新
function updateServoLimit(servoNum) {
  const minVal = document.getElementById(`min-angle-${servoNum}`).value;
  const maxVal = document.getElementById(`max-angle-${servoNum}`).value;
  
  if(parseInt(minVal) > parseInt(maxVal)) {
    // 最小値が最大値を超えないように
    return;
  }

  fetch(`/api/servo_limit?servo=${servoNum}&min=${minVal}&max=${maxVal}`)
    .then(r => r.text())
    .then(d => console.log(`Servo ${servoNum} limit updated: ${minVal}-${maxVal}`));
}

// 角度制限読み込み
function fetchServoLimits() {
  for(let i=1; i<=3; i++) {
    fetch(`/api/servo_limit?servo=${i}`)
      .then(r => r.json())
      .then(d => {
        document.getElementById(`min-angle-${i}`).value = d.min;
        document.getElementById(`max-angle-${i}`).value = d.max;
      });
  }
}


</script>
</body>
</html>
)rawliteral";

WebServer server(80);
DNSServer dnsServer;
const byte DNS_PORT = 53;
Preferences preferences;

Servo servo1, servo2, servo3;
const int PIN_SERVO1 = 25;
const int PIN_SERVO2 = 26;
const int PIN_SERVO3 = 27;

// --- WS2812B LED設定 ---
#define LED_COUNT 35
#define LED_PIN 13
Adafruit_NeoPixel pixels(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);
bool ledManualMode = false;   // true=ユーザー手動操作中, false=ステート連動
uint32_t currentLedColor = 0; // 現在の色 (Manual時保持用)

void updateLed(uint32_t color) {
  for (int i = 0; i < pixels.numPixels(); i++) {
    pixels.setPixelColor(i, color);
  }
  pixels.show();
}

// --- サーボ設定 ---
const int US_AT_0_DEG = 500;    // 0度 (閉/強)
const int US_AT_270_DEG = 2500; // 270度 (開/弱)
const unsigned long DETACH_DELAY_MS = 5000;

// --- 位置補正（個別サーボオフセット） ---
int servo1Offset = 0; // -90 ~ +90の範囲で調整可能
int servo2Offset = 0;
int servo3Offset = 0;

// --- 角度制限 (最小/最大) ---
int minAngle1 = 0;
int maxAngle1 = 270;
int minAngle2 = 0;
int maxAngle2 = 270;
int minAngle3 = 0;
int maxAngle3 = 270;

// --- HC-SR04センサー ---
const int TRIG_PIN = 32;
const int ECHO_PIN = 33;
float currentDistance = 0.0;
unsigned long lastDistanceMeasure = 0;

// 前方宣言
void setServoAngleSafe(int servoNum, int targetAngle);

bool sensorEnabled = false;
float sensorThreshold = 10.0; // Default 10cm
int sensorTriggerCount = 0;   // 連続検知カウンタ

// --- 履歴構造体 ---
struct HistoryItem {
  String timeStr;
  String preset;
  int strength;
  int count;
};
std::vector<HistoryItem> historyLog;
String currentSessionPreset = "Custom";

// 状態管理
// 状態管理
/*
enum State {
  IDLE,
  PREPARE_SQUEEZE,
  SQUEEZING,
  HOLDING,
  RELEASING,
  WAIT_CYCLE,
  FINISHED_BLINK
};
*/
// 状態定義
// - IDLE: 待機中
// - PREPARE_SQUEEZE: 締め付け準備中
// - SQUEEZING: 締め付け中
// - HOLDING: 締め付け保持中
// - RELEASING: 緩め中
// - WAIT_CYCLE: 次のサイクル待機中
// - FINISHED_BLINK: 完了点滅中 (IDLEではないので動作中扱い)
const int IDLE = 0;
const int PREPARE_SQUEEZE = 1;
const int SQUEEZING = 2;
const int HOLDING = 3;
const int RELEASING = 4;
const int WAIT_CYCLE = 5;
const int FINISHED_BLINK = 6;

int currentState = IDLE;
int lastState = IDLE;

unsigned long stateStartTime = 0;
unsigned long sessionStartTime = 0;

// パラメータ
float holdTimeSec = 0.5;
float reachTimeSec = 0.5;

int targetStrength = 50;
int targetCount = 3;
int currentCycle = 0;
int pin13State = 0;
int activeLedCount = 35;

void setAllServosAngle(int angle) {
  setServoAngleSafe(1, angle);
  setServoAngleSafe(2, angle);
  setServoAngleSafe(3, angle);
}

int strengthToAngle(int strength) {
  if (strength < 0)
    strength = 0;
  if (strength > 100)
    strength = 100;
  // 270度(開) → 90度(最大閉) の範囲に制限
  return map(strength, 0, 100, 270, 90);
}

void attachAllServos() {
  if (!servo1.attached())
    servo1.attach(PIN_SERVO1, US_AT_0_DEG, US_AT_270_DEG);
  if (!servo2.attached())
    servo2.attach(PIN_SERVO2, US_AT_0_DEG, US_AT_270_DEG);
  if (!servo3.attached())
    servo3.attach(PIN_SERVO3, US_AT_0_DEG, US_AT_270_DEG);
}

void detachAllServos() {
  if (servo1.attached())
    servo1.detach();
  if (servo2.attached())
    servo2.detach();
  if (servo3.attached())
    servo3.detach();
}

// 安全なサーボ制御関数 (制限とオフセット適用)
void setServoAngleSafe(int servoNum, int targetAngle) {
  int minA = 0;
  int maxA = 270;
  int offset = 0;

  if (servoNum == 1) {
    minA = minAngle1;
    maxA = maxAngle1;
    offset = servo1Offset;
  } else if (servoNum == 2) {
    minA = minAngle2;
    maxA = maxAngle2;
    offset = servo2Offset;
  } else if (servoNum == 3) {
    minA = minAngle3;
    maxA = maxAngle3;
    offset = servo3Offset;
  } else {
    return; // Invalid servo number
  }

  // 1. 角度制限 (最優先)
  if (targetAngle < minA)
    targetAngle = minA;
  if (targetAngle > maxA)
    targetAngle = maxA;

  // 2. オフセット適用
  // 補正: 目標角度 - オフセット
  int correctedAngle = targetAngle - offset;

  // 3. 物理限界リミット
  if (correctedAngle < 0)
    correctedAngle = 0;
  if (correctedAngle > 270)
    correctedAngle = 270;

  // 4. 出力
  int us = map(correctedAngle, 0, 270, US_AT_0_DEG, US_AT_270_DEG);
  Serial.printf("S%d->%d(%dus)\n", servoNum, correctedAngle, us); // Debug

  if (servoNum == 1) {
    if (!servo1.attached())
      servo1.attach(PIN_SERVO1, US_AT_0_DEG, US_AT_270_DEG);
    servo1.writeMicroseconds(us);
  } else if (servoNum == 2) {
    if (!servo2.attached())
      servo2.attach(PIN_SERVO2, US_AT_0_DEG, US_AT_270_DEG);
    servo2.writeMicroseconds(us);
  } else if (servoNum == 3) {
    if (!servo3.attached())
      servo3.attach(PIN_SERVO3, US_AT_0_DEG, US_AT_270_DEG);
    servo3.writeMicroseconds(us);
  }
}

// Helper for raw read
float readRawDistance() {
  digitalWrite(32, LOW); // TRIG
  delayMicroseconds(2);
  digitalWrite(32, HIGH);
  delayMicroseconds(10);
  digitalWrite(32, LOW);

  long duration =
      pulseIn(33, HIGH, 3000); // 3ms timeout (approx 50cm) to fix lag
  if (duration == 0)
    return 999.0;
  return duration * 0.034 / 2.0;
}

float measureDistance() {
  // Enhanced Median Filter (Reduced to 3 samples for speed)
  float readings[3];
  for (int i = 0; i < 3; i++) {
    readings[i] = readRawDistance();
    delay(1); // Reduced delay
  }

  // Simple Bubble Sort
  for (int i = 0; i < 2; i++) {
    for (int j = 0; j < 2 - i; j++) {
      if (readings[j] > readings[j + 1]) {
        float temp = readings[j];
        readings[j] = readings[j + 1];
        readings[j + 1] = temp;
      }
    }
  }

  // Return Median
  return readings[1];
}

// API: Start
void handleApiStart() {
  if (server.hasArg("str"))
    targetStrength = server.arg("str").toInt();
  if (server.hasArg("cnt"))
    targetCount = server.arg("cnt").toInt();
  if (server.hasArg("preset"))
    currentSessionPreset = server.arg("preset");
  else
    currentSessionPreset = "カスタム";

  if (targetStrength > 100)
    targetStrength = 100;
  if (targetStrength < 0)
    targetStrength = 0;

  Serial.printf("[API] Start: Str=%d%%, Cnt=%d, Preset=%s\n", targetStrength,
                targetCount, currentSessionPreset.c_str());

  currentCycle = 0;
  sessionStartTime = millis();
  currentState = PREPARE_SQUEEZE;

  server.send(200, "text/plain", "OK");
}

void handleApiStop() {
  Serial.println("[API] Stop");
  currentState = IDLE;
  attachAllServos();
  setAllServosAngle(270);
  server.send(200, "text/plain", "OK");
}

void handleApiSettings() {
  if (server.hasArg("led_cnt")) {
    int cnt = server.arg("led_cnt").toInt();
    if (cnt < 1)
      cnt = 1;
    if (cnt > 35)
      cnt = 35;
    activeLedCount = cnt;
    preferences.putInt("led_cnt", activeLedCount);
    Serial.printf("[API] LED Count: %d\n", activeLedCount);
  }
  if (server.hasArg("hold")) {
    holdTimeSec = server.arg("hold").toFloat();
    preferences.putFloat("hold", holdTimeSec);
  }
  if (server.hasArg("reach")) {
    reachTimeSec = server.arg("reach").toFloat();
    preferences.putFloat("reach", reachTimeSec);
  }
  // Sync params for sensor auto-start
  if (server.hasArg("str"))
    targetStrength = server.arg("str").toInt();
  if (server.hasArg("cnt"))
    targetCount = server.arg("cnt").toInt();
  if (server.hasArg("sth")) {
    sensorThreshold = server.arg("sth").toFloat();
    preferences.putFloat("sth", sensorThreshold);
  }

  String json = "{";
  json += "\"hold\":" + String(holdTimeSec) + ",";
  json += "\"reach\":" + String(reachTimeSec) + ",";
  json += "\"pin13\":" + String(pin13State) + ",";
  json += "\"sensor\":" + String(sensorEnabled) + ",";
  json += "\"sth\":" + String(sensorThreshold);
  json += "}";
  server.send(200, "application/json", json);
}

void handleApiPin13() {
  if (server.hasArg("val")) {
    pin13State = server.arg("val").toInt();
    digitalWrite(13, pin13State ? HIGH : LOW);
    preferences.putInt("pin13", pin13State);
  }
  server.send(200, "text/plain", "OK");
}

void handleApiStatus() {
  String s;
  switch (currentState) {
  case IDLE:
    s = "IDLE";
    break;
  case PREPARE_SQUEEZE:
    s = "PREPARE_SQUEEZE";
    break;
  case SQUEEZING:
    s = "SQUEEZING";
    break;
  case HOLDING:
    s = "HOLDING";
    break;
  case RELEASING:
    s = "RELEASING";
    break;
  case WAIT_CYCLE:
    s = "WAIT_CYCLE";
    break;
  case FINISHED_BLINK:
    s = "FINISHED_BLINK";
    break;
  }

  // Exclude WAIT time (0.3s) to prevent timer remaining at the end
  // PREPARE (0.3s) + REACH + HOLD + REACH + WAIT (0.3s) = Total Cycle
  // Corrected: Overhead is 0.4s (WAIT+Margin) per cycle + 0.4s (PREPARE+Margin)
  // initial
  float cycleDur = (reachTimeSec * 2) + holdTimeSec + 0.4;
  float totalDur = (targetCount * cycleDur) + 0.4;

  String json = "{";
  json += "\"state\":\"" + s + "\",";
  json += "\"cycle\":" + String(currentCycle) + ",";
  json += "\"total\":" + String(targetCount) + ",";
  json += "\"elap\":" + String(millis() - sessionStartTime) + ",";
  json += "\"pin13\":" + String(pin13State) + ",";
  json += "\"dur\":" + String(totalDur) + ",";
  json += "\"preset\":\"" + currentSessionPreset + "\",";
  json += "\"str\":" + String(targetStrength);
  json += "}";
  server.send(200, "application/json", json);
}

void handleApiManual() {
  if (server.hasArg("val")) {
    int pct = server.arg("val").toInt();
    if (pct < 0)
      pct = 0;
    if (pct > 100)
      pct = 100;

    int targetAngle = strengthToAngle(pct);

    currentState = IDLE;
    stateStartTime =
        millis(); // Reset idle timer so it doesn't detach immediately
    attachAllServos();
    setAllServosAngle(targetAngle);

    Serial.printf("[API] Manual: %d%% -> %d deg\n", pct, targetAngle);
  }
  server.send(200, "text/plain", "OK");
}

void handleApiManualAngle() {
  if (server.hasArg("angle")) {
    int angle = server.arg("angle").toInt();
    // 90度～270度の範囲に制限
    if (angle < 90)
      angle = 90;
    if (angle > 270)
      angle = 270;

    currentState = IDLE;
    stateStartTime = millis();
    attachAllServos();
    setAllServosAngle(angle);

    Serial.printf("[API] Manual Angle: %d degrees\n", angle);
  }
  server.send(200, "text/plain", "OK");
}

void handleApiServoAll() {
  if (server.hasArg("angle")) {
    int angle = server.arg("angle").toInt();
    if (angle < 90)
      angle = 90;
    if (angle > 270)
      angle = 270;

    currentState = IDLE;
    stateStartTime = millis();
    attachAllServos();
    setAllServosAngle(angle);

    Serial.printf("[API] All Servos: %d degrees\n", angle);
  }
  server.send(200, "text/plain", "OK");
}

void handleApiServoIndividual() {
  if (server.hasArg("servo") && server.hasArg("angle")) {
    int servoNum = server.arg("servo").toInt();
    int angle = server.arg("angle").toInt();
    // 範囲チェックは setServoAngleSafe
    // 内で行われるため簡易チェックのみ、または省略可だが一応残す if (angle <
    // 90) angle = 90; //
    // 制限は個別のmin/maxに委ねるべきか？安全のためsetServoAngleSafeにお任せする

    currentState = IDLE;
    stateStartTime = millis();

    setServoAngleSafe(servoNum, angle);

    Serial.printf("[API] Servo %d: %d degrees\n", servoNum, angle);
  }
  server.send(200, "text/plain", "OK");
}

void handleApiHistory() {
  String json = "[";
  for (size_t i = 0; i < historyLog.size(); i++) {
    if (i > 0)
      json += ",";
    json += "{";
    json += "\"time\":\"" + historyLog[i].timeStr + "\",";
    json += "\"preset\":\"" + historyLog[i].preset + "\",";
    json += "\"strength\":" + String(historyLog[i].strength) + ",";
    json += "\"count\":" + String(historyLog[i].count);
    json += "}";
  }
  json += "]";
  server.send(200, "application/json", json);
}

void handleApiServoOffset() {
  if (server.hasArg("servo") && server.hasArg("value")) {
    int servoNum = server.arg("servo").toInt();
    int value = server.arg("value").toInt();
    if (value >= -90 && value <= 90 && servoNum >= 1 && servoNum <= 3) {
      int correctedAngle = 270 - value;
      if (correctedAngle < 0)
        correctedAngle = 0;
      if (correctedAngle > 270)
        correctedAngle = 270;
      int us = map(correctedAngle, 0, 270, US_AT_0_DEG, US_AT_270_DEG);

      if (servoNum == 1) {
        servo1Offset = value;
        preferences.putInt("servo1Off", value);
        if (!servo1.attached())
          servo1.attach(PIN_SERVO1, US_AT_0_DEG, US_AT_270_DEG);
        servo1.writeMicroseconds(us);
      } else if (servoNum == 2) {
        servo2Offset = value;
        preferences.putInt("servo2Off", value);
        if (!servo2.attached())
          servo2.attach(PIN_SERVO2, US_AT_0_DEG, US_AT_270_DEG);
        servo2.writeMicroseconds(us);
      } else if (servoNum == 3) {
        servo3Offset = value;
        preferences.putInt("servo3Off", value);
        if (!servo3.attached())
          servo3.attach(PIN_SERVO3, US_AT_0_DEG, US_AT_270_DEG);
        servo3.writeMicroseconds(us);
      }
      Serial.printf("[API] Servo %d Offset: %d\n", servoNum, value);
      server.send(200, "text/plain", "OK");
    } else {
      server.send(400, "text/plain", "Error: servo 1-3, value -90~+90");
    }
  } else if (server.hasArg("servo")) {
    int servoNum = server.arg("servo").toInt();
    int offset = 0;
    if (servoNum == 1)
      offset = servo1Offset;
    else if (servoNum == 2)
      offset = servo2Offset;
    else if (servoNum == 3)
      offset = servo3Offset;
    String json = "{\"offset\":" + String(offset) + "}";
    server.send(200, "application/json", json);
  } else {
    server.send(400, "text/plain", "Missing servo parameter");
  }
}

void handleApiServoLimit() {
  if (server.hasArg("servo")) {
    int servoNum = server.arg("servo").toInt();
    if (server.hasArg("min") && server.hasArg("max")) {
      int minV = server.arg("min").toInt();
      int maxV = server.arg("max").toInt();
      if (minV < 0)
        minV = 0;
      if (maxV > 270)
        maxV = 270;

      if (servoNum == 1) {
        minAngle1 = minV;
        maxAngle1 = maxV;
        preferences.putInt("minAng1", minV);
        preferences.putInt("maxAng1", maxV);
      } else if (servoNum == 2) {
        minAngle2 = minV;
        maxAngle2 = maxV;
        preferences.putInt("minAng2", minV);
        preferences.putInt("maxAng2", maxV);
      } else if (servoNum == 3) {
        minAngle3 = minV;
        maxAngle3 = maxV;
        preferences.putInt("minAng3", minV);
        preferences.putInt("maxAng3", maxV);
      }
      server.send(200, "text/plain", "OK");
    } else {
      int minV = 0, maxV = 270;
      if (servoNum == 1) {
        minV = minAngle1;
        maxV = maxAngle1;
      } else if (servoNum == 2) {
        minV = minAngle2;
        maxV = maxAngle2;
      } else if (servoNum == 3) {
        minV = minAngle3;
        maxV = maxAngle3;
      }

      String json =
          "{\"min\":" + String(minV) + ",\"max\":" + String(maxV) + "}";
      server.send(200, "application/json", json);
    }
  } else {
    server.send(400, "text/plain", "Missing servo param");
  }
}

void handleApiLed() {
  int r = 0, g = 0, b = 0;
  if (server.hasArg("color")) {
    String hex = server.arg("color");
    if (hex.startsWith("#"))
      hex = hex.substring(1);
    long number = strtol(hex.c_str(), NULL, 16);
    r = (number >> 16) & 0xFF;
    g = (number >> 8) & 0xFF;
    b = number & 0xFF;
  } else if (server.hasArg("r") && server.hasArg("g") && server.hasArg("b")) {
    r = server.arg("r").toInt();
    g = server.arg("g").toInt();
    b = server.arg("b").toInt();
  }

  ledManualMode = true; // Switch to manual mode
  currentLedColor = pixels.Color(r, g, b);
  updateLed(currentLedColor);

  Serial.printf("[API] LED Manual: R%d G%d B%d\n", r, g, b);
  server.send(200, "text/plain", "OK");
}

void handleApiLedMode() {
  if (server.hasArg("mode")) {
    String m = server.arg("mode");
    if (m == "auto") {
      ledManualMode = false;
      Serial.println("[API] LED Mode: Auto");
    } else {
      ledManualMode = true;
      Serial.println("[API] LED Mode: Manual");
    }
  }
  server.send(200, "text/plain", "OK");
}

void handleApiSensorMode() {
  if (server.hasArg("val")) {
    sensorEnabled = (server.arg("val").toInt() == 1);
    preferences.putBool("sensor", sensorEnabled);
    Serial.printf("[API] Sensor Mode: %s\n", sensorEnabled ? "ON" : "OFF");
  }
  server.send(200, "text/plain", "OK");
}

void handleApiDistance() {
  String json = "{\"distance\":" + String(currentDistance, 1) + "}";
  server.send(200, "application/json", json);
}

void handleRoot() {
  String html = html_main;
  html.replace("{{BUILD_TIME}}", __DATE__ " " __TIME__);
  server.send(200, "text/html", html);
}

void setup() {
  Serial.begin(115200);

  preferences.begin("job", false);
  holdTimeSec = preferences.getFloat("hold", 0.5);
  reachTimeSec = preferences.getFloat("reach", 0.5);
  pin13State = preferences.getInt("pin13", 0);
  activeLedCount = preferences.getInt("led_cnt", 35);
  servo1Offset = preferences.getInt("servo1Off", 0);
  servo2Offset = preferences.getInt("servo2Off", 0);
  servo3Offset = preferences.getInt("servo3Off", 0);

  sensorEnabled = preferences.getBool("sensor", false);
  sensorThreshold = preferences.getFloat("sth", 10.0);

  minAngle1 = preferences.getInt("minAng1", 0);
  maxAngle1 = preferences.getInt("maxAng1", 270);
  minAngle2 = preferences.getInt("minAng2", 0);
  maxAngle2 = preferences.getInt("maxAng2", 270);
  minAngle3 = preferences.getInt("minAng3", 0);
  maxAngle3 = preferences.getInt("maxAng3", 270);

  // HC-SR04センサー設定
  pinMode(32, OUTPUT); // Trig
  pinMode(33, INPUT);  // Echo

  pinMode(13, OUTPUT);
  // digitalWrite(13, pin13State ? HIGH : LOW);

  // NeoPixel Init
  pixels.begin();
  pixels.setBrightness(50); // 適度な明るさ
  pixels.show();            // Initialize all pixels to 'off'

  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  ESP32PWM::allocateTimer(2);
  ESP32PWM::allocateTimer(3);

  servo1.setPeriodHertz(50);
  servo2.setPeriodHertz(50);
  servo3.setPeriodHertz(50);

  // attachAllServos();
  Serial.printf("Servo1 attached: %d\n",
                servo1.attach(PIN_SERVO1, US_AT_0_DEG, US_AT_270_DEG));
  Serial.printf("Servo2 attached: %d\n",
                servo2.attach(PIN_SERVO2, US_AT_0_DEG, US_AT_270_DEG));
  Serial.printf("Servo3 attached: %d\n",
                servo3.attach(PIN_SERVO3, US_AT_0_DEG, US_AT_270_DEG));

  setAllServosAngle(270); // 初期位置

  // --- AP Mode Setup (Yakisoba-Shiro) ---
  WiFi.disconnect(true);
  delay(100);
  WiFi.mode(WIFI_AP);
  WiFi.softAP("焼きそば四郎"); // No Password

  Serial.print("AP IPs: ");
  Serial.println(WiFi.softAPIP());

  // DNS Server (Captive Portal)
  dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
  dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());

  server.on("/", handleRoot);
  server.on("/api/status", handleApiStatus);
  server.on("/api/start", handleApiStart);
  server.on("/api/stop", handleApiStop);
  server.on("/api/settings", handleApiSettings);
  // server.on("/api/pin13", handleApiPin13);
  server.on("/api/manual", handleApiManual);
  server.on("/api/manual_angle", handleApiManualAngle);
  server.on("/api/servo_all", handleApiServoAll);
  server.on("/api/servo_individual", handleApiServoIndividual);
  server.on("/api/history", handleApiHistory);
  server.on("/api/servo_offset", handleApiServoOffset);
  server.on("/api/servo_limit", handleApiServoLimit);
  server.on("/api/sensor_mode", handleApiSensorMode);

  server.on("/api/distance", handleApiDistance);
  server.on("/api/led", handleApiLed);
  server.on("/api/led_mode", handleApiLedMode);

  // Captive Portal Redirect
  server.onNotFound([]() { handleRoot(); });

  server.begin();
  Serial.println("Ready.");
}

void loop() {
  dnsServer.processNextRequest();
  server.handleClient();

  unsigned long now = millis();

  // HC-SR04センサー測定 (200msごと)
  if (now - lastDistanceMeasure > 200) {
    currentDistance = measureDistance();
    lastDistanceMeasure = now;

    if (currentState ==
        IDLE) { // Only check if IDLE to prevent lag during operation? User
                // wants button fix -> Optimization done in measureDistance
      if (sensorEnabled && currentDistance < sensorThreshold &&
          currentDistance > 0.1) {
        sensorTriggerCount++;
        Serial.printf("Sensor Detect: %.1f cm (Count: %d)\n", currentDistance,
                      sensorTriggerCount);

        // 3回連続検知 (approx 600ms) で開始
        if (sensorTriggerCount >= 3) {
          Serial.println(">>> Sensor START Triggered");
          sensorTriggerCount = 0;
          currentCycle = 0;
          sessionStartTime = millis();
          currentState = PREPARE_SQUEEZE;
          currentSessionPreset = "センサー自動";
        }
      } else {
        sensorTriggerCount = 0;
      }
    }
  }

  if (currentState != lastState) {
    stateStartTime = now;
    Serial.printf("State: %d\n", currentState);

    if (currentState == PREPARE_SQUEEZE) {
      attachAllServos();
      setAllServosAngle(270);
    }
    if (currentState == PREPARE_SQUEEZE) {
      attachAllServos();
      setAllServosAngle(270);
    }
    lastState = currentState;
  }

  // --- LED Auto Control ---
  static int lastLedState = -1;
  static bool lastLedManualMode = false;
  static uint32_t lastLedManualColor = 0;

  // Manual Mode Change Check or State Change or Manual Color Change
  bool needLedUpdate = false;

  if (ledManualMode != lastLedManualMode)
    needLedUpdate = true;
  if (!ledManualMode && currentState != lastLedState)
    needLedUpdate = true;
  if (ledManualMode && lastLedManualColor != currentLedColor)
    needLedUpdate = true;

  // Force update during blink to allow animation
  if (currentState == FINISHED_BLINK)
    needLedUpdate = true;

  if (needLedUpdate) {
    if (ledManualMode) {
      updateLed(currentLedColor);
      lastLedManualColor = currentLedColor;
    } else {
      if (currentState == IDLE) {
        updateLed(0); // OFF
      } else if (currentState == FINISHED_BLINK) {
        unsigned long elapsed = millis() - stateStartTime;
        // Blink Logic: Faster! 0.3s interval, 3 times
        // 0-300 ON, 300-600 OFF
        // 600-900 ON, 900-1200 OFF
        // 1200-1500 ON, 1500-1800 OFF
        bool on = false;
        if (elapsed < 300)
          on = true;
        else if (elapsed >= 600 && elapsed < 900)
          on = true;
        else if (elapsed >= 1200 && elapsed < 1500)
          on = true;

        if (on)
          updateLed(pixels.Color(0, 255, 0)); // Green
        else
          updateLed(0); // OFF
      } else {
        // Progressive Green Bar
        // Calculate total duration
        // Corrected: Include PREPARE(0.3) + WAIT(0.3) overhead
        // Corrected: Overhead is 0.4s (WAIT+Margin) per cycle + 0.4s
        // (PREPARE+Margin) initial
        float cycleDur = (reachTimeSec * 2) + holdTimeSec + 0.4;
        float totalDur = (targetCount * cycleDur) + 0.4; // +0.4 margin
        // Current elapsed in session
        // We need sessionStartTime from start
        unsigned long sessionElapsed = millis() - sessionStartTime;

        float progress = (float)sessionElapsed / (totalDur * 1000.0);
        if (progress > 1.0)
          progress = 1.0;
        if (progress < 0.0)
          progress = 0.0;

        int ledCount = (int)(progress * activeLedCount);
        for (int i = 0; i < LED_COUNT; i++) {
          // Limit to activeLedCount for total display
          if (i < activeLedCount) {
            if (i < ledCount)
              pixels.setPixelColor(i, pixels.Color(0, 255, 0));
            else
              pixels.setPixelColor(i, 0);
          } else {
            pixels.setPixelColor(i, 0); // Always OFF above limit
          }
        }
        pixels.show();
      }
    }
    lastLedState = currentState;
    lastLedManualMode = ledManualMode;
  }

  switch (currentState) {
  case IDLE:
    // Auto Detach Removed as per user request
    yield(); // Watchdog reset
    break;

  case PREPARE_SQUEEZE:
    setAllServosAngle(270);
    if (now - stateStartTime > 300) {
      currentState = SQUEEZING;
    }
    yield(); // Watchdog reset
    break;

  case SQUEEZING: {
    unsigned long duration = reachTimeSec * 1000;
    unsigned long elapsed = now - stateStartTime;
    int startAngle = 270;
    int targetAngle = strengthToAngle(targetStrength);

    if (elapsed >= duration) {
      setAllServosAngle(targetAngle);
      currentState = HOLDING;
    } else {
      float progress = (float)elapsed / (float)duration;
      int currentAngle = startAngle + (targetAngle - startAngle) * progress;
      setAllServosAngle(currentAngle);
    }
    yield(); // Watchdog reset
  } break;

  case HOLDING:
    if (now - stateStartTime >= (holdTimeSec * 1000)) {
      currentState = RELEASING;
    }
    yield(); // Watchdog reset
    break;

  case RELEASING:
    setAllServosAngle(270);
    // Latency Fix: Skip wait on the very last cycle to start blinking
    // immediately
    if (currentCycle == targetCount - 1) {
      currentState = WAIT_CYCLE;
    } else {
      if (now - stateStartTime >= 300) {
        currentState = WAIT_CYCLE;
      }
    }
    yield(); // Watchdog reset
    break;

  case FINISHED_BLINK:
    if (now - stateStartTime >= 900) {
      currentState = IDLE;
      setAllServosAngle(270); // Ensure Reset
    }
    yield();
    break;

  case WAIT_CYCLE:
    currentCycle++;
    if (currentCycle < targetCount) {
      currentState = SQUEEZING;
    } else {
      Serial.println("Finished.");
      setAllServosAngle(270);
      currentState = FINISHED_BLINK;
      stateStartTime = now; // Reuse for blink timing

      // --- 履歴保存 ---
      struct tm timeinfo;
      if (!getLocalTime(&timeinfo)) {
        Serial.println("Failed to obtain time");
      } else {
        char timeStringBuff[50];
        strftime(timeStringBuff, sizeof(timeStringBuff), "%Y/%m/%d %H:%M",
                 &timeinfo);

        HistoryItem newItem;
        newItem.timeStr = String(timeStringBuff);
        newItem.preset = currentSessionPreset;
        newItem.strength = targetStrength;
        newItem.count = targetCount;

        historyLog.push_back(newItem);

        // 最大保存件数 (20件)
        if (historyLog.size() > 20) {
          historyLog.erase(historyLog.begin());
        }
      }
      // ----------------
    }
    yield(); // Watchdog reset
    break;
  }
}
