const el = (id) => document.getElementById(id);
const statusText = el('status'),
    login = el('login'),
    code = el('code'),
    expiry = el('expiry');
const buttons = Array.from(document.querySelectorAll('[data-mode]'));
let deadline = 0,
    controlToken = '',
    testBusy = false,
    testDeadline = 0,
    settingsLoaded = false,
    settingsBusy = false,
    restarting = false,
    settingsToken = '',
    restartToken = '',
    trialActive = false,
    trialDeadline = 0,
    settingsAvailable = false;
const settingFields = [
    'ssid',
    'tenant',
    'client',
    'ntp',
    'poll_seconds',
    'stale_seconds',
    'brightness',
];
const errors = {
    network: 'Cannot reach Microsoft. Retrying automatically.',
    clock: 'Waiting for clock synchronization before contacting Microsoft.',
    auth: 'Microsoft sign-in needs attention. Retrying automatically.',
    auth_config: 'Check the Microsoft app registration and tenant settings.',
    auth_denied: 'Microsoft sign-in was declined. Waiting for another attempt.',
    auth_expired: 'The sign-in code expired. Waiting for another attempt.',
    storage: 'Cannot save or read the device’s login. Check device storage.',
    permission: 'Signed in, but Microsoft denied access to presence. Check Presence.Read consent.',
    throttled: 'Microsoft asked the frame to wait. Retrying after the required delay.',
    response: 'Microsoft returned an unexpected response. Retrying automatically.',
};
const displays = {
    connecting: 'Blinking blue',
    authenticating: 'Blinking yellow',
    unknown: 'Blinking blue',
    green: 'Green',
    yellow: 'One yellow LED',
    red: 'Blinking red',
    rainbow: 'Rainbow',
    configuration: 'Magenta',
};
const wifiEvents = {
    disconnected: 'Disconnected',
    retry_started: 'Retry started',
    retry_waiting: 'Waiting before retry',
    timeout: 'Connection timed out',
    connected: 'Connected',
    recovery_ap_enabled: 'Recovery network enabled',
    recovery_ap_disabled: 'Recovery network disabled',
};
function clearCode() {
    login.hidden = true;
    code.textContent = '';
    deadline = 0;
}
function firmwareBusy() {
    return Boolean(globalThis.frameFirmware?.busy());
}
function controlUnavailable() {
    return !controlToken || restarting || firmwareBusy();
}
function settingsUnavailable() {
    return !settingsLoaded || !settingsAvailable || settingsBusy || trialActive;
}
function settingsDisabled() {
    return controlUnavailable() || settingsUnavailable();
}
function lightsDisabled() {
    return controlUnavailable() || testBusy;
}
function enableTests() {
    buttons.forEach((button) => (button.disabled = lightsDisabled()));
    el('save-settings').disabled = el('reset-auth').disabled = settingsDisabled();
}
async function getJson(url) {
    const controller = new AbortController();
    const timeout = setTimeout(() => controller.abort(), 5000);
    try {
        const response = await fetch(url, { cache: 'no-store', signal: controller.signal });
        if (!response.ok) throw Error('Unavailable');
        return await response.json();
    } finally {
        clearTimeout(timeout);
    }
}

function renderPresence(data) {
    el('activity').textContent = data.activity || 'Not received yet';
    el('freshness').textContent = data.fresh ? 'Current' : 'Unavailable or stale';
    el('age').textContent =
        data.age_seconds >= 0 ? data.age_seconds + ' seconds ago' : 'Not received yet';
    el('display').textContent = displays[data.display] || 'Unknown';
    const accounts = { signed_in: 'Signed in', code: 'Sign-in required' };
    el('account').textContent = accounts[data.state] || 'Waiting';
}

function renderWifi(data) {
    el('connection').textContent = data.connected ? 'Connected' : 'Disconnected';
    el('signal').textContent = data.signal_known ? data.signal_rssi + ' dBm' : 'Unknown';
    el('disconnect').textContent =
        data.last_disconnect_reason >= 0
            ? data.last_disconnect_reason_name + ' (' + data.last_disconnect_reason + ')'
            : 'None recorded';
    el('retries').textContent = data.retry_count;
    el('next-retry').textContent =
        data.next_retry_seconds > 0
            ? 'In ' + data.next_retry_seconds + ' seconds'
            : 'Not scheduled';
    el('recovery-ap').textContent = data.recovery_ap ? 'Enabled' : 'Disabled';
}

function wifiHistoryItem(event) {
    const item = document.createElement('li');
    item.textContent =
        (wifiEvents[event.event] || 'Wi-Fi event') + ' · ' + event.age_seconds + ' seconds ago';
    if (event.event === 'disconnected') {
        item.textContent +=
            ' · ' + event.reason_name + ' (' + event.reason + ') · ' + event.rssi + ' dBm';
    }
    return item;
}

function renderHistory(events) {
    const history = el('wifi-history');
    history.textContent = '';
    events.forEach((event) => history.appendChild(wifiHistoryItem(event)));
    if (!events.length) history.textContent = 'No Wi-Fi events recorded';
}

function renderDevice(data) {
    el('poll').textContent = data.poll_seconds + ' seconds';
    el('gpio').textContent = 'GPIO ' + data.led_gpio;
    el('brightness').textContent = data.brightness_percent + '%';
    el('uptime').textContent = Math.floor(data.uptime_seconds / 60) + ' minutes';
    testDeadline = data.test_seconds > 0 ? Date.now() + data.test_seconds * 1000 : 0;
    if (!testBusy)
        el('test-status').textContent = testDeadline
            ? 'LED test active; normal status returns automatically.'
            : 'Normal Teams status';
}

function presenceMessage(data) {
    if (data.fresh) return 'Your frame is receiving Teams presence.';
    return data.state === 'signed_in'
        ? 'Signed in. Waiting for a fresh presence update…'
        : 'Waiting for the frame to connect to Microsoft…';
}

function serviceMessage(data) {
    if (!data.connected) return 'The frame is reconnecting to Wi-Fi.';
    if (errors[data.error]) return errors[data.error];
    const messages = {
        configuration: 'Complete the Microsoft settings below.',
        clock: 'Waiting for clock synchronization…',
    };
    return messages[data.service] || presenceMessage(data);
}

function renderLogin(data) {
    clearCode();
    statusText.textContent = serviceMessage(data);
    if (data.state !== 'code' || data.expires_in <= 0) return;
    code.textContent = data.user_code;
    deadline = Date.now() + data.expires_in * 1000;
    login.hidden = false;
    statusText.textContent = 'Your frame needs you to sign in.';
}

function dashboardUnavailable() {
    clearCode();
    controlToken = '';
    testDeadline = 0;
    const fields = [
        'activity',
        'freshness',
        'age',
        'display',
        'account',
        'signal',
        'disconnect',
        'retries',
        'next-retry',
        'recovery-ap',
        'wifi-history',
    ];
    fields.forEach((id) => (el(id).textContent = 'Unavailable'));
    el('connection').textContent = 'Cannot reach frame';
    el('test-status').textContent = 'Cannot reach frame. Any active test expires automatically.';
    statusText.textContent =
        'Cannot reach the frame. Check that you are on the same Wi-Fi. Reconnecting…';
}

async function update() {
    if (globalThis.frameFirmware?.transferring()) {
        setTimeout(update, 3000);
        return;
    }
    try {
        const data = await getJson('/api/status');
        controlToken = data.control_token || '';
        renderPresence(data);
        renderWifi(data);
        renderHistory(data.wifi_history || []);
        renderDevice(data);
        renderLogin(data);
        await loadSettings();
        tick();
    } catch {
        dashboardUnavailable();
    }
    enableTests();
    setTimeout(update, 3000);
}
async function postJson(url, payload, handleResponse) {
    const controller = new AbortController();
    const timeout = setTimeout(() => controller.abort(), 5000);
    try {
        const response = await fetch(url, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json', 'X-Frame-Token': controlToken },
            body: JSON.stringify(payload),
            signal: controller.signal,
        });
        return await handleResponse(response);
    } finally {
        clearTimeout(timeout);
    }
}

async function testLights(mode) {
    if (lightsDisabled()) return;
    testBusy = true;
    enableTests();
    try {
        await postJson('/api/led-test', { mode }, (response) => {
            if (!response.ok) throw Error('Rejected');
        });
        el('test-status').textContent =
            mode === 'auto'
                ? 'Returning to Teams status…'
                : 'Test started. Normal status returns after ten seconds.';
    } catch {
        el('test-status').textContent =
            'Could not confirm the test. Any started test expires automatically.';
    }
    testBusy = false;
    enableTests();
}
function populateSettings(data, rebooted) {
    settingFields.forEach((name) => (el('setting-' + name).value = data[name]));
    el('setting-password').value = '';
    el('setting-open').checked = false;
    clearFieldErrors();
    el('settings-status').textContent = rebooted
        ? 'Frame reconnected. Current settings loaded.'
        : 'Settings loaded. Passwords are never displayed.';
}

function reconcileSettingsForm(data, rebooted) {
    if (!settingsLoaded || rebooted) populateSettings(data, rebooted);
    else if (!settingsAvailable) {
        el('settings-status').textContent = 'Settings connection restored. Your edits are kept.';
    }
    settingsToken = data.control_token;
    controlToken = data.control_token;
    settingsLoaded = true;
    settingsAvailable = true;
}

function reconcileSettingsRestart(data) {
    if (data.control_token !== restartToken) restarting = false;
    if (data.restart_pending) {
        restarting = true;
        restartToken = data.control_token;
    }
    if (restarting) showRestart();
}

function reconcileTrial(data, rebooted) {
    const completed = trialActive && !data.trial && !data.restart_pending && !rebooted;
    if (completed) el('settings-status').textContent = 'Wi-Fi trial completed. Settings confirmed.';
    trialActive = Boolean(data.trial);
    trialDeadline = trialActive ? Date.now() + data.trial_seconds_remaining * 1000 : 0;
    tickTrial();
}

async function loadSettings() {
    try {
        const data = await getJson('/api/settings');
        const rebooted = settingsToken && data.control_token !== settingsToken;
        reconcileSettingsForm(data, rebooted);
        reconcileTrial(data, rebooted);
        reconcileSettingsRestart(data);
    } catch {
        settingsAvailable = false;
        if (!restarting) el('settings-status').textContent = 'Cannot load settings. Retrying…';
    }
}
const fieldMessages = {
    ssid: 'Enter a Wi-Fi name of 1–32 bytes.',
    password:
        'Use 8–63 printable ASCII characters or 64 hexadecimal digits. Leave blank to keep the saved password.',
    open: 'Choose whether to use an open network.',
    tenant: 'Enter a valid tenant GUID (xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx).',
    client: 'Enter a valid app client GUID (xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx).',
    ntp: 'Enter a hostname or IPv4 address using letters, digits, dots and hyphens (up to 253 bytes).',
    poll_seconds: 'Enter a whole number from 2 to 300.',
    stale_seconds: 'Enter a whole number from 30 to 3600, greater than the poll interval.',
    brightness: 'Enter a whole number from 1 to 100.',
};
function clearFieldErrors() {
    Object.keys(fieldMessages).forEach((name) => {
        el('error-' + name).hidden = true;
        el('setting-' + name).removeAttribute('aria-invalid');
    });
}
function showRestart(wifiTrial = false) {
    el('settings-status').textContent =
        'Restart pending' +
        (wifiTrial ? ' for a Wi-Fi connection trial' : '') +
        '. Waiting for the frame to reconnect automatically. If its address changes, reconnect to its Wi-Fi and open the new address, or use setup Wi-Fi at http://192.168.4.1/.';
}
function tickTrial() {
    el('trial-status').hidden = el('trial-progress').hidden = !trialActive;
    if (!trialActive) return;
    const seconds = Math.max(0, Math.ceil((trialDeadline - Date.now()) / 1000));
    el('trial-progress').value = 180 - seconds;
    el('trial-status').textContent =
        'Trying new Wi-Fi settings: ' +
        seconds +
        ' seconds remaining. Waiting for connection and storage confirmation; previous settings return if the trial fails.';
}
function settingsPayload(action) {
    const payload = { action };
    if (action !== 'save') return payload;
    settingFields.forEach((name) => {
        const value = el('setting-' + name).value;
        payload[name] = ['poll_seconds', 'stale_seconds', 'brightness'].includes(name)
            ? Number(value)
            : value;
    });
    payload.password = el('setting-password').value;
    payload.open_network = el('setting-open').checked;
    return payload;
}

function showFieldError(field) {
    const input = el('setting-' + field),
        message = el('error-' + field);
    message.textContent = fieldMessages[field];
    message.hidden = false;
    input.setAttribute('aria-invalid', 'true');
    input.focus();
    el('settings-status').textContent =
        'Settings were not saved. Correct the highlighted field and try again.';
}

async function settingsFailure(response) {
    const data = await response.json().catch(() => ({}));
    if (data.error === 'validation' && Object.hasOwn(fieldMessages, data.field)) {
        showFieldError(data.field);
        return;
    }
    if (data.error === 'storage') {
        el('settings-status').textContent =
            'Device storage failed. The change was not confirmed and no restart was scheduled. Try again.';
        return;
    }
    await refreshRejectedSettings(response.status, data.error);
}

async function refreshRejectedSettings(status, error) {
    const messages = {
        restart_or_trial_pending:
            'A restart or Wi-Fi trial is already in progress. Waiting for updated status…',
        session: 'The frame session changed. Refreshing its settings; try again once connected.',
    };
    const reason = status === 403 ? 'session' : error;
    el('settings-status').textContent =
        messages[reason] || 'The frame rejected the request. Try again once connected.';
    if (messages[reason]) await loadSettings();
}

function settingsSaved(data, submittedToken) {
    const messages = {
        unchanged: 'No changes to save.',
        applied: 'Saved. Brightness applied.',
        restart: '',
        wifi_trial: '',
    };
    if (!Object.hasOwn(messages, data.outcome)) throw Error('Unknown save outcome');
    el('setting-password').value = '';
    if (['restart', 'wifi_trial'].includes(data.outcome)) {
        restarting = true;
        restartToken = submittedToken;
        showRestart(data.outcome === 'wifi_trial');
    } else {
        el('settings-status').textContent = messages[data.outcome];
    }
}

async function changeSettings(action) {
    if (settingsDisabled()) return;
    if (action === 'reset_auth' && !confirm('Clear Microsoft sign-in and restart the frame?'))
        return;
    clearFieldErrors();
    settingsBusy = true;
    enableTests();
    const submittedToken = controlToken;
    try {
        await postJson('/api/settings', settingsPayload(action), async (response) => {
            if (!response.ok) return settingsFailure(response);
            settingsSaved(await response.json(), submittedToken);
        });
    } catch {
        el('settings-status').textContent =
            'Change not confirmed: connection lost. Reconnecting automatically to check the frame. Your entered settings are kept until a reboot is detected.';
    }
    settingsBusy = false;
    enableTests();
}
el('settings-form').addEventListener('submit', (event) => {
    event.preventDefault();
    changeSettings('save');
});
el('reset-auth').addEventListener('click', () => changeSettings('reset_auth'));
function tick() {
    tickTrial();
    if (!deadline) return;
    const seconds = Math.max(0, Math.ceil((deadline - Date.now()) / 1000));
    if (!seconds) {
        clearCode();
        statusText.textContent = 'Code expired. Waiting for a new code…';
        return;
    }
    expiry.textContent =
        'Code expires in ' + Math.floor(seconds / 60) + ':' + String(seconds % 60).padStart(2, '0');
}
buttons.forEach((button) =>
    button.addEventListener('click', () => testLights(button.dataset.mode)),
);
globalThis.refreshFrameControls = enableTests;
globalThis.frameSettingsBusy = () => settingsBusy || restarting || trialActive;
setInterval(tick, 1000);
update();
