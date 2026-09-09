async function imageReader(response) {
    if (!response.ok) throw Error((await response.text()).slice(0, 240));
    return response.body.getReader();
}

export async function readImage(response, expected, progress) {
    const reader = await imageReader(response);
    const chunks = [];
    let received = 0;
    try {
        while (true) {
            const { done, value } = await reader.read();
            if (done) break;
            received += value.length;
            if (received > expected) throw Error('Release image is larger than advertised.');
            chunks.push(value);
            progress(received, expected);
        }
        if (received !== expected) throw Error('Release download was incomplete.');
        return new Blob(chunks, { type: 'application/octet-stream' });
    } finally {
        await reader.cancel();
    }
}

export async function downloadImage(asset, token, progress) {
    const controller = new AbortController();
    const timeout = setTimeout(() => controller.abort(), 150000);
    try {
        const response = await fetch('/api/firmware/asset', {
            method: 'POST',
            headers: { 'X-Frame-Token': token },
            body: String(asset.id),
            signal: controller.signal,
        });
        return await readImage(response, asset.size, progress);
    } finally {
        clearTimeout(timeout);
    }
}

function uploadResult(request) {
    if (request.status < 200 || request.status >= 300) {
        return { error: request.responseText.slice(0, 240) || 'The frame rejected the image.' };
    }
    try {
        const result = JSON.parse(request.responseText);
        if (result.ok === true && result.restart === true) return { accepted: true };
    } catch {
        /* An unexpected response cannot rule out an already scheduled reboot. */
    }
    return { ambiguous: true };
}

export function uploadImage(image, token, progress, verifying) {
    return new Promise((resolve) => {
        const request = new XMLHttpRequest();
        request.open('POST', '/api/firmware');
        request.timeout = 150000;
        request.setRequestHeader('Content-Type', 'application/octet-stream');
        request.setRequestHeader('X-Frame-Token', token);
        request.upload.onprogress = (event) => progress(event.loaded, image.size);
        request.upload.onload = verifying;
        request.onload = () => resolve(uploadResult(request));
        request.onerror = request.ontimeout = request.onabort = () => resolve({ ambiguous: true });
        request.send(image);
    });
}
