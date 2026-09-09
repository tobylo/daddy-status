const repository = 'https://api.github.com/repos/tobylo/daddy-status';

export async function fetchReleases(page) {
    const controller = new AbortController();
    const timeout = setTimeout(() => controller.abort(), 15000);
    try {
        const response = await fetch(`${repository}/releases?per_page=30&page=${page}`, {
            headers: { Accept: 'application/vnd.github+json' },
            signal: controller.signal,
            credentials: 'omit',
        });
        if ([403, 429].includes(response.status)) {
            throw Error('GitHub rate limit reached. Try later or upload a local file.');
        }
        if (!response.ok)
            throw Error('Cannot load GitHub releases. Try again or upload a local file.');
        const releases = await response.json();
        if (!Array.isArray(releases)) throw Error('GitHub returned an invalid release list.');
        return releases;
    } finally {
        clearTimeout(timeout);
    }
}

function otaAsset(release, maxSize) {
    const name = `daddy-status-${release.tag_name}-ota-frame.bin`;
    return (release.assets || []).find(
        (asset) =>
            asset.name === name &&
            asset.state === 'uploaded' &&
            Number.isSafeInteger(asset.id) &&
            asset.id > 0 &&
            Number.isSafeInteger(asset.size) &&
            asset.size > 0 &&
            asset.size <= maxSize,
    );
}

export function releaseChoices(releases, prereleases, maxSize) {
    return releases
        .filter((release) => !release.draft && (prereleases || !release.prerelease))
        .map((release) => ({ ...release, asset: otaAsset(release, maxSize) }))
        .filter((release) => release.asset);
}

export function releaseNotes(tag) {
    return `https://github.com/tobylo/daddy-status/releases/tag/${encodeURIComponent(tag)}`;
}
