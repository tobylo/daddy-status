import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import vm from 'node:vm';
import { readImage, uploadImage, downloadImage } from '../main/firmware_transfer.js';
import { releaseChoices, releaseNotes, fetchReleases } from '../main/firmware_releases.js';

const release = (tag, prerelease = false) => ({
    tag_name: tag,
    prerelease,
    published_at: '2026-09-09T00:00:00Z',
    assets: [
        { name: `daddy-status-${tag}-ota-frame-factory.bin`, id: 1, size: 50, state: 'uploaded' },
        { name: `daddy-status-${tag}-ota-frame.bin`, id: 2, size: 50, state: 'uploaded' },
    ],
});

function releaseFiltering() {
    const releases = [release('v1'), release('v2-rc1', true), { ...release('v3'), draft: true }];
    assert.deepEqual(
        releaseChoices(releases, false, 100).map((r) => r.tag_name),
        ['v1'],
    );
    assert.equal(releaseChoices(releases, true, 100).length, 2);
    assert.equal(releaseChoices(releases, true, 40).length, 0);
    assert.equal(releaseChoices([{ ...release('v1'), assets: [] }], true, 100).length, 0);
    assert.equal(releaseChoices(releases, false, 100)[0].asset.id, 2);
    assert.equal(
        releaseNotes('v1/<script>'),
        'https://github.com/tobylo/daddy-status/releases/tag/v1%2F%3Cscript%3E',
    );
}

async function boundedDownload() {
    const seen = [];
    const blob = await readImage(new Response(new Uint8Array(50)), 50, (n) => seen.push(n));
    assert.equal(blob.size, 50);
    assert.deepEqual(seen, [50]);
    await assert.rejects(
        readImage(new Response(new Uint8Array(51)), 50, () => {}),
        /larger/,
    );
    await assert.rejects(
        readImage(new Response(new Uint8Array(49)), 50, () => {}),
        /incomplete/,
    );
    await assert.rejects(
        readImage(new Response('Rejected', { status: 400 }), 50, () => {}),
        /Rejected/,
    );
}

async function requestContracts() {
    const original = globalThis.fetch;
    const requests = [];
    globalThis.fetch = async (url, options) => {
        requests.push({ url, ...options });
        return new Response(new Uint8Array(50));
    };
    try {
        assert.equal((await downloadImage({ id: 2, size: 50 }, 'local-token', () => {})).size, 50);
        assert.equal(requests[0].url, '/api/firmware/asset');
        assert.equal(requests[0].headers['X-Frame-Token'], 'local-token');
        assert.equal(requests[0].body, '2');
        globalThis.fetch = async (url, options) => {
            requests.push({ url, ...options });
            return new Response('[]', { status: 403 });
        };
        await assert.rejects(fetchReleases(2), /rate limit/);
        assert.match(requests[1].url, /per_page=30&page=2$/);
        assert.equal(requests[1].credentials, 'omit');
        assert.equal(requests[1].headers['X-Frame-Token'], undefined);
    } finally {
        globalThis.fetch = original;
    }
}

async function uploadContracts() {
    let request;
    globalThis.XMLHttpRequest = class {
        constructor() {
            request = this;
            this.upload = {};
            this.headers = {};
        }
        open(method, url) {
            this.method = method;
            this.url = url;
        }
        setRequestHeader(key, value) {
            this.headers[key] = value;
        }
        send(body) {
            this.body = body;
        }
    };
    const file = new Blob([new Uint8Array(50)]);
    let verifying = false;
    const promise = uploadImage(
        file,
        'token',
        () => {},
        () => {
            verifying = true;
        },
    );
    assert.equal(request.url, '/api/firmware');
    assert.equal(request.method, 'POST');
    assert.equal(request.body, file);
    assert.equal(request.headers['X-Frame-Token'], 'token');
    assert.equal(request.headers['Content-Type'], 'application/octet-stream');
    assert.equal(request.timeout, 150000);
    request.upload.onload();
    assert.ok(verifying);
    request.status = 200;
    request.responseText = '{"ok":true,"restart":true}';
    request.onload();
    assert.deepEqual(await promise, { accepted: true });
    const lost = uploadImage(
        file,
        'token',
        () => {},
        () => {},
    );
    request.onerror();
    assert.deepEqual(await lost, { ambiguous: true });
    const rejected = uploadImage(
        file,
        'token',
        () => {},
        () => {},
    );
    request.status = 400;
    request.responseText = 'Untrusted firmware';
    request.onload();
    assert.deepEqual(await rejected, { error: 'Untrusted firmware' });
    delete globalThis.XMLHttpRequest;
}

function element() {
    return {
        value: '',
        textContent: '',
        hidden: false,
        disabled: false,
        checked: false,
        files: [],
        children: [],
        events: {},
        addEventListener(name, callback) {
            this.events[name] = callback;
        },
        replaceChildren() {
            this.children = [];
            this.value = '';
        },
        appendChild(child) {
            this.children.push(child);
            if (this.children.length === 1) this.value = child.value;
        },
    };
}

function harness() {
    const html = readFileSync(new URL('../main/auth.html', import.meta.url), 'utf8');
    const elements = Object.fromEntries(
        [...html.matchAll(/id="(firmware-[^"]+)"/g)].map(([, id]) => [id, element()]),
    );
    const state = {
        version: 'v1',
        partition: 65536,
        max_size: 100,
        readiness: 'ready',
        confirmed: true,
        control_token: 'boot-one',
    };
    const model = {
        state,
        now: 0,
        settingsBusy: false,
        offline: false,
        uploads: [],
        downloads: [],
        result: { accepted: true },
        releaseList: [release('v2')],
        releaseError: undefined,
    };
    const context = vm.createContext({
        document: { getElementById: (id) => elements[id], createElement: element },
        Date: { now: () => model.now },
        AbortController,
        confirm: () => true,
        setTimeout: () => 1,
        clearTimeout: () => {},
        frameSettingsBusy: () => model.settingsBusy,
        fetch: async () => {
            if (model.offline) throw Error('offline');
            return { ok: true, json: async () => ({ ...state }) };
        },
        fetchReleases: async () => {
            if (model.releaseError) throw Error(model.releaseError);
            return model.releaseList;
        },
        releaseChoices,
        releaseNotes,
        downloadImage: async (asset, token, progress) => {
            model.downloads.push({ asset, token });
            progress(asset.size, asset.size);
            return { size: asset.size };
        },
        uploadImage: async (image, token, progress, verifying) => {
            model.uploads.push({ image, token });
            progress(image.size, image.size);
            verifying();
            return model.result;
        },
    });
    const script = readFileSync(new URL('../main/firmware.js', import.meta.url), 'utf8').replace(
        /^import .*;\n/gm,
        '',
    );
    vm.runInContext(script, context);
    return { elements, model, run: (source) => vm.runInContext(source, context) };
}

async function localUpdateAndBoot() {
    const { elements: e, model: m, run } = harness();
    await run('readStatus()');
    e['firmware-file'].files = [{ name: 'signed.bin', size: 50 }];
    await run('startUpdate(localCandidate())');
    assert.equal(m.uploads.length, 1);
    assert.equal(m.uploads[0].token, 'boot-one');
    assert.equal(run('busy()'), true);
    assert.equal(e['firmware-upload'].disabled, true);
    await run('startUpdate(localCandidate())');
    assert.equal(m.uploads.length, 1);
    await run('readStatus()');
    assert.equal(run('busy()'), true); // The old boot cannot confirm an update.
    Object.assign(m.state, {
        partition: 2097152,
        control_token: 'boot-two',
        version: 'v2',
        confirmed: false,
        readiness: 'probation',
    });
    await run('readStatus()');
    assert.match(e['firmware-message'].textContent, /Waiting for its boot health/);
    Object.assign(m.state, { confirmed: true, readiness: 'ready' });
    await run('readStatus()');
    assert.equal(run('busy()'), false);
    assert.match(e['firmware-message'].textContent, /Update complete.*v2/);
}

async function ambiguousAndRollback() {
    const { elements: e, model: m, run } = harness();
    await run('readStatus()');
    e['firmware-file'].files = [{ name: 'signed.bin', size: 50 }];
    m.result = { ambiguous: true };
    await run('startUpdate(localCandidate())');
    assert.match(e['firmware-message'].textContent, /response was lost/);
    assert.equal(run('busy()'), true);
    m.offline = true;
    await run('poll()');
    m.offline = false;
    m.state.control_token = 'rollback-boot';
    await run('readStatus()');
    assert.match(e['firmware-message'].textContent, /previous firmware slot/);
    assert.equal(m.uploads.length, 1);
    assert.equal(run('busy()'), false);
}

async function rejectionAndValidation() {
    const { elements: e, model: m, run } = harness();
    await run('readStatus()');
    for (const file of [
        { name: 'factory.bin', size: 50 },
        { name: 'signed.bin', size: 101 },
        { name: 'empty.bin', size: 0 },
    ]) {
        e['firmware-file'].files = [file];
        await run('startUpdate(localCandidate())');
    }
    assert.equal(m.uploads.length, 0);
    e['firmware-file'].files = [{ name: 'signed.bin', size: 50 }];
    m.settingsBusy = true;
    await run('startUpdate(localCandidate())');
    assert.equal(m.uploads.length, 0);
    m.settingsBusy = false;
    m.result = { error: 'Incomplete, invalid or untrusted firmware' };
    await run('startUpdate(localCandidate())');
    assert.match(e['firmware-message'].textContent, /Update rejected.*untrusted/);
    assert.equal(run('busy()'), false);
    m.state.readiness = 'disabled';
    await run('readStatus()');
    await run('startUpdate(localCandidate())');
    assert.equal(m.uploads.length, 1);
    assert.match(e['firmware-readiness'].textContent, /USB/);
}

async function releaseInstallAndFallback() {
    const { elements: e, model: m, run } = harness();
    await run('readStatus()');
    await run('loadReleases(true)');
    assert.equal(e['firmware-release'].children.length, 1);
    assert.equal(e['firmware-notes'].href, releaseNotes('v2'));
    await run('startUpdate(releaseCandidate(choices[0]))');
    assert.equal(m.downloads.length, 1);
    assert.equal(m.downloads[0].asset.id, 2);
    assert.equal(m.uploads[0].image.size, 50);
    assert.equal(m.uploads[0].token, 'boot-one');
    m.now = 300001;
    await run('readStatus()');
    assert.match(e['firmware-message'].textContent, /could not be confirmed/);
    m.releaseError = 'Rate limited';
    await run('loadReleases(true)');
    e['firmware-file'].files = [{ name: 'signed.bin', size: 50 }];
    await run('startUpdate(localCandidate())');
    assert.equal(m.uploads.length, 2);
    assert.match(e['firmware-releases-message'].textContent, /Local upload remains available/);
}

releaseFiltering();
await boundedDownload();
await requestContracts();
await uploadContracts();
await localUpdateAndBoot();
await ambiguousAndRollback();
await rejectionAndValidation();
await releaseInstallAndFallback();
console.log(
    'Firmware browser filtering, bounded transfers, authorization, boot confirmation, rollback and failures passed',
);
