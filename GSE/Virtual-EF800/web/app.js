(() => {
  const WIDTH = 240, HEIGHT = 64, FRAME_SIZE = WIDTH * HEIGHT / 8;
  const canvas = document.querySelector('#lcd');
  const context = canvas.getContext('2d');
  const status = document.querySelector('#status');
  const text = document.querySelector('#text');
  const logs = document.querySelector('#logs');
  const logsStatus = document.querySelector('#logs-status');
  const logsPause = document.querySelector('#logs-pause');
  const logsClear = document.querySelector('#logs-clear');
  const logsSeverity = document.querySelector('#logs-severity');
  const logsAutoscroll = document.querySelector('#logs-autoscroll');
  const acquire = document.querySelector('#control');
  const release = document.querySelector('#release');
  const buttons = [...document.querySelectorAll('[data-key]')];
  const pluginFile = document.querySelector('#plugin-file');
  const pluginUpload = document.querySelector('#plugin-upload');
  const simulatorRestart = document.querySelector('#simulator-restart');
  const pluginStatus = document.querySelector('#plugin-status');
  const pluginId = document.querySelector('#plugin-id');
  const pluginPort = document.querySelector('#plugin-port');
  const pluginAssign = document.querySelector('#plugin-assign');
  const pluginExecute = document.querySelector('#plugin-execute');
  const deviceDockButtons = [...document.querySelectorAll('[data-dock-port]')];
  const deviceDelay = document.querySelector('#device-delay');
  const deviceResponse = document.querySelector('#device-response');
  const deviceAvailable = document.querySelector('#device-available');
  const deviceStatus = document.querySelector('#device-status');
  let socket, controller = false, simulator = false, reconnectTimer;
  const held = new Set();
  const MAX_LOG_RECORDS = 64;
  const logRecords = [];
  let logsPaused = false, lastLogSequence = null;

  function setStatus(message) { status.textContent = message; }
  function send(message) {
    if (socket && socket.readyState === WebSocket.OPEN)
      { socket.send(JSON.stringify({version: 1, ...message})); return true; }
    return false;
  }
  function renderPixels(hex) {
    if (!hex || hex.length !== FRAME_SIZE * 2) return;
    const pixels = new Uint8Array(FRAME_SIZE);
    for (let i = 0; i < FRAME_SIZE; i++) pixels[i] = parseInt(hex.slice(i * 2, i * 2 + 2), 16);
    const image = context.createImageData(WIDTH, HEIGHT);
    for (let y = 0; y < HEIGHT; y++) for (let x = 0; x < WIDTH; x++) {
      const on = pixels[y * 30 + Math.floor(x / 8)] & (1 << (x % 8));
      const index = (y * WIDTH + x) * 4;
      image.data[index] = image.data[index + 1] = image.data[index + 2] = on ? 17 : 255;
      image.data[index + 3] = 255;
    }
    context.putImageData(image, 0, 0);
  }
  function renderText(rows) {
    context.save();
    context.fillStyle = '#111111';
    context.font = '8px monospace';
    context.textBaseline = 'top';
    (rows || []).slice(0, 8).forEach((row, index) => {
      const value = String(row || '').replace(/\0/g, '');
      context.fillText(value.slice(0, 40), 0, index * 8);
    });
    context.restore();
  }
  function updateControls() {
    acquire.disabled = controller;
    release.disabled = !controller;
    buttons.forEach(button => button.disabled = !controller);
    pluginUpload.disabled = !simulator;
    simulatorRestart.disabled = !simulator;
    pluginAssign.disabled = !simulator;
    pluginExecute.disabled = !simulator;
  }
  function setKeyHeld(key, isHeld) {
    const button = buttons.find(candidate => candidate.dataset.key === key);
    if (!button) return;
    button.classList.toggle('held', isHeld);
    button.setAttribute('aria-pressed', String(isHeld));
  }
  function severityRank(value) { return {DEBUG: 0, INFO: 1, WARNING: 2, ERROR: 3, PANIC: 4}[value] ?? 0; }
  function renderLogs() {
    if (logsPaused) return;
    const minimum = logsSeverity.value === 'ALL' ? 0 : severityRank(logsSeverity.value);
    logs.textContent = logRecords.filter(record => record.gap || severityRank(record.severity) >= minimum)
      .map(record => record.gap ? `--- ${record.text} ---` :
        `[${record.severity}]${record.replay ? ' [history]' : ''} #${record.sequence} ${record.text}`)
      .join('\n');
    logsStatus.textContent = `${logRecords.length} recent record(s)${logsPaused ? ' (paused)' : ''}`;
    if (logsAutoscroll.checked) logs.scrollTop = logs.scrollHeight;
  }
  function appendLog(record) {
    if (record.flags & 8) {
      logRecords.length = 0;
      lastLogSequence = null;
    }
    if (lastLogSequence !== null && record.sequence > lastLogSequence + 1) {
      logRecords.push({gap: true, text: `diagnostic gap: records ${lastLogSequence + 1}-${record.sequence - 1} unavailable`});
    }
    if (record.flags & 4) logRecords.push({gap: true, text: 'diagnostic records were dropped or evicted'});
    logRecords.push(record);
    lastLogSequence = record.sequence;
    while (logRecords.length > MAX_LOG_RECORDS) logRecords.shift();
    renderLogs();
  }
  async function uploadPlugin() {
    if (!pluginFile.files.length) { pluginStatus.textContent = 'Select a .PMP package first.'; return; }
    const form = new FormData(); form.append('file', pluginFile.files[0]);
    pluginStatus.textContent = 'Uploading and validating...';
    try {
      const response = await fetch('/api/plugins', {method: 'POST', body: form});
      const result = await response.json();
      pluginStatus.textContent = result.accepted ? 'Installed. Restart required.' : `Rejected: ${result.error || result.code || 'unknown error'}`;
    } catch (error) { pluginStatus.textContent = `Upload failed: ${error}`; }
  }
  async function restartSimulator() {
    pluginStatus.textContent = 'Restarting simulator...';
    try {
      const response = await fetch('/api/simulator/restart', {method: 'POST'});
      const result = await response.json();
      pluginStatus.textContent = result.accepted ? 'Restart requested; waiting for rediscovery.' : 'Restart rejected.';
    } catch (error) { pluginStatus.textContent = `Restart failed: ${error}`; }
  }
  async function assignPlugin() {
    pluginStatus.textContent = 'Assigning plugin through MainApplication...';
    try {
      const response = await fetch(`/api/plugins/${Number(pluginId.value)}/assign`, {
        method: 'POST', headers: {'Content-Type': 'application/json'},
        body: JSON.stringify({port: Number(pluginPort.value), connection: 1})});
      const result = await response.json();
      pluginStatus.textContent = result.accepted ? `Plugin assigned to port ${pluginPort.value}.` : `Assignment rejected: ${result.error}`;
    } catch (error) { pluginStatus.textContent = `Assignment failed: ${error}`; }
  }
  async function executePlugin() {
    pluginStatus.textContent = 'Running measurement plugin...';
    try {
      const response = await fetch(`/api/plugins/execute/${Number(pluginPort.value)}`, {method: 'POST'});
      const result = await response.json();
      pluginStatus.textContent = result.accepted ? 'Measurement completed; see LCD.' : `Measurement failed: ${result.error}`;
    } catch (error) { pluginStatus.textContent = `Execution failed: ${error}`; }
  }
  async function dockDevice(port) {
    const deviceProfile = document.querySelector(`#device-profile-${port}`);
    deviceStatus.textContent = `Applying virtual device profile to Port ${port}...`;
    try {
      const response = await fetch(`/api/devices/${port}`, {
        method: 'POST', headers: {'Content-Type': 'application/json'},
        body: JSON.stringify({available: deviceAvailable.checked,
          delay_ms: Number(deviceDelay.value), response_hex: deviceResponse.value,
          profile: Number(deviceProfile.value)})
      });
      const result = await response.json();
      deviceStatus.textContent = result.accepted ? `Port ${port} docked.` : `Rejected: ${result.error || 'unknown error'}`;
    } catch (error) { deviceStatus.textContent = `Dock failed: ${error}`; }
  }
  function connect() {
    socket = new WebSocket(`${location.protocol === 'https:' ? 'wss' : 'ws'}://${location.host}/ws`);
    socket.onopen = () => { setStatus('Connected to web gateway; viewer mode'); updateControls(); };
    socket.onmessage = event => {
      let message; try { message = JSON.parse(event.data); } catch (_) { return; }
      if (message.type === 'status') {
        simulator = !!message.simulatorConnected;
        if (!message.controller || !message.simulatorConnected) clearHeld();
        controller = !!message.controller && controller;
         setStatus(`${simulator ? 'Simulator connected' : 'Simulator disconnected'} - ${controller ? 'controller' : 'viewer'}${message.stale ? ' (display stale)' : ''}${message.capabilities ? ` - ${message.capabilities}` : ''}`);
        updateControls();
       } else if (message.type === 'lcd') {
        renderPixels(message.pixels);
         text.textContent = (message.text || []).join('\n');
      } else if (message.type === 'log') {
        appendLog(message);
      } else if (message.type === 'log-epoch') {
        logRecords.length = 0;
        lastLogSequence = null;
        logsStatus.textContent = `Diagnostic epoch ${message.epoch}; waiting for records.`;
      } else if (message.type === 'plugin') {
        const result = message.result || {};
        pluginStatus.textContent = message.state === 'installed' ?
          'Installed. Restart required.' :
          (message.state === 'uploading' ? 'Uploading and validating...' : `Plugin rejected: ${result.error || 'unknown error'}`);
      } else if (message.type === 'plugin-status') {
        pluginStatus.textContent = message.accepted ?
          `Simulator registry: ${message.count} package(s).` :
          `Plugin registry error: ${message.error}`;
      } else if (message.type === 'ack' && message.command === 'acquire') {
        controller = !!message.accepted; updateControls();
      } else if (message.type === 'ack' &&
                 (message.command === 'key-down' || message.command === 'key-up')) {
        const key = message.key;
        if (message.command === 'key-down') pendingDown.delete(key);
        else pendingUp.delete(key);
        if (message.accepted) {
          if (message.command === 'key-down') held.add(key);
          else held.delete(key);
          setKeyHeld(key, message.command === 'key-down');
        }
      }
    };
    socket.onclose = () => {
      controller = false; simulator = false; clearHeld(); updateControls();
      setStatus('Disconnected; retrying...'); clearTimeout(reconnectTimer); reconnectTimer = setTimeout(connect, 1000);
    };
  }
  acquire.onclick = () => send({command: 'acquire'});
  release.onclick = () => { send({command: 'release'}); controller = false; held.clear(); updateControls(); };
  pluginUpload.onclick = uploadPlugin;
  simulatorRestart.onclick = restartSimulator;
  pluginAssign.onclick = assignPlugin;
  pluginExecute.onclick = executePlugin;
  deviceDockButtons.forEach(button => {
    button.onclick = () => dockDevice(Number(button.dataset.dockPort));
  });
  logsPause.onclick = () => {
    logsPaused = !logsPaused;
    logsPause.textContent = logsPaused ? 'Resume' : 'Pause';
    renderLogs();
  };
  logsClear.onclick = () => { logRecords.length = 0; lastLogSequence = null; renderLogs(); };
  logsSeverity.onchange = renderLogs;
  logsAutoscroll.onchange = renderLogs;
  const pendingDown = new Set(), pendingUp = new Set();
  function clearHeld() {
    held.clear(); pendingDown.clear(); pendingUp.clear();
    buttons.forEach(button => {
      button.classList.remove('held');
      button.setAttribute('aria-pressed', 'false');
    });
  }
  function keyDown(key) {
    if (!controller || held.has(key) || pendingDown.has(key)) return;
    if (send({command: 'key-down', key})) pendingDown.add(key);
  }
  function keyUp(key) {
    if (!held.has(key) && !pendingDown.has(key)) return;
    if (send({command: 'key-up', key})) pendingUp.add(key);
  }
  buttons.forEach(button => {
    const key = button.dataset.key;
    button.addEventListener('pointerdown', event => { event.preventDefault(); keyDown(key); });
    button.addEventListener('pointerup', event => { event.preventDefault(); keyUp(key); });
    button.addEventListener('pointercancel', () => keyUp(key));
    button.addEventListener('pointerleave', () => keyUp(key));
  });
  const keys = {
    ArrowUp: 'up', ArrowDown: 'down', ArrowLeft: 'left', ArrowRight: 'right',
    Enter: 'enter', Escape: 'escape', Backspace: 'backspace', Delete: 'delete',
    Insert: 'insert', Tab: 'tab', Shift: 'shift', Control: 'ctrl', Alt: 'alt',
    CapsLock: 'caps', ' ': 'space', ',': 'comma', '.': 'period', '-': 'minus',
    '+': 'plus', '<': 'less', '^': 'caret'
  };
  function keyForEvent(event) {
    if (keys[event.key]) return keys[event.key];
    if (event.key.length === 1 && /^[a-z0-9]$/i.test(event.key)) return event.key.toLowerCase();
    return null;
  }
  window.addEventListener('keydown', event => {
    const key = keyForEvent(event);
    if (key) { event.preventDefault(); keyDown(key); }
  });
  window.addEventListener('keyup', event => {
    const key = keyForEvent(event);
    if (key) { event.preventDefault(); keyUp(key); }
  });
  function releaseHeld() {
    if (controller) send({command: 'release-all'});
    clearHeld();
  }
  window.addEventListener('blur', releaseHeld);
  window.addEventListener('beforeunload', releaseHeld);
  updateControls(); connect();
})();
