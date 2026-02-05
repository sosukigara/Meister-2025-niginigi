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
        if (isDanger) { btn.className = "modal-btn btn-danger"; }
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
        .then(r => r.json())
        .then(d => {
            if (d.hold) document.getElementById('inp-hold').value = d.hold;
            if (d.reach) document.getElementById('inp-reach').value = d.reach;
            if (d.pin13 !== undefined) document.getElementById('chk-pin13').checked = (d.pin13 == 1);
            if (d.sensor !== undefined) {
                document.getElementById('chk-sensor').checked = (d.sensor == 1);
                updSensorLbl(d.sensor == 1);
            }
            if (d.sth) {
                document.getElementById('inp-sth').value = d.sth;
                document.getElementById('sth-disp').innerText = d.sth;
            }
            if (d.build) {
                document.getElementById('build-disp').innerText = d.build;
            }

            if (d.str !== undefined) {
                document.getElementById('inp-str').value = d.str;
                updVal('str-disp', d.str, '%');
            }
            if (d.cnt !== undefined) {
                tgtCount = d.cnt;
                document.querySelectorAll('.chk-btn').forEach(b => {
                    if (parseInt(b.innerText) === tgtCount) b.classList.add('active');
                    else b.classList.remove('active');
                });
            }
            if (d.led_cnt !== undefined) {
                document.getElementById('inp-led-cnt').value = d.led_cnt;
                document.getElementById('led-cnt-disp').innerText = d.led_cnt;
            }

            // Initial Preset Matching
            let matched = null;
            // Note: soft=2, normal=3, kosen=5
            if (d.str == 30 && d.cnt == 2) matched = 'やわらか';
            else if (d.str == 50 && d.cnt == 3) matched = 'ふつう';
            else if (d.str == 90 && d.cnt == 5) matched = '高専生用';

            document.querySelectorAll('.preset-btn').forEach(b => b.classList.remove('active'));
            if (matched) {
                const btn = document.querySelector(`.preset-btn[data-name="${matched}"]`);
                if (btn) btn.classList.add('active');
                currentPresetName = matched;
            } else {
                currentPresetName = "Custom";
            }

            updTimeDisp();
            renderMyPresets();
            setTimeout(() => document.body.classList.add('ready'), 50);
        }).catch(e => {
            updTimeDisp();
            document.body.classList.add('ready');
        });
}
fetchSettings();

function saveHold(v) { fetch('/api/settings?hold=' + v).then(() => updTimeDisp()); }
function saveReach(v) { fetch('/api/settings?reach=' + v).then(() => updTimeDisp()); }
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
    if (isOn) { el.innerText = "ON"; el.style.color = "var(--accent-blue)"; }
    else { el.innerText = "OFF"; el.style.color = "var(--text-sub)"; }
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
function setLedColor(r, g, b) {
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
    // 理論値: PREPARE(0.3s) + Count * (Reach + Hold + RELEASE(0.5s))
    // Backend: 0.3 + (targetCount * (reach + hold + 0.5))
    const total = 0.3 + (tgtCount * (r + h + 0.5));
    document.getElementById('time-display').innerText = fmtTime(Math.ceil(total));
}

function setCount(n, el) {
    tgtCount = n;
    document.querySelectorAll('.chk-btn').forEach(b => b.classList.remove('active'));
    el.classList.add('active');
    if (!isRunning) updTimeDisp();
}

function setPreset(mode, el) {
    document.querySelectorAll('.preset-btn').forEach(b => b.classList.remove('active'));
    el.classList.add('active');
    currentPresetName = el.getAttribute('data-name');

    const s = document.getElementById('inp-str');
    let count = 3;

    if (mode === 'soft') {
        s.value = 30;
        count = 2;
    }
    if (mode === 'normal') {
        s.value = 50;
        count = 3;
    }
    if (mode === 'kosen') {
        s.value = 90;
        count = 5;
    }

    updVal('str-disp', s.value, '%');

    tgtCount = count;
    document.querySelectorAll('.chk-btn').forEach(b => {
        if (parseInt(b.innerText) === count) {
            b.classList.add('active');
        } else {
            b.classList.remove('active');
        }
    });

    // Sync params with debounce
    if (debounceTimer) clearTimeout(debounceTimer);
    debounceTimer = setTimeout(() => {
        fetch(`/api/settings?str=${s.value}&cnt=${count}`).catch(() => { });
    }, 500);

    if (!isRunning) updTimeDisp();
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
            if (data.length === 0) {
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
    // 理論値に合わせて補正
    sessionTotalDur = 0.3 + (tgtCount * (r + h + 0.5));
    if (sessionTotalDur < 1) sessionTotalDur = 1;

    // プリセット名も送信
    // プリセット名も送信
    fetch(`/api/start?str=${s}&cnt=${tgtCount}&preset=${encodeURIComponent(runPreset)}`)
        .then(() => setTimeout(() => isStarting = false, 5000)) // タイムアウト延長
        .catch(() => { isStarting = false; });
}

function stop() {
    isManualStop = true;
    fetch('/api/stop').catch(() => { });
}

function setOnline(isOnline) {
    if (isOnline) {
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

            if (d.state !== 'IDLE') {
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
                if (d.dur && d.dur > 0) sessionTotalDur = d.dur;

                // 詳細情報の同期 (完了画面用)
                if (d.preset) runPreset = d.preset;
                if (d.str) runStrength = d.str;
                if (d.total) runCount = d.total;

                let txt = "動作中";
                if (d.state === 'PREPARE_SQUEEZE') txt = "準備中...";
                if (d.state === 'SQUEEZING') txt = "握り中";
                if (d.state === 'HOLDING') txt = "保持中";
                if (d.state === 'RELEASING') txt = "解放中";
                document.getElementById('status-badge').innerText = `${txt} (${d.cycle + 1}/${d.total})`;

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
    for (let i = 1; i <= 3; i++) {
        document.getElementById('servo' + i + '-val').innerText = angle + '°';
        const el = document.getElementById('inp-servo-' + i);
        if (el) el.value = angle;
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
        if (!name) return; // Cancelled

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
    if (!p) return;

    showConfirm(`「${p.name}」を読み込みますか？`).then(ok => {
        if (!ok) return;

        document.getElementById('inp-hold').value = p.hold;
        document.getElementById('inp-reach').value = p.reach;
        document.getElementById('inp-str').value = p.str;
        document.getElementById('inp-sth').value = p.sth || 10;

        // Apply changes
        saveHold(p.hold);
        saveReach(p.reach);
        saveSth(p.sth || 10);
        // Sync Strength & Count
        fetch(`/api/settings?str=${p.str}&cnt=${p.count}`).catch(() => { });

        // UI update
        updVal('str-disp', p.str, '%');
        document.getElementById('sth-disp').innerText = p.sth || 10;

        // Set Count
        tgtCount = p.count;
        document.querySelectorAll('.chk-btn').forEach(b => {
            if (parseInt(b.innerText) == p.count) b.classList.add('active');
            else b.classList.remove('active');
        });

        updTimeDisp();
        showToast("読み込みました");
    });
}

function deleteMyPreset(idx) {
    showConfirm("本当に削除しますか？", true).then(ok => {
        if (!ok) return;
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

    if (list.length === 0) {
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

    if (parseInt(minVal) > parseInt(maxVal)) {
        // 最小値が最大値を超えないように
        return;
    }

    fetch(`/api/servo_limit?servo=${servoNum}&min=${minVal}&max=${maxVal}`)
        .then(r => r.text())
        .then(d => console.log(`Servo ${servoNum} limit updated: ${minVal}-${maxVal}`));
}

// 角度制限読み込み
function fetchServoLimits() {
    for (let i = 1; i <= 3; i++) {
        fetch(`/api/servo_limit?servo=${i}`)
            .then(r => r.json())
            .then(d => {
                document.getElementById(`min-angle-${i}`).value = d.min;
                document.getElementById(`max-angle-${i}`).value = d.max;
            });
    }
}
