(() => {
  const WIDTH = 240, HEIGHT = 64, FRAME_SIZE = WIDTH * HEIGHT / 8;
  const canvas = document.querySelector('#lcd');
  const context = canvas.getContext('2d');
  const status = document.querySelector('#status');
  const logs = document.querySelector('#logs');
  const logsStatus = document.querySelector('#logs-status');
  const logsPause = document.querySelector('#logs-pause');
  const logsClear = document.querySelector('#logs-clear');
  const logsSeverity = document.querySelector('#logs-severity');
  const logsAutoscroll = document.querySelector('#logs-autoscroll');
  const compositionGroups = document.querySelector('#composition-groups');
  const compositionStatus = document.querySelector('#composition-status');
  const acquire = document.querySelector('#control');
  const release = document.querySelector('#release');
  const buttons = [...document.querySelectorAll('[data-key]')];
  const mtpStorage = document.querySelector('#mtp-storage');
  const mtpRefresh = document.querySelector('#mtp-refresh');
  const mtpUp = document.querySelector('#mtp-up');
  const mtpPathLabel = document.querySelector('#mtp-path');
  const mtpList = document.querySelector('#mtp-list');
  const mtpDropzone = document.querySelector('#mtp-dropzone');
  const mtpFile = document.querySelector('#mtp-file');
  const mtpUpload = document.querySelector('#mtp-upload');
  const mtpProgress = document.querySelector('#mtp-progress');
  const mtpCancel = document.querySelector('#mtp-cancel');
  const mtpStatus = document.querySelector('#mtp-status');
  const deviceDockButtons = [...document.querySelectorAll('[data-dock-port]')];
  const deviceDelay = document.querySelector('#device-delay');
  const deviceResponse = document.querySelector('#device-response');
  const deviceAvailable = document.querySelector('#device-available');
  const deviceStatus = document.querySelector('#device-status');
  const batterySocInput = document.querySelector('#battery-soc');
  const batterySocApply = document.querySelector('#battery-soc-apply');
  const batterySocStatus = document.querySelector('#battery-soc-status');
  let socket, controller = false, simulator = false, reconnectTimer;
  let mtpCurrentPath = '', mtpBusy = false, mtpAbortController = null;
  let mtpActiveRequestId = null;
  let compositionState = null, compositionBusy = false;
  let batterySocBusy = false, batterySocSource = null;
  let compositionApplyButtons = [];
  let lcdContrastEnabled = false, lcdContrastDuty = 78, lastLcdPixels = null;
  const LCD_BACKGROUND = [106, 119, 88];
  const LCD_DARK_INK = [16, 22, 14];
  const LCD_DEEP_INK = [4, 7, 4];
  const held = new Set();
  const MAX_LOG_RECORDS = 64;
  const logRecords = [];
  let logsPaused = false, lastLogSequence = null;

  function setStatus(message) { status.textContent = message; }
  function lcdInkColor() {
    if (!lcdContrastEnabled) return LCD_DARK_INK;
    let from, to, amount;
    if (lcdContrastDuty <= 78) {
      from = LCD_BACKGROUND;
      to = LCD_DARK_INK;
      amount = lcdContrastDuty / 78;
    } else {
      from = LCD_DARK_INK;
      to = LCD_DEEP_INK;
      amount = (lcdContrastDuty - 78) / 22;
    }
    return from.map((channel, index) =>
      Math.round(channel + (to[index] - channel) * amount));
  }
  function applyLcdContrast(enabled, duty) {
    if (typeof enabled !== 'boolean' || !Number.isInteger(duty) || duty < 0 || duty > 100)
      return;
    lcdContrastEnabled = enabled;
    lcdContrastDuty = duty;
    if (lastLcdPixels !== null) renderPixels(lastLcdPixels);
  }
  function send(message) {
    if (socket && socket.readyState === WebSocket.OPEN)
      { socket.send(JSON.stringify({version: 1, ...message})); return true; }
    return false;
  }
  function renderPixels(hex) {
    if (!hex || hex.length !== FRAME_SIZE * 2) return;
    lastLcdPixels = hex;
    const pixels = new Uint8Array(FRAME_SIZE);
    for (let i = 0; i < FRAME_SIZE; i++) pixels[i] = parseInt(hex.slice(i * 2, i * 2 + 2), 16);
    const image = context.createImageData(WIDTH, HEIGHT);
    const ink = lcdInkColor();
    for (let y = 0; y < HEIGHT; y++) for (let x = 0; x < WIDTH; x++) {
      const on = pixels[y * 30 + Math.floor(x / 8)] & (1 << (x % 8));
      const index = (y * WIDTH + x) * 4;
      const color = on ? ink : LCD_BACKGROUND;
      image.data[index] = color[0];
      image.data[index + 1] = color[1];
      image.data[index + 2] = color[2];
      image.data[index + 3] = 255;
    }
    context.putImageData(image, 0, 0);
  }
  function updateControls() {
    acquire.disabled = controller;
    release.disabled = !controller;
    buttons.forEach(button => button.disabled = !controller);
    mtpStorage.disabled = !simulator || mtpBusy;
    mtpRefresh.disabled = !simulator || mtpBusy;
    mtpUp.disabled = !simulator || mtpBusy || !mtpCurrentPath;
    mtpFile.disabled = !simulator || mtpBusy;
    mtpUpload.disabled = !simulator || mtpBusy || !mtpFile.files.length;
    mtpCancel.disabled = !mtpBusy || !mtpAbortController;
    compositionApplyButtons.forEach(({button, select, selected}) => {
      button.disabled = !controller || !simulator || compositionBusy ||
        !compositionState || !compositionState.available ||
        compositionState.restartRequired || select.value === selected;
      select.disabled = !controller || !simulator || compositionBusy ||
        !compositionState || !compositionState.available ||
        compositionState.restartRequired;
    });
    const batteryDisabled = !controller || !simulator || batterySocBusy ||
      batterySocSource === null || batterySocSource === 'BATSOC0';
    batterySocInput.disabled = batteryDisabled;
    batterySocApply.disabled = batteryDisabled;
  }
  function renderComposition(state) {
    compositionState = state;
    compositionGroups.replaceChildren();
    compositionApplyButtons = [];
    if (!state || !state.available) {
      compositionStatus.textContent = 'Composition catalog unavailable.';
      updateControls();
      return;
    }
    for (const group of state.groups) {
      const row = document.createElement('div');
      row.className = 'composition-group';
      const label = document.createElement('label');
      label.textContent = group.id;
      const select = document.createElement('select');
      select.setAttribute('aria-label', `${group.id} composition selection`);
      for (const choice of group.options) {
        const option = document.createElement('option');
        option.value = choice.token;
        option.textContent = `${choice.token}${choice.default ? ' (default)' : ''}`;
        select.append(option);
      }
      select.value = group.selected;
      const active = document.createElement('span');
      active.className = 'composition-active';
      active.textContent = `Active now: ${group.active}`;
      const apply = document.createElement('button');
      apply.type = 'button';
      apply.textContent = 'Apply';
      select.addEventListener('change', updateControls);
      apply.addEventListener('click', () => {
        if (!controller || compositionBusy || select.value === group.selected) return;
        compositionBusy = true;
        compositionStatus.textContent = `Committing ${group.id}=${select.value}...`;
        updateControls();
        if (!send({command: 'composition-set', group: group.id, token: select.value})) {
          compositionBusy = false;
          compositionStatus.textContent = 'Command could not be sent.';
          updateControls();
        }
      });
      row.append(label, select, active, apply);
      compositionGroups.append(row);
      compositionApplyButtons.push({button: apply, select, selected: group.selected});
    }
    compositionStatus.textContent = state.restartRequired ?
      'Composition committed. Restart the simulator to activate the selected items.' :
      'Changes are committed through the profile service; they take effect after restart.';
    updateControls();
  }
  function renderBatterySoc(message) {
    if (!Number.isInteger(message.socPercent) ||
        message.socPercent < 0 || message.socPercent > 100) return;
    batterySocInput.value = String(message.socPercent);
    batterySocSource = typeof message.source === 'string' ? message.source : null;
    batterySocStatus.textContent = batterySocSource && batterySocSource !== 'BATSOC0' ?
      `Virtual battery input: ${message.socPercent}% (${batterySocSource}).` :
      'No battery SOC source is active.';
    updateControls();
  }
  function applyBatterySoc() {
    const socPercent = batterySocInput.valueAsNumber;
    if (!Number.isInteger(socPercent) || socPercent < 0 || socPercent > 100) {
      batterySocStatus.textContent = 'Enter an integer from 0 to 100.';
      return;
    }
    batterySocBusy = true;
    batterySocStatus.textContent = `Applying virtual battery SOC ${socPercent}%...`;
    updateControls();
    if (!send({command: 'battery-soc-set', socPercent})) {
      batterySocBusy = false;
      batterySocStatus.textContent = 'Simulator is disconnected.';
      updateControls();
    }
  }
  function setMtpStatus(message) { mtpStatus.textContent = message; }
  function setMtpBusy(busy) {
    mtpBusy = busy;
    if (!busy) mtpAbortController = null;
    updateControls();
  }
  function updateMtpProgress(transferred, total) {
    mtpProgress.max = Math.max(1, total || 1);
    mtpProgress.value = Math.min(transferred || 0, mtpProgress.max);
    mtpProgress.setAttribute('aria-label', `${transferred || 0} of ${total || 0} bytes transferred`);
  }
  function mtpUrl(path) {
    const query = new URLSearchParams({storage: mtpStorage.value, path});
    return `/api/mtp/list?${query}`;
  }
  async function refreshMtpDirectory() {
    if (!simulator || mtpBusy) return;
    setMtpBusy(true);
    setMtpStatus('Loading MTP directory through virtual USB...');
    mtpList.replaceChildren();
    try {
      const response = await fetch(mtpUrl(mtpCurrentPath));
      const result = await response.json();
      if (!response.ok) throw new Error(result.error || `HTTP ${response.status}`);
      renderMtpEntries(result.entries || []);
      mtpPathLabel.textContent = `/${result.storage}${result.path ? `/${result.path}` : ''}`;
      setMtpStatus(`${result.entries.length} item(s) in ${result.storage}.`);
    } catch (error) {
      setMtpStatus(`MTP listing failed: ${error.message || error}`);
    } finally { setMtpBusy(false); }
  }
  function renderMtpEntries(entries) {
    mtpList.replaceChildren();
    for (const entry of entries) {
      const item = document.createElement('li');
      const name = document.createElement('button');
      name.className = 'mtp-entry';
      name.type = 'button';
      name.textContent = entry.directory ? `📁 ${entry.name}` : entry.name;
      if (entry.directory) {
        name.onclick = () => {
          mtpCurrentPath = mtpCurrentPath ? `${mtpCurrentPath}/${entry.name}` : entry.name;
          updateControls(); refreshMtpDirectory();
        };
      } else {
        name.disabled = !simulator || mtpBusy;
        name.onclick = () => downloadMtpFile(entry.name);
        const size = document.createElement('span');
        size.className = 'mtp-size';
        size.textContent = `${entry.size} bytes`;
        item.append(name, size);
        mtpList.append(item);
        continue;
      }
      item.append(name);
      mtpList.append(item);
    }
  }
  async function downloadMtpFile(name) {
    if (mtpBusy) return;
    const path = mtpCurrentPath ? `${mtpCurrentPath}/${name}` : name;
    const query = new URLSearchParams({storage: mtpStorage.value, path});
    mtpAbortController = new AbortController();
    setMtpBusy(true);
    updateMtpProgress(0, 1);
    setMtpStatus(`Downloading ${name} through MTP...`);
    try {
      const response = await fetch(`/api/mtp/download?${query}`, {signal: mtpAbortController.signal});
      if (!response.ok) {
        const result = await response.json();
        throw new Error(result.error || `HTTP ${response.status}`);
      }
      const blob = await response.blob();
      const link = document.createElement('a');
      link.href = URL.createObjectURL(blob);
      link.download = name;
      link.click();
      setTimeout(() => URL.revokeObjectURL(link.href), 1000);
      updateMtpProgress(blob.size, blob.size);
      setMtpStatus(`Downloaded ${name} (${blob.size} bytes).`);
    } catch (error) {
      setMtpStatus(error.name === 'AbortError' ? 'Download cancelled.' : `Download failed: ${error.message || error}`);
    } finally { setMtpBusy(false); }
  }
  async function uploadMtpFile(file) {
    if (!file || mtpBusy) return;
    if (file.size === 0 || file.size > 32 * 1024) {
      setMtpStatus('Upload rejected: files must be between 1 byte and 32 KiB.');
      return;
    }
    const form = new FormData();
    form.append('storage', mtpStorage.value);
    form.append('path', mtpCurrentPath);
    form.append('file', file, file.name);
    mtpAbortController = new AbortController();
    setMtpBusy(true);
    updateMtpProgress(0, file.size);
    setMtpStatus(`Uploading ${file.name} through MTP...`);
    try {
      const response = await fetch('/api/mtp/upload', {
        method: 'POST', body: form, signal: mtpAbortController.signal
      });
      const result = await response.json();
      if (!response.ok || !result.accepted) throw new Error(result.error || `HTTP ${response.status}`);
      updateMtpProgress(file.size, file.size);
      setMtpStatus(`Uploaded ${file.name} (${file.size} bytes).`);
      mtpFile.value = '';
      setMtpBusy(false);
      await refreshMtpDirectory();
    } catch (error) {
      setMtpStatus(error.name === 'AbortError' ? 'Upload cancelled.' : `Upload failed: ${error.message || error}`);
    } finally { setMtpBusy(false); updateControls(); }
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
    logs.replaceChildren();
    const visible = logRecords.filter(record => record.gap ||
      severityRank(record.severity) >= minimum);
    visible.forEach((record, index) => {
      if (record.gap) {
        logs.append(document.createTextNode(`--- ${record.text} ---`));
      } else {
        logs.append(document.createTextNode(
          `[${record.severity}]${record.replay ? ' [history]' : ''} #${record.sequence} `));
        const segments = Array.isArray(record.segments) ? record.segments :
          [{text: record.text || ''}];
        segments.forEach(segment => {
          const span = document.createElement('span');
          if (/^#[0-9a-f]{6}$/i.test(segment.foreground || ''))
            span.style.color = segment.foreground;
          if (/^#[0-9a-f]{6}$/i.test(segment.background || ''))
            span.style.backgroundColor = segment.background;
          if (segment.bold) span.style.fontWeight = 'bold';
          span.textContent = segment.text || '';
          logs.append(span);
        });
      }
      if (index + 1 < visible.length) logs.append(document.createTextNode('\n'));
    });
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
    logRecords.push(record);
    lastLogSequence = record.sequence;
    while (logRecords.length > MAX_LOG_RECORDS) logRecords.shift();
    renderLogs();
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
        const wasSimulator = simulator;
        simulator = !!message.simulatorConnected;
        if (!simulator) batterySocBusy = false;
        if (!message.controller || !message.simulatorConnected) clearHeld();
        controller = !!message.controller && controller;
         setStatus(`${simulator ? 'Simulator connected' : 'Simulator disconnected'} - ${controller ? 'controller' : 'viewer'}${message.stale ? ' (display stale)' : ''}${message.capabilities ? ` - ${message.capabilities}` : ''}`);
        updateControls();
        if (simulator && !wasSimulator) refreshMtpDirectory();
        } else if (message.type === 'lcd') {
          renderPixels(message.pixels);
        } else if (message.type === 'lcd-contrast') {
          applyLcdContrast(message.enabled, message.duty);
        } else if (message.type === 'composition') {
          renderComposition(message);
        } else if (message.type === 'battery-soc') {
          renderBatterySoc(message);
        } else if (message.type === 'log') {
        appendLog(message);
      } else if (message.type === 'log-epoch') {
        logRecords.length = 0;
        lastLogSequence = null;
        logsStatus.textContent = `Diagnostic epoch ${message.epoch}; waiting for records.`;
      } else if (message.type === 'mtp-progress') {
        if (mtpBusy && Number.isFinite(message.transferred) && Number.isFinite(message.total))
          updateMtpProgress(message.transferred, message.total);
      } else if (message.type === 'mtp-transfer') {
        if (message.state === 'started') mtpActiveRequestId = message.requestId;
        if (message.state === 'failed') setMtpStatus(`MTP ${message.operation} failed: ${message.error || 'unknown error'}`);
        else if (message.state === 'cancelled') setMtpStatus(`MTP ${message.operation} cancelled.`);
        if (['complete', 'failed', 'cancelled'].includes(message.state) &&
            message.requestId === mtpActiveRequestId) mtpActiveRequestId = null;
      } else if (message.type === 'ack' && message.command === 'acquire') {
        controller = !!message.accepted; updateControls();
      } else if (message.type === 'ack' && message.command === 'composition-set') {
        compositionBusy = false;
        compositionStatus.textContent = message.accepted ?
          'Composition committed. Restart the simulator to activate the selected items.' :
          `Composition change rejected: ${message.error || 'unknown error'}`;
        updateControls();
      } else if (message.type === 'ack' && message.command === 'battery-soc-set') {
        batterySocBusy = false;
        batterySocStatus.textContent = message.accepted ?
          `Virtual battery SOC set to ${message.socPercent}%.` :
          `Battery SOC update rejected: ${message.error || 'unknown error'}`;
        updateControls();
      } else if (message.type === 'error' && compositionBusy) {
        compositionBusy = false;
        compositionStatus.textContent = `Command rejected: ${message.error || 'unknown error'}`;
        updateControls();
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
      controller = false; simulator = false; compositionBusy = false;
      batterySocBusy = false; clearHeld(); updateControls();
      setStatus('Disconnected; retrying...'); clearTimeout(reconnectTimer); reconnectTimer = setTimeout(connect, 1000);
    };
  }
  acquire.onclick = () => send({command: 'acquire'});
  release.onclick = () => { send({command: 'release'}); controller = false; held.clear(); updateControls(); };
  mtpRefresh.onclick = refreshMtpDirectory;
  mtpStorage.onchange = () => {
    mtpCurrentPath = '';
    mtpPathLabel.textContent = `/${mtpStorage.value}`;
    updateControls(); refreshMtpDirectory();
  };
  mtpUp.onclick = () => {
    mtpCurrentPath = mtpCurrentPath.split('/').slice(0, -1).join('/');
    updateControls(); refreshMtpDirectory();
  };
  mtpUpload.onclick = () => uploadMtpFile(mtpFile.files[0]);
  mtpFile.onchange = updateControls;
  mtpCancel.onclick = () => {
    if (mtpActiveRequestId !== null) send({command: 'mtp-cancel', requestId: mtpActiveRequestId});
    if (mtpAbortController) mtpAbortController.abort();
  };
  mtpDropzone.addEventListener('dragover', event => {
    event.preventDefault(); mtpDropzone.classList.add('drag-over');
  });
  mtpDropzone.addEventListener('dragleave', () => mtpDropzone.classList.remove('drag-over'));
  mtpDropzone.addEventListener('drop', event => {
    event.preventDefault(); mtpDropzone.classList.remove('drag-over');
    uploadMtpFile(event.dataTransfer.files[0]);
  });
  deviceDockButtons.forEach(button => {
    button.onclick = () => dockDevice(Number(button.dataset.dockPort));
  });
  batterySocApply.onclick = applyBatterySoc;
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
