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
and initial OTA metadata together. Do this before closing the frame again.

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
release to this frame. `*.pem` is ignored. Never use the disposable CI key on a
real device. Losing the key requires USB reprovisioning; do not generate another
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
