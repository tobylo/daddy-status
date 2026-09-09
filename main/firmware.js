import { downloadImage, uploadImage } from './firmware_transfer.js';
import { fetchReleases, releaseChoices, releaseNotes } from './firmware_releases.js';

const el = (id) => document.getElementById(id);
const readinessMessages = {
    disabled:
        'Wireless updates are disabled. Provision an OTA-capable factory image over USB first.',
    unavailable: 'No OTA slot is available. Check the USB provisioning guide.',
    probation: 'The new firmware is running its boot health check. Wait for confirmation.',
    busy: 'A settings trial or restart is in progress. Wait before updating.',
    ready: 'Ready for a signed application image. Settings and Microsoft sign-in will be kept.',
};
let status,
    transfer = false,
    pending,
    releases = [],
    choices = [],
    page = 1,
    loading = false;
let pollInFlight = false;
let knownMaxSize = 0;

function message(text) {
    el('firmware-message').textContent = text;
}
function progress(stage, loaded, total) {
    el('firmware-progress').hidden = false;
    el('firmware-progress').value = Math.round((100 * loaded) / total);
    message(`${stage}: ${Math.round((100 * loaded) / total)}%`);
}
function busy() {
    return transfer || Boolean(pending);
}
function ready() {
    return status?.readiness === 'ready' && !busy() && !globalThis.frameSettingsBusy?.();
}
function controls() {
    el('firmware-file').disabled = !ready();
    el('firmware-upload').disabled = !ready() || !el('firmware-file').files.length;
    el('firmware-install').disabled = !ready() || !choices.length;
    el('firmware-release').disabled = busy() || loading;
    el('firmware-prereleases').disabled = busy() || loading;
    el('firmware-refresh').disabled = busy() || loading;
    el('firmware-more').disabled = busy() || loading;
    globalThis.refreshFrameControls?.();
}

function finish(text) {
    pending = undefined;
    message(text);
    controls();
}

function reconcileBoot(current) {
    if (!pending) return;
    if (current.control_token !== pending.token) {
        if (current.partition === pending.partition) {
            finish(
                'The frame returned to its previous firmware slot. The update was not retained (rollback or restart).',
            );
        } else if (current.confirmed) {
            finish(
                `Update complete. Firmware ${current.version} has passed its boot health check.`,
            );
        } else {
            message(`Firmware ${current.version} restarted. Waiting for its boot health check…`);
        }
    }
    if (pending && Date.now() > pending.deadline) {
        finish(
            'Update outcome could not be confirmed. Check the current version and frame before explicitly retrying. No automatic retry was made.',
        );
    }
}

async function readStatus() {
    const controller = new AbortController();
    const timeout = setTimeout(() => controller.abort(), 5000);
    try {
        const response = await fetch('/api/firmware', {
            cache: 'no-store',
            signal: controller.signal,
        });
        if (!response.ok) throw Error('Cannot read firmware status.');
        const current = await response.json();
        if (!current.control_token || !readinessMessages[current.readiness])
            throw Error('Invalid firmware status.');
        status = current;
        knownMaxSize = current.max_size;
        el('firmware-version').textContent = current.version;
        el('firmware-readiness').textContent = readinessMessages[current.readiness];
        reconcileBoot(current);
        return current;
    } finally {
        clearTimeout(timeout);
    }
}

async function poll() {
    if (!transfer && !pollInFlight) {
        pollInFlight = true;
        try {
            await readStatus();
        } catch {
            status = undefined;
            el('firmware-readiness').textContent = 'Cannot reach the frame. Reconnecting…';
            if (pending && Date.now() > pending.deadline) {
                finish(
                    'Cannot confirm the update. Reconnect to the frame and check its version before retrying.',
                );
            }
        } finally {
            pollInFlight = false;
            controls();
        }
    }
    setTimeout(poll, 3000);
}

function selection() {
    const release = choices[Number(el('firmware-release').value)];
    el('firmware-notes').hidden = !release;
    if (release) el('firmware-notes').href = releaseNotes(release.tag_name);
}

function renderReleases() {
    choices = releaseChoices(releases, el('firmware-prereleases').checked, knownMaxSize);
    el('firmware-release').replaceChildren();
    choices.forEach((release, index) => {
        const option = document.createElement('option');
        option.value = index;
        option.textContent = `${release.tag_name} · ${String(release.published_at).slice(0, 10)}${release.prerelease ? ' · prerelease' : ''}`;
        el('firmware-release').appendChild(option);
    });
    el('firmware-releases-message').textContent = choices.length
        ? 'Choose a release to install.'
        : 'No matching OTA releases in these results. Try older releases, include prereleases, or upload a local file.';
    selection();
    controls();
}

async function loadReleases(reset) {
    if (loading || busy()) return;
    loading = true;
    controls();
    el('firmware-releases-message').textContent = 'Loading GitHub releases…';
    try {
        await readStatus();
        const nextPage = reset ? 1 : page;
        const batch = await fetchReleases(nextPage);
        releases = reset ? batch : releases.concat(batch);
        page = nextPage + 1;
        el('firmware-more').hidden = batch.length < 30;
        renderReleases();
    } catch (error) {
        el('firmware-releases-message').textContent =
            `${error.message} Local upload remains available.`;
    } finally {
        loading = false;
        controls();
    }
}

function checkImage(image, name, maxSize) {
    if (!image?.size || image.size > maxSize)
        throw Error('Choose a nonempty application image that fits the OTA slot.');
    if (!/\.bin$/i.test(name) || /factory|bootloader|partition/i.test(name)) {
        throw Error(
            'Choose a signed application .bin, not a factory image, bootloader or partition table.',
        );
    }
}

async function install(image, before) {
    // Record the old boot before sending: a lost response may still mean success.
    pending = {
        token: before.control_token,
        partition: before.partition,
        deadline: Date.now() + 300000,
    };
    const result = await uploadImage(
        image,
        before.control_token,
        (loaded, total) => progress('Uploading', loaded, total),
        () => message('Upload sent. The frame is verifying the image signature…'),
    );
    if (result.error) return finish(`Update rejected: ${result.error}`);
    message(
        result.accepted
            ? 'Image verified. Waiting for restart and boot health confirmation…'
            : 'Upload response was lost. Checking whether the frame updated; do not upload again yet.',
    );
}

function localCandidate() {
    const file = el('firmware-file').files[0];
    return { image: file, name: file?.name, load: async () => file };
}

function releaseCandidate(release) {
    return {
        image: release.asset,
        name: release.asset.name,
        load: (token) =>
            downloadImage(release.asset, token, (loaded, total) =>
                progress('Downloading', loaded, total),
            ),
    };
}

function confirmCandidate(candidate) {
    try {
        checkImage(candidate.image, candidate.name, status.max_size);
    } catch (error) {
        message(error.message);
        return false;
    }
    return confirm(
        `Install ${candidate.name}? The frame will restart. Use an image signed with this frame's key and matching its hardware/profile.`,
    );
}

async function transferCandidate(candidate) {
    const before = await readStatus();
    if (before.readiness !== 'ready') throw Error(readinessMessages[before.readiness]);
    checkImage(candidate.image, candidate.name, before.max_size);
    const image = await candidate.load(before.control_token);
    await install(image, before);
}

async function startUpdate(candidate) {
    if (!ready() || loading) return;
    if (!confirmCandidate(candidate)) return;
    transfer = true;
    controls();
    el('firmware-progress').hidden = true;
    message('Preparing the firmware update…');
    try {
        await transferCandidate(candidate);
    } catch (error) {
        message(
            pending
                ? 'Upload outcome uncertain. Checking the frame before allowing another update…'
                : `Update could not start: ${error.message}`,
        );
    } finally {
        transfer = false;
        controls();
    }
}

el('firmware-file').addEventListener('change', controls);
el('firmware-upload').addEventListener('click', () => startUpdate(localCandidate()));
el('firmware-install').addEventListener('click', () => {
    const release = choices[Number(el('firmware-release').value)];
    if (release) startUpdate(releaseCandidate(release));
});
el('firmware-refresh').addEventListener('click', () => loadReleases(true));
el('firmware-more').addEventListener('click', () => loadReleases(false));
el('firmware-release').addEventListener('change', selection);
el('firmware-prereleases').addEventListener('change', renderReleases);
globalThis.frameFirmware = { busy, transferring: () => transfer };
poll();
