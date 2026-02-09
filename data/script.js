// ========== WebSocket连接 ==========
let ws = null;
let isOnline = false;
let lastHeartbeatTime = 0;

// ========== 电机配置 ==========

let config = {
    motor: { speed: 240, duration: 0.1 },      // ✅ 改为和HTML一致
    servo1: { speed: 130, duration: 0.1 },     // ✅ 改为和HTML一致
    servo2: { speed: 130, duration: 0.1 },     // ✅ 改为和HTML一致
    servo3: { angle: 90, step: 30 },
    settingsExpanded: false
};

// ========== 长按检测 ==========
let longPressTimers = {};
let isLongPress = {};

// ========== 初始化 ==========
document.addEventListener('DOMContentLoaded', () => {
    loadSettings();
    initWebSocket();
    initControls();
    initSettings();
    startHeartbeatCheck();
});

// ========== localStorage 设置保存/加载 ==========
function saveSettings() {
    localStorage.setItem('motorControlSettings', JSON.stringify(config));
}

function loadSettings() {
    const saved = localStorage.getItem('motorControlSettings');
    if (saved) {
        try {
            const loaded = JSON.parse(saved);
            config = { ...config, ...loaded };
        } catch (e) {
            console.error('加载设置失败:', e);
        }
    }
}

// ========== WebSocket初始化 ==========
function initWebSocket() {
    const wsUrl = `ws://${window.location.hostname}:81`;
    console.log('连接WebSocket:', wsUrl);
    
    ws = new WebSocket(wsUrl);
    
    ws.onopen = () => {
        console.log('✓ WebSocket连接成功');
        isOnline = true;
        updateStatus('online');
    };
    
    ws.onmessage = (event) => {
        try {
            const data = JSON.parse(event.data);
            handleMessage(data);
        } catch (e) {
            console.error('解析消息失败:', e);
        }
    };
    
    ws.onerror = (error) => {
        console.error('WebSocket错误:', error);
    };
    
    ws.onclose = () => {
        console.log('✗ WebSocket断开');
        isOnline = false;
        updateStatus('offline');
        
        // 🔄 改进: 无限重试,不放弃
        reconnectWebSocket();
    };
}

// ========== 🆕 重连函数 (无限重试) ==========
function reconnectWebSocket() {
    // 清理旧连接
    if (ws) {
        ws.onclose = null; // 防止重复触发
        ws.close();
        ws = null;
    }
    
    console.log('3秒后尝试重连...');
    setTimeout(() => {
        console.log('正在重连WebSocket...');
        initWebSocket();
    }, 3000);
}

// ========== 处理接收到的消息 ==========
function handleMessage(data) {
    if (data.type === 'heartbeat') {
        lastHeartbeatTime = Date.now();
        
        if (data.rssi !== undefined) {
            updateSignal(data.rssi);
        }
        
        if (data.uptime !== undefined) {
            updateUptime(data.uptime);
        }
    }
}

// ========== 心跳检测 ==========
function startHeartbeatCheck() {
    setInterval(() => {
        if (isOnline) {
            const timeSinceLastBeat = Date.now() - lastHeartbeatTime;
            // ⚠️ 改进: 从3秒改为10秒
            if (timeSinceLastBeat > 10000) {
                console.warn('超过10秒未收到心跳,判定为离线');
                isOnline = false;
                updateStatus('offline');
            }
        }
    }, 1000);
}

// ========== 更新状态显示 ==========
function updateStatus(status) {
    const indicator = document.getElementById('statusIndicator');
    
    if (status === 'online') {
        indicator.textContent = '🟢 在线';
        indicator.className = 'status-indicator status-online';
        enableAllControls();
    } else {
        indicator.textContent = '🔴 离线';
        indicator.className = 'status-indicator status-offline';
        document.getElementById('signalText').textContent = '--';
        document.getElementById('uptimeText').textContent = '00:00:00';
        disableAllControls();
    }
}

function updateSignal(rssi) {
    const signalEl = document.getElementById('signalText');
    let bars = '';
    if (rssi > -50) bars = '████';
    else if (rssi > -60) bars = '███░';
    else if (rssi > -70) bars = '██░░';
    else bars = '█░░░';
    
    signalEl.textContent = bars + ' ' + rssi + 'dBm';
}

function updateUptime(seconds) {
    const hours = Math.floor(seconds / 3600);
    const minutes = Math.floor((seconds % 3600) / 60);
    const secs = seconds % 60;
    const uptimeStr = String(hours).padStart(2, '0') + ':' +
                      String(minutes).padStart(2, '0') + ':' +
                      String(secs).padStart(2, '0');
    
    document.getElementById('uptimeText').textContent = uptimeStr;
}

// ========== 启用/禁用控制 ==========
function enableAllControls() {
    document.querySelectorAll('button, input[type="range"]').forEach(el => {
        el.disabled = false;
    });
}

function disableAllControls() {
    document.querySelectorAll('button, input[type="range"]').forEach(el => {
        el.disabled = true;
    });
}

// ========== 发送指令 ==========
function sendCommand(cmd) {
    if (ws && ws.readyState === WebSocket.OPEN) {
        ws.send(JSON.stringify(cmd));
    } else {
        console.error('WebSocket未连接');
    }
}

// ========== 初始化控制按钮 ==========
function initControls() {
    // TT电机
    setupMotorButtons('motor', 'motorForward', 'motorReverse', 'motorStop', true);
    
    // 360度舵机1
    setupMotorButtons('servo1', 'servo1Forward', 'servo1Reverse', 'servo1Stop', false);
    
    // 360度舵机2
    setupMotorButtons('servo2', 'servo2Forward', 'servo2Reverse', 'servo2Stop', false);
    
    // 180度舵机
    setupServo180Controls();
}

// ========== 设置电机按钮 ==========
function setupMotorButtons(motorName, forwardId, reverseId, stopId, isTTMotor) {
    const forwardBtn = document.getElementById(forwardId);
    const reverseBtn = document.getElementById(reverseId);
    const stopBtn = document.getElementById(stopId);
    
    setupLongPressButton(forwardBtn, motorName, 'forward', isTTMotor);
    setupLongPressButton(reverseBtn, motorName, 'reverse', isTTMotor);
    
    stopBtn.addEventListener('click', () => {
        sendCommand({ type: 'stop', motor: motorName });
        updateMotorStatus(motorName, '停止');
    });
}

// ========== 设置长按按钮 ==========
function setupLongPressButton(btn, motorName, direction, isTTMotor) {
    const key = motorName + direction;
    
    const startPress = (e) => {
        e.preventDefault();
        isLongPress[key] = false;
        
        longPressTimers[key] = setTimeout(() => {
            isLongPress[key] = true;
            handleLongPress(motorName, direction, isTTMotor);
        }, 300);
    };
    
    const endPress = (e) => {
        e.preventDefault();
        clearTimeout(longPressTimers[key]);
        
        if (isLongPress[key]) {
            sendCommand({ type: 'stop', motor: motorName });
            updateMotorStatus(motorName, '停止');
        } else {
            handleShortPress(motorName, direction, isTTMotor);
        }
    };
    
    btn.addEventListener('mousedown', startPress);
    btn.addEventListener('mouseup', endPress);
    btn.addEventListener('mouseleave', endPress);
    btn.addEventListener('touchstart', startPress);
    btn.addEventListener('touchend', endPress);
    btn.addEventListener('touchcancel', endPress);
}

// ========== 处理短按 ==========
function handleShortPress(motorName, direction, isTTMotor) {
    const cfg = config[motorName];
    let speed;
    
    if (isTTMotor) {
        speed = cfg.speed;
    } else {
        speed = direction === 'forward' ? cfg.speed : (180 - cfg.speed);
    }
    
    const cmd = {
        type: 'run_duration',
        motor: motorName,
        speed: speed,
        duration: cfg.duration * 1000
    };
    
    if (isTTMotor) {
        cmd.forward = (direction === 'forward');
    }
    
    sendCommand(cmd);
    
    const dirText = getDirectionText(motorName, direction);
    updateMotorStatus(motorName, dirText + ' ' + cfg.duration.toFixed(1) + 's');
    
    setTimeout(() => {
        updateMotorStatus(motorName, '停止');
    }, cfg.duration * 1000);
}

// ========== 处理长按 ==========
function handleLongPress(motorName, direction, isTTMotor) {
    const cfg = config[motorName];
    let speed;
    
    if (isTTMotor) {
        speed = cfg.speed;
    } else {
        speed = direction === 'forward' ? cfg.speed : (180 - cfg.speed);
    }
    
    const cmd = {
        type: 'start_continuous',
        motor: motorName,
        speed: speed
    };
    
    if (isTTMotor) {
        cmd.forward = (direction === 'forward');
    }
    
    sendCommand(cmd);
    
    const dirText = getDirectionText(motorName, direction);
    updateMotorStatus(motorName, '持续' + dirText);
}

// ========== 获取方向文本 ==========
function getDirectionText(motorName, direction) {
    if (motorName === 'motor') {
        return direction === 'forward' ? '正转' : '反转';
    } else {
        return direction === 'forward' ? '顺转' : '逆转';
    }
}

// ========== 更新电机状态显示 ==========
function updateMotorStatus(motorName, status) {
    document.getElementById(motorName + 'Status').textContent = status;
}

// ========== 设置180度舵机控制 ==========
function setupServo180Controls() {
    const leftBtn = document.getElementById('servo3Left');
    const centerBtn = document.getElementById('servo3Center');
    const rightBtn = document.getElementById('servo3Right');
    
    leftBtn.addEventListener('click', () => {
        config.servo3.angle = Math.max(0, config.servo3.angle - config.servo3.step);
        sendServo3Command();
        saveSettings();
    });
    
    centerBtn.addEventListener('click', () => {
        config.servo3.angle = 90;
        sendServo3Command();
        saveSettings();
    });
    
    rightBtn.addEventListener('click', () => {
        config.servo3.angle = Math.min(180, config.servo3.angle + config.servo3.step);
        sendServo3Command();
        saveSettings();
    });
}

function sendServo3Command() {
    sendCommand({
        type: 'servo180',
        angle: config.servo3.angle
    });
    document.getElementById('servo3Status').textContent = config.servo3.angle + '°';
    document.getElementById('servo3AngleValue').textContent = config.servo3.angle;
    document.getElementById('servo3Angle').value = config.servo3.angle;
}

// ========== 初始化设置面板 ==========
function initSettings() {
    // 折叠面板切换
    const settingsToggle = document.getElementById('settingsToggle');
    const settingsContent = document.getElementById('settingsContent');
    const settingsArrow = document.getElementById('settingsArrow');
    
    // 恢复折叠状态
    if (config.settingsExpanded) {
        settingsContent.classList.add('expanded');
        settingsArrow.classList.add('expanded');
    }
    
    settingsToggle.addEventListener('click', () => {
        settingsContent.classList.toggle('expanded');
        settingsArrow.classList.toggle('expanded');
        config.settingsExpanded = settingsContent.classList.contains('expanded');
        saveSettings();
    });
    
    // TT电机设置
    setupRangeSetting('motorSpeed', 'motorSpeedValue', 'motor', 'speed');
    setupRangeSetting('motorDuration', 'motorDurationValue', 'motor', 'duration', 1);
    
    // 舵机1设置
    setupRangeSetting('servo1Speed', 'servo1SpeedValue', 'servo1', 'speed');
    setupRangeSetting('servo1Duration', 'servo1DurationValue', 'servo1', 'duration', 1);
    
    // 舵机2设置
    setupRangeSetting('servo2Speed', 'servo2SpeedValue', 'servo2', 'speed');
    setupRangeSetting('servo2Duration', 'servo2DurationValue', 'servo2', 'duration', 1);
    
    // 舵机3设置
    setupRangeSetting('servo3Angle', 'servo3AngleValue', 'servo3', 'angle', 0, '°');
    setupRangeSetting('servo3Step', 'servo3StepValue', 'servo3', 'step', 0, '°');
    
    // 恢复所有设置值
    restoreAllSettings();
}

// ========== 设置范围滑块 ==========
function setupRangeSetting(sliderId, valueId, configKey, property, decimals = 0, suffix = '') {
    const slider = document.getElementById(sliderId);
    const valueDisplay = document.getElementById(valueId);
    
    slider.addEventListener('input', (e) => {
        const value = parseFloat(e.target.value);
        config[configKey][property] = value;
        valueDisplay.textContent = decimals > 0 ? value.toFixed(decimals) : value;
        saveSettings();
    });
}

// ========== 恢复所有设置 ==========
function restoreAllSettings() {
    // TT电机
    document.getElementById('motorSpeed').value = config.motor.speed;
    document.getElementById('motorSpeedValue').textContent = config.motor.speed;
    document.getElementById('motorDuration').value = config.motor.duration;
    document.getElementById('motorDurationValue').textContent = config.motor.duration.toFixed(1);
    
    // 舵机1
    document.getElementById('servo1Speed').value = config.servo1.speed;
    document.getElementById('servo1SpeedValue').textContent = config.servo1.speed;
    document.getElementById('servo1Duration').value = config.servo1.duration;
    document.getElementById('servo1DurationValue').textContent = config.servo1.duration.toFixed(1);
    
    // 舵机2
    document.getElementById('servo2Speed').value = config.servo2.speed;
    document.getElementById('servo2SpeedValue').textContent = config.servo2.speed;
    document.getElementById('servo2Duration').value = config.servo2.duration;
    document.getElementById('servo2DurationValue').textContent = config.servo2.duration.toFixed(1);
    
    // 舵机3
    document.getElementById('servo3Angle').value = config.servo3.angle;
    document.getElementById('servo3AngleValue').textContent = config.servo3.angle;
    document.getElementById('servo3Step').value = config.servo3.step;
    document.getElementById('servo3StepValue').textContent = config.servo3.step;
    document.getElementById('servo3Status').textContent = config.servo3.angle + '°';
}