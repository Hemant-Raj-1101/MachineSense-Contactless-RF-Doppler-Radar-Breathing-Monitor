
/* ================================================================
   MACHINESENSE DASHBOARD — CLEAN APPLICATION LOGIC
   ================================================================ */

let WEBSOCKET_URL = 'ws://machinesense.local:81';

const MAX_CHART_POINTS = 30;
const RECONNECT_DELAY_MS = 3000;
const ALERT_DISPLAY_MS = 5000;

/* ================================================================
   DOM REFERENCES
   ================================================================ */

const dom = {
    overlay: document.getElementById('loading-overlay'),
    loadingStatus: document.getElementById('loading-status'),

    calibrationOverlay: document.getElementById('calibration-overlay'),
    calibrationProgressBar:
        document.getElementById('calibration-progress-bar'),
    calibrationTimer:
        document.getElementById('calibration-timer'),

    dashboard: document.getElementById('dashboard'),

    connectionDot:
        document.getElementById('connection-dot'),
    connectionLabel:
        document.getElementById('connection-label'),
    uptime:
        document.getElementById('header-uptime'),

    statusDisplay:
        document.getElementById('status-display'),
    statusIcon:
        document.getElementById('status-icon'),
    statusLabel:
        document.getElementById('status-label'),
    statusSublabel:
        document.getElementById('status-sublabel'),

    statAlerts:
        document.getElementById('stat-alerts'),
    statBreathing:
        document.getElementById('stat-breathing'),
    statLastAlert:
        document.getElementById('stat-last-alert'),

    statBreathBPM:
        document.getElementById('stat-breath-bpm'),
    statHeartFreq:
        document.getElementById('stat-heart-frequency'),
    statHeartBPM:
        document.getElementById('stat-heart-bpm'),

    vignette:
        document.getElementById('vignette-overlay'),

    /* FIXED EVENT LOG */
    logContainer:
        document.getElementById('event-log')
};

/* ================================================================
   STATE
   ================================================================ */

let ws = null;

let alertCount = 0;
let logIndex = 0;

let alertTimeout = null;
let uptimeStart = null;
let uptimeInterval = null;
let reconnectTimer = null;

let isConnected = false;

let calibrationActive = false;
let calibrationStartTime = null;
let calibrationInterval = null;

const CALIBRATION_DURATION_S = 15;


/* ================================================================
   WEB AUDIO ALERT
   ================================================================ */

let audioCtx = null;

function playAlertTone() {

    try {

        if (!audioCtx) {

            audioCtx = new (
                window.AudioContext ||
                window.webkitAudioContext
            )();
        }

        const now = audioCtx.currentTime;

        for (let i = 0; i < 3; i++) {

            const osc =
                audioCtx.createOscillator();

            const gain =
                audioCtx.createGain();

            osc.type = 'square';

            osc.frequency.setValueAtTime(
                800,
                now + i * 0.35
            );

            osc.frequency.setValueAtTime(
                600,
                now + i * 0.35 + 0.17
            );

            gain.gain.setValueAtTime(
                0.06,
                now + i * 0.35
            );

            gain.gain.exponentialRampToValueAtTime(
                0.001,
                now + i * 0.35 + 0.33
            );

            osc.connect(gain)
                .connect(audioCtx.destination);

            osc.start(now + i * 0.35);

            osc.stop(
                now + i * 0.35 + 0.35
            );
        }

    } catch (e) {

        console.log('Audio unavailable');

    }
}


/* ================================================================
   CHART DEFAULTS
   ================================================================ */

const chartDefaults = {

    responsive: true,

    maintainAspectRatio: false,

    animation: {
        duration: 300
    },

    interaction: {
        mode: 'nearest',
        intersect: false
    },

    plugins: {

        legend: {
            display: false
        },

        tooltip: {

            backgroundColor:
                'rgba(10, 14, 23, 0.92)',

            titleFont: {
                family: "'JetBrains Mono', monospace",
                size: 11
            },

            bodyFont: {
                family: "'JetBrains Mono', monospace",
                size: 11
            },

            borderColor:
                'rgba(34, 211, 238, 0.2)',

            borderWidth: 1,

            padding: 10,

            cornerRadius: 6
        }
    },

    scales: {

        x: {

            display: true,

            grid: {
                color:
                    'rgba(34, 211, 238, 0.05)',
                drawTicks: false
            },

            ticks: {

                font: {
                    family:
                        "'JetBrains Mono', monospace",
                    size: 9
                },

                color: '#334155',

                maxRotation: 0,

                maxTicksLimit: 6
            },

            border: {
                color:
                    'rgba(34, 211, 238, 0.1)'
            }
        },

        y: {

            display: true,

            grid: {
                color:
                    'rgba(34, 211, 238, 0.05)',
                drawTicks: false
            },

            ticks: {

                font: {
                    family:
                        "'JetBrains Mono', monospace",
                    size: 10
                },

                color: '#475569',

                padding: 8
            },

            border: {
                color:
                    'rgba(34, 211, 238, 0.1)'
            }
        }
    }
};


/* ================================================================
   BREATHING CHART
   ================================================================ */

const breathingCanvas =
    document.getElementById('breathing-chart');

let breathingChart = null;

if (breathingCanvas) {

    breathingChart = new Chart(
        breathingCanvas.getContext('2d'),
        {

            type: 'line',

            data: {

                labels: [],

                datasets: [{

                    label: 'Breathing (Hz)',

                    data: [],

                    borderColor: '#22d3ee',

                    backgroundColor:
                        'rgba(34, 211, 238, 0.08)',

                    borderWidth: 2,

                    pointRadius: 3,

                    pointBackgroundColor: '#22d3ee',

                    pointBorderColor: '#22d3ee',

                    pointHoverRadius: 5,

                    tension: 0.35,

                    fill: true
                }]
            },

            options: {

                ...chartDefaults,

                scales: {

                    ...chartDefaults.scales,

                    y: {

                        ...chartDefaults.scales.y,

                        min: 0,

                        max: 0.8,

                        ticks: {

                            ...chartDefaults.scales.y.ticks,

                            stepSize: 0.1,

                            callback:
                                v => v.toFixed(1)
                        }
                    }
                }
            }
        }
    );
}


/* ================================================================
   CHART DATA
   ================================================================ */

function pushChartData(
    chart,
    label,
    value
) {

    if (!chart) {
        return;
    }

    chart.data.labels.push(label);

    chart.data.datasets[0]
        .data.push(value);

    if (
        chart.data.labels.length >
        MAX_CHART_POINTS
    ) {

        chart.data.labels.shift();

        chart.data.datasets[0]
            .data.shift();
    }

    chart.update('none');
}


/* ================================================================
   WEBSOCKET CONNECTION
   ================================================================ */

function connect() {

    if (
        ws &&
        (
            ws.readyState === WebSocket.OPEN ||
            ws.readyState === WebSocket.CONNECTING
        )
    ) {

        return;
    }

    dom.loadingStatus.textContent =
        `Connecting to ${WEBSOCKET_URL}`;

    showOverlay();

    try {

        ws = new WebSocket(
            WEBSOCKET_URL
        );

    } catch (e) {

        dom.loadingStatus.textContent =
            'ERROR: Failed to create WebSocket';

        scheduleReconnect();

        return;
    }


    /* ------------------------------------------------------------
       CONNECTED
       ------------------------------------------------------------ */

    ws.onopen = () => {

        console.log(
            'MachineSense WebSocket connected'
        );

        isConnected = true;

        clearTimeout(
            reconnectTimer
        );

        hideOverlay();

        setConnectionStatus(true);

        uptimeStart = Date.now();

        clearInterval(
            uptimeInterval
        );

        uptimeInterval =
            setInterval(
                updateUptime,
                1000
            );

        addLogEntry(
            'system',
            'MachineSense WebSocket connected'
        );
    };


    /* ------------------------------------------------------------
       MESSAGE
       ------------------------------------------------------------ */

    ws.onmessage = (event) => {

        console.log(
            'MachineSense:',
            event.data
        );

        parseMachineSenseMessage(
            event.data
        );
    };


    /* ------------------------------------------------------------
       ERROR
       ------------------------------------------------------------ */

    ws.onerror = () => {

        console.error(
            'MachineSense WebSocket error'
        );

        dom.loadingStatus.textContent =
            'CONNECTION ERROR — RETRYING...';
    };


    /* ------------------------------------------------------------
       CLOSED
       ------------------------------------------------------------ */

    ws.onclose = () => {

        console.log(
            'MachineSense WebSocket closed'
        );

        isConnected = false;

        setConnectionStatus(false);

        clearInterval(
            uptimeInterval
        );

        showOverlay();

        dom.loadingStatus.textContent =
            'CONNECTION LOST — RECONNECTING...';

        scheduleReconnect();
    };
}


/* ================================================================
   RECONNECT
   ================================================================ */

function scheduleReconnect() {

    clearTimeout(
        reconnectTimer
    );

    reconnectTimer =
        setTimeout(
            connect,
            RECONNECT_DELAY_MS
        );
}


/* ================================================================
   OVERLAY
   ================================================================ */

function showOverlay() {

    if (dom.overlay) {

        dom.overlay.classList.remove(
            'hidden'
        );
    }

    if (dom.dashboard) {

        dom.dashboard.classList.remove(
            'visible'
        );
    }
}


function hideOverlay() {

    if (dom.overlay) {

        dom.overlay.classList.add(
            'hidden'
        );
    }
}


/* ================================================================
   MACHINE SENSE MESSAGE PARSER
   ================================================================ */

function parseMachineSenseMessage(raw) {

    raw = String(raw).trim();

    /*
       Accept:

       $MachineSense
       $MACHINESENSE
       $machinesense
    */

    if (
        !raw
            .toUpperCase()
            .startsWith('$MACHINESENSE')
    ) {

        return;
    }

    const parts =
        raw.split(',');

    if (parts.length < 3) {

        return;
    }

    const msgType =
        parts[1].toUpperCase();


    /* ============================================================
       STATUS
       ============================================================ */

    if (
        msgType === 'STATUS' &&
        parts[2].toUpperCase() ===
            'CONNECTED'
    ) {

        addLogEntry(
            'system',
            'ESP32 connected — MachineSense protocol active'
        );

        return;
    }


    /* ============================================================
       DATA
       ============================================================ */

    if (
        msgType === 'DATA' &&
        parts.length >= 10
    ) {

        const breathFreq =
            parseFloat(parts[2]);

        const heartFreq =
            parseFloat(parts[3]);

        const breathMag =
            parseFloat(parts[4]);

        const heartMag =
            parseFloat(parts[5]);

        const systemState =
            parts[9].toUpperCase();


        /*
           We intentionally read confidence
           internally only because it is part
           of the ESP32 packet format.

           It is NOT displayed on dashboard.
        */

        const confidence =
            parseInt(parts[6], 10);

        // New firmware sends the confirmed detector state as field 14.
        // Keep compatibility with older firmware, where it is unavailable.
        const humanDetected = parts.length >= 15
            ? parts[14] === '1'
            : confidence >= 5;

        if (!Number.isFinite(breathFreq) || !Number.isFinite(heartFreq)) {
            addLogEntry('system', 'Invalid frequency values received from ESP32');
            return;
        }


        /* --------------------------------------------------------
           CALIBRATION
           -------------------------------------------------------- */

        if (
            systemState ===
            'CALIBRATING'
        ) {

            if (!calibrationActive) {

                showCalibrationOverlay();
            }

            return;
        }


        /* --------------------------------------------------------
           CALIBRATION COMPLETE
           -------------------------------------------------------- */

        if (
            calibrationActive &&
            systemState === 'ACTIVE'
        ) {

            hideCalibrationOverlay();

            addLogEntry(
                'system',
                'Calibration complete — MachineSense detection ACTIVE'
            );
        }


        /* --------------------------------------------------------
           SHOW DASHBOARD
           -------------------------------------------------------- */

        if (
            dom.dashboard &&
            !dom.dashboard.classList.contains(
                'visible'
            )
        ) {

            dom.dashboard.classList.add(
                'visible'
            );
        }


        /* --------------------------------------------------------
           TIME
           -------------------------------------------------------- */

        const now =
            new Date();

        const timeLabel =
            now.toLocaleTimeString(
                'en-US',
                {
                    hour12: false,
                    hour: '2-digit',
                    minute: '2-digit',
                    second: '2-digit'
                }
            );


        /* --------------------------------------------------------
           BREATHING CHART ONLY
           -------------------------------------------------------- */

        pushChartData(
            breathingChart,
            timeLabel,
            breathFreq
        );


        /* --------------------------------------------------------
           BREATHING DISPLAY
           -------------------------------------------------------- */

        if (dom.statBreathing) {

            dom.statBreathing.textContent =
                breathFreq.toFixed(3);
        }


        /* --------------------------------------------------------
           BREATHING BPM
           -------------------------------------------------------- */

        const breathBPM =
            breathFreq * 60;

        if (dom.statBreathBPM) {

            dom.statBreathBPM.innerHTML =
                `${breathBPM.toFixed(1)}
                 <span class="stat-card__unit">
                    BPM
                 </span>`;
        }

        if (dom.statHeartFreq) {
            dom.statHeartFreq.textContent = heartFreq.toFixed(3);
        }

        if (dom.statHeartBPM) {
            dom.statHeartBPM.innerHTML = `${(heartFreq * 60).toFixed(1)} <span class="stat-card__unit">BPM</span>`;
        }


        /* --------------------------------------------------------
           SIGNAL STATE
           -------------------------------------------------------- */

        if (
            humanDetected
        ) {

            dom.statusLabel.textContent =
                'HUMAN ACTIVITY DETECTED';

            dom.statusSublabel.textContent =
                'Microwave motion detected';

        } else {

            dom.statusLabel.textContent =
                'MONITORING';

            dom.statusSublabel.textContent =
                'System standing by';
        }


        /* --------------------------------------------------------
           EVENT LOG
           -------------------------------------------------------- */

        addLogEntry(
            'data',
            `DATA | Breathing: ${breathFreq.toFixed(3)} Hz | Signal: ${Math.max(
                breathMag,
                heartMag
            ).toFixed(1)}`
        );

        return;
    }


    /* ============================================================
       HUMAN DETECTION ALERT
       ============================================================ */

    if (
        msgType === 'ALERT' &&
        parts[2].toUpperCase() ===
            'HUMAN_DETECTED'
    ) {

        let espTimestamp = '—';

        if (
            parts[3] &&
            parts[3].startsWith('TIME:')
        ) {

            espTimestamp =
                parts[3].substring(5);
        }

        triggerRedAlert(
            espTimestamp
        );

        return;
    }
}


/* ================================================================
   CALIBRATION
   ================================================================ */

function showCalibrationOverlay() {

    calibrationActive = true;

    calibrationStartTime =
        Date.now();

    if (dom.calibrationOverlay) {

        dom.calibrationOverlay
            .classList.remove(
                'hidden'
            );
    }

    if (dom.dashboard) {

        dom.dashboard
            .classList.remove(
                'visible'
            );
    }

    if (
        dom.calibrationProgressBar
    ) {

        dom.calibrationProgressBar
            .style.width = '0%';
    }

    if (
        dom.calibrationTimer
    ) {

        dom.calibrationTimer.textContent =
            'ANALYZING NOISE FLOOR — 15s REMAINING';
    }

    clearInterval(
        calibrationInterval
    );

    calibrationInterval =
        setInterval(() => {

            const elapsedMs =
                Date.now() -
                calibrationStartTime;

            const elapsedS =
                Math.floor(
                    elapsedMs / 1000
                );

            const remainingS =
                Math.max(
                    0,
                    CALIBRATION_DURATION_S -
                    elapsedS
                );

            const progressPct =
                Math.min(
                    100,
                    (
                        elapsedMs /
                        (
                            CALIBRATION_DURATION_S *
                            1000
                        )
                    ) * 100
                );

            if (
                dom.calibrationProgressBar
            ) {

                dom.calibrationProgressBar
                    .style.width =
                    `${progressPct}%`;
            }

            if (
                dom.calibrationTimer
            ) {

                dom.calibrationTimer.textContent =
                    `ANALYZING NOISE FLOOR — ${remainingS}s REMAINING`;
            }

            if (
                remainingS <= 0
            ) {

                clearInterval(
                    calibrationInterval
                );

                calibrationInterval =
                    null;

                if (
                    dom.calibrationTimer
                ) {

                    dom.calibrationTimer.textContent =
                        'FINALIZING THRESHOLD...';
                }

                if (
                    dom.calibrationProgressBar
                ) {

                    dom.calibrationProgressBar
                        .style.width =
                        '100%';
                }
            }

        }, 500);
}


function hideCalibrationOverlay() {

    calibrationActive = false;

    calibrationStartTime = null;

    clearInterval(
        calibrationInterval
    );

    calibrationInterval = null;

    if (
        dom.calibrationOverlay
    ) {

        dom.calibrationOverlay
            .classList.add(
                'hidden'
            );
    }

    setTimeout(() => {

        if (dom.dashboard) {

            dom.dashboard
                .classList.add(
                    'visible'
                );
        }

    }, 400);
}


/* ================================================================
   RED ALERT
   ================================================================ */

function triggerRedAlert(
    espTimestamp
) {

    alertCount++;

    if (dom.statAlerts) {

        dom.statAlerts.textContent =
            alertCount;
    }

    const now =
        new Date();

    const timeStr =
        now.toLocaleTimeString(
            'en-US',
            {
                hour12: false
            }
        );

    if (dom.statLastAlert) {

        dom.statLastAlert.textContent =
            timeStr;
    }

    addLogEntry(
        'alert',
        `HUMAN DETECTED — ESP: ${espTimestamp} ms — Local: ${timeStr}`
    );

    playAlertTone();


    if (dom.statusDisplay) {

        dom.statusDisplay.className =
            'status-display red-alert';
    }

    if (dom.statusIcon) {

        dom.statusIcon.textContent =
            '⚠';
    }

    if (dom.statusLabel) {

        dom.statusLabel.textContent =
            'RED ALERT';
    }

    if (dom.statusSublabel) {

        dom.statusSublabel.textContent =
            'HUMAN PRESENCE DETECTED';
    }

    if (dom.vignette) {

        dom.vignette.classList.add(
            'active'
        );
    }


    clearTimeout(
        alertTimeout
    );

    alertTimeout =
        setTimeout(() => {

            if (dom.statusDisplay) {

                dom.statusDisplay.className =
                    'status-display standby';
            }

            if (dom.statusIcon) {

                dom.statusIcon.textContent =
                    '';
            }

            if (dom.statusLabel) {

                dom.statusLabel.textContent =
                    'MONITORING';
            }

            if (dom.statusSublabel) {

                dom.statusSublabel.textContent =
                    'System standing by';
            }

            if (dom.vignette) {

                dom.vignette.classList.remove(
                    'active'
                );
            }

        }, ALERT_DISPLAY_MS);
}


/* ================================================================
   CONNECTION STATUS
   ================================================================ */

function setConnectionStatus(
    online
) {

    if (!dom.connectionDot) {
        return;
    }

    if (online) {

        dom.connectionDot
            .classList.add(
                'online'
            );

        if (dom.connectionLabel) {

            dom.connectionLabel.textContent =
                'ONLINE';

            dom.connectionLabel.style.color =
                '#34d399';
        }

    } else {

        dom.connectionDot
            .classList.remove(
                'online'
            );

        if (dom.connectionLabel) {

            dom.connectionLabel.textContent =
                'OFFLINE';

            dom.connectionLabel.style.color =
                '#475569';
        }
    }
}


/* ================================================================
   EVENT LOG
   ================================================================ */

function addLogEntry(
    type,
    message
) {

    if (!dom.logContainer) {

        console.warn(
            'MachineSense event log element not found'
        );

        return;
    }


    /* Remove "Awaiting connection..." */

    const placeholder =
        dom.logContainer.querySelector(
            '.event-log__entry'
        );

    if (
        placeholder &&
        placeholder.textContent.includes(
            'Awaiting MachineSense connection'
        )
    ) {

        placeholder.remove();
    }


    logIndex++;


    const entry =
        document.createElement(
            'div'
        );

    entry.className =
        `event-log__entry log-entry--${type}`;


    const now =
        new Date();

    const ts =
        now.toLocaleTimeString(
            'en-US',
            {
                hour12: false,
                fractionalSecondDigits: 3
            }
        );


    entry.innerHTML = `

        <span class="event-log__time">
            ${ts}
        </span>

        <span class="event-log__message">
            <strong>#${String(logIndex).padStart(3, '0')}</strong>
            ${escapeHtml(message)}
        </span>

    `;


    dom.logContainer.appendChild(
        entry
    );


    /* Keep latest 100 entries */

    while (
        dom.logContainer.children.length >
        100
    ) {

        dom.logContainer.removeChild(
            dom.logContainer.firstChild
        );
    }


    dom.logContainer.scrollTop =
        dom.logContainer.scrollHeight;
}


/* ================================================================
   HTML ESCAPE
   ================================================================ */

function escapeHtml(
    str
) {

    const div =
        document.createElement(
            'div'
        );

    div.textContent =
        String(str);

    return div.innerHTML;
}


/* ================================================================
   UPTIME
   ================================================================ */

function updateUptime() {

    if (!uptimeStart) {
        return;
    }

    const elapsed =
        Math.floor(
            (
                Date.now() -
                uptimeStart
            ) / 1000
        );

    const h =
        String(
            Math.floor(
                elapsed / 3600
            )
        ).padStart(2, '0');

    const m =
        String(
            Math.floor(
                (elapsed % 3600) / 60
            )
        ).padStart(2, '0');

    const s =
        String(
            elapsed % 60
        ).padStart(2, '0');


    if (dom.uptime) {

        dom.uptime.textContent =
            `${h}:${m}:${s}`;
    }
}


/* ================================================================
   INITIALIZATION
   ================================================================ */

const connectButton =
    document.getElementById(
        'connect-btn'
    );

if (connectButton) {

    connectButton.addEventListener(
        'click',
        () => {

            /*
               Always use machinesense.local
               unless user explicitly enters
               another hostname/IP.
            */

            const input =
                document
                    .getElementById(
                        'ip-input'
                    );

            const inputVal =
                input
                    ? input.value.trim()
                    : '';


            const host =
                inputVal ||
                'machinesense.local';


            WEBSOCKET_URL =
                `ws://${host}:81`;


            const setupOverlay =
                document.getElementById(
                    'setup-overlay'
                );

            if (setupOverlay) {

                setupOverlay.classList.add(
                    'hidden'
                );
            }


            addLogEntry(
                'system',
                `MachineSense dashboard initialized — connecting to ${host}`
            );


            connect();
        }
    );
}


/* ================================================================
   ENTER KEY
   ================================================================ */

const ipInput =
    document.getElementById(
        'ip-input'
    );

if (ipInput) {

    ipInput.addEventListener(
        'keypress',
        (e) => {

            if (e.key === 'Enter') {

                if (connectButton) {

                    connectButton.click();
                }
            }
        }
    );
}


/* ================================================================
   AUTO CONNECT
   ================================================================ */

console.log(
    'MachineSense dashboard loaded'
);

console.log(
    'WebSocket target:',
    WEBSOCKET_URL
);
