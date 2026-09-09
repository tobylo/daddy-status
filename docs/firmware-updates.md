# Wireless firmware updates

OTA is an opt-in signed application upload on the trusted local network. The
signing key authenticates the firmware publisher: IDF verifies the signature in
`esp_ota_end()` before the new slot can be selected. Unsigned, corrupted, or
wrong-key images are rejected. The page's public per-boot token also blocks
ordinary cross-site submissions; it is not an administrator password.

The upload uses local HTTP, like the dashboard. It does not provide transport
confidentiality or protect against a LAN client repeatedly submitting images or
replaying an older correctly signed release. Keep firmware free of compiled-in
secrets when distributing it, keep the LAN trusted, and do not expose the port
to the internet. This feature does not enable hardware Secure Boot, flash
encryption, eFuse anti-rollback, or protection from physical flash replacement.

## One-time USB migration

The old single-app table cannot gain OTA support through an application update.
Use USB once to flash the new bootloader, partition table, signed application,
and initial OTA metadata together. Do this before closing the frame again. The
`ota-frame-factory` image from a [tagged release](#tagged-releases-and-the-web-flasher)
is the same set merged into one file, at the cost of erasing saved settings.

The 4 MB layout has two 1,984 KiB app slots (`ota_0` at `0x10000`, `ota_1` at
`0x200000`) and OTA metadata at `0x3f0000`. NVS remains at `0x9000`, size `0x6000`,
and PHY data remains at `0xf000`; normal full flashing preserves saved settings
and Microsoft authorization. Do not erase flash. Check the actual board has at
least 4 MB and that the **signed** binary fits a slot. Do not reuse an old
`sdkconfig`: existing values override the new defaults.

With ESP-IDF 6.1 exported, generate a unique ECDSA signing key once:

```sh
espsecure generate-signing-key --version 1 secure_boot_signing_key.pem
idf.py -B build-ota -D SDKCONFIG=sdkconfig.ota.local \
  -D 'SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.frame;sdkconfig.ota' menuconfig
idf.py -B build-ota -D SDKCONFIG=sdkconfig.ota.local \
  -D 'SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.frame;sdkconfig.ota' build
idf.py -B build-ota -D SDKCONFIG=sdkconfig.ota.local -p /dev/ttyUSB0 flash monitor
```

Use the device's real port and omit `sdkconfig.frame` if its GPIO13 wiring is not
appropriate. Configure Wi-Fi/recovery access in menuconfig. Verify `FRAME_OTA_ENABLE`,
`SECURE_SIGNED_ON_UPDATE`, and `BOOTLOADER_APP_ROLLBACK_ENABLE` are enabled in the
generated configuration. The profile uses IDF's ECDSA V1 signed-app scheme for
classic ESP32 compatibility, without enabling hardware Secure Boot.

Back up the private key securely outside the repository and reuse it for every
release to this frame. `*.pem` is ignored. Never use the disposable CI key from
pull-request builds on a real device. Losing the key requires USB reprovisioning; do not generate another
key over the existing one. The default build has the dual-slot layout and
rollback enabled but rejects all uploads until signed OTA is configured.

## Later wireless updates

Build with the same profile, configuration, and signing key. On a computer on
the same trusted network, upload **only** `build-ota/daddy-status.bin`, the signed
application image. Do not upload a merged flash image, bootloader, or partition
table. The endpoint takes a raw binary body with Content-Length, not multipart
form data or chunked transfer encoding. Example with curl and jq:

```sh
frame=http://192.168.1.123
frame_token=$(curl --fail --silent --show-error "$frame/api/status" | jq -er .control_token)
curl --fail-with-body --show-error --max-time 150 \
  -H "X-Frame-Token: $frame_token" -H 'Content-Type: application/octet-stream' \
  --data-binary @build-ota/daddy-status.bin "$frame/api/firmware"
```

Wait for any settings trial/restart and firmware probation to finish first.
The upload holds a settings reservation, writes only the inactive slot, and has
a two-minute receive deadline plus a five-second socket timeout. Failed writes,
interrupted transfers, and failed verification leave the selected boot image
unchanged. A successful response means the image was verified and selected;
the device then restarts even if the response could not reach the client.
Check the device before retrying an ambiguous network failure.

## Tagged releases and the web flasher

Pushing a `v*` tag that is reachable from `master` runs `.github/workflows/release.yml`.
It builds two profiles, signs the OTA-capable one with the **release key**,
publishes a GitHub release with notes generated from the merged pull requests
(categories in `.github/release.yml`), and deploys a web flasher to GitHub Pages
from `web/index.html`. Tags containing a hyphen, such as `v1.2.0-rc1`, are marked
as pre-releases. CI builds use empty credential defaults; settings are entered
through the device's web page afterwards.

| Asset | Profile | Signed | Use |
| --- | --- | --- | --- |
| `daddy-status-<tag>-factory.bin` | `sdkconfig.defaults` | no | Merged image for USB or web flashing at `0x0`. No OTA endpoint; LED GPIO 25. |
| `daddy-status-<tag>-ota-frame-factory.bin` | frame + ota | release key | Merged image for USB or web flashing at `0x0`. Signed OTA and rollback; LED GPIO 13. |
| `daddy-status-<tag>-ota-frame.bin` | frame + ota | release key | App image for the OTA page on a device running an `ota-frame` image. |
| `signature_verification_key.bin`, `*.elf`, `SHA256SUMS` | | | Public key for `espsecure verify-signature`, backtrace symbols, checksums. |

Merged factory images pad the gaps between bootloader, partition table, app and
OTA data with `0xFF`, so flashing one **erases saved settings and Microsoft
sign-in**. App images uploaded through the OTA page keep them. The
[encrypted profile](encrypted-storage.md) is not released at all: its first
boot permanently programs eFuses, its devices have no USB fallback, and its
signing key should therefore be set up and kept locally rather than in GitHub.

### Release key model

The release key is the ECDSA signing key described above, with one copy on your
machine and one in GitHub. Devices flashed from a release accept only images
signed with it, so the only sources of updates are GitHub releases and local
builds signed with the same key. Because the verification key is compiled into
the app rather than fused, hardware Secure Boot must stay off in this model; a
fused key cannot be rotated and must never live in CI.

**Key rotation is a manual, local procedure**, not a tagged release: the
workflow refuses any build whose signing key does not match the committed
public key, and a bridge image needs the opposite combination. To rotate:

1. Generate the new key and extract its public key to a separate file, without
   touching the committed one yet.
2. Build a bridge image that embeds the **new** public key but is not signed
   at build time, then sign it with the **old** key and verify it:

   ```sh
   idf.py -B build-bridge -D SDKCONFIG=build-bridge/sdkconfig \
     -D 'SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.frame;sdkconfig.ota' \
     -D CONFIG_SECURE_BOOT_BUILD_SIGNED_BINARIES=n \
     -D CONFIG_SECURE_BOOT_VERIFICATION_KEY=new_verification_key.bin build
   espsecure sign-data --version 1 --keyfile old_signing_key.pem \
     --output bridge.bin build-bridge/daddy-status.bin
   espsecure verify-signature --version 1 --keyfile signature_verification_key.bin bridge.bin
   ```

3. Upload `bridge.bin` through the OTA page of every device and wait for boot
   confirmation. Each device now trusts the new key only.
4. Replace the committed `signature_verification_key.bin` with the new public
   key, update `SIGNING_KEY_PEM`, and tag the next release as usual.

Devices that miss the bridge can only be recovered with a factory image over
USB, which erases their settings.

One-time setup, for this repository or for a fork that wants its own key:

1. Generate the key as shown above and back it up outside the repository.
2. Extract the public key and commit it. It is not secret; every signed image
   already contains it.

   ```sh
   espsecure extract-public-key --version 1 --keyfile secure_boot_signing_key.pem \
     signature_verification_key.bin
   ```

3. In the repository settings create an environment named `release`, restrict
   its deployment branches and tags to `v*`, and add a secret `SIGNING_KEY_PEM`
   containing the PEM file. Only the tag-triggered build job runs in that
   environment; pull-request builds keep using a disposable key that never
   reaches hardware.
4. Under Pages, set the source to **GitHub Actions**.

The workflow refuses to run if the tag is not on `master`, if the secret or the
committed public key is missing, or if the two do not match. The last check
prevents publishing images that would strand every device on its next update.
Each signed image is verified against the committed public key before upload.

To publish a release:

```sh
git tag -a v1.0.0 -m "v1.0.0"
git push origin v1.0.0
```

### Web flasher

The Pages site offers the two factory images through
[ESP Web Tools](https://esp-web-tools.esphome.io/). It needs a Chromium browser
with Web Serial, a USB connection to a classic ESP32, and it erases the flash.
Users who prefer the command line can flash the same files with esptool:

```sh
esptool --chip esp32 -p /dev/ttyUSB0 write-flash 0x0 daddy-status-v1.0.0-ota-frame-factory.bin
```

## Rollback and acceptance

On first OTA boot, IDF marks the candidate pending verification. Local startup
must initialize NVS, settings, LEDs, the web server, and the Graph worker. The
main/display loop must then run with station Wi-Fi connected continuously for
30 seconds, with no settings trial active. Only then is the firmware confirmed.
Microsoft login, NTP, and Graph availability are not prerequisites for confirmation.
This checks software initialization and network reachability, not physical LED
output or every application behavior.

A reset/crash before confirmation causes the bootloader to return to the previous
valid slot. If the main loop runs but cannot pass health checks within three
minutes, it explicitly requests rollback. A Wi-Fi outage during probation can
therefore roll back otherwise healthy firmware. A hung system requires a watchdog
or power reset to trigger bootloader rollback. The initial USB installation has
no previous OTA image to return to; validate it before the first wireless update.

OTA does not roll back NVS writes. Future releases must keep stored settings and
tokens backward-compatible with the previous image throughout probation. Neither
slot nor the partition table is resized over the network. Keep the previous
firmware binary and signing key as part of release records.

See the [ESP-IDF OTA guide](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32/api-reference/system/ota.html)
and [signed app verification](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32/security/secure-boot-v1.html)
for the SDK signature and boot-state behavior.

## Encrypted devices

The [encrypted storage profile](encrypted-storage.md) combines signed OTA with
NVS and flash encryption. Its first installation requires USB provisioning;
an application-only OTA upload cannot install its partition table or bootloader.
Keep using that profile and the same signing key for later OTA updates. A build
without NVS encryption refuses to initialize storage on an encrypted device,
so it cannot erase encrypted credentials through the normal NVS recovery path.
Such an incompatible update fails boot confirmation and rolls back.
Release-mode flash encryption restricts USB recovery: the generic USB reflash
advice above does not apply to devices provisioned with that profile.
