# Encrypted credential storage

The optional `sdkconfig.encrypted` profile enables ESP-IDF native XTS-AES NVS
encryption for the default `nvs` partition, covering refresh tokens, frame
settings and Wi-Fi driver credentials. Access tokens remain in RAM. The
application continues using the standard NVS APIs; it contains no custom crypto
or embedded encryption key. Default development builds remain unencrypted.

On classic ESP32, NVS keys are protected by hardware flash encryption. The
profile uses **release mode**, which permanently programs security eFuses on
first boot and restricts subsequent USB flashing/debugging. Building or merging
this profile does not provision a device. Do not flash it as an ordinary update.

Use [Espressif's NVS encryption guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/storage/nvs_encryption.html)
and [flash encryption provisioning guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/security/flash-encryption.html)
when preparing hardware. This protects stored data against raw flash readout;
it does not enable hardware Secure Boot or protect a running application from
compromise. The composed OTA profile verifies application signatures. Local
browser HTTP remains limited to trusted networks.

## Build

Use ESP-IDF 6.1 and a fresh configuration, because existing sdkconfig values
override profile defaults. Generate a signing key once as described in
[firmware updates](firmware-updates.md), back it up outside the repo, and reuse
it for every update. Never overwrite an existing key or use a disposable CI key
on hardware.

```sh
idf.py -B build-encrypted -D SDKCONFIG=sdkconfig.encrypted.local \
  -D 'SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.frame;sdkconfig.ota;sdkconfig.encrypted' menuconfig
idf.py -B build-encrypted -D SDKCONFIG=sdkconfig.encrypted.local \
  -D 'SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.frame;sdkconfig.ota;sdkconfig.encrypted' build
```

Omit `sdkconfig.frame` for boards without its GPIO13 wiring. Configure the
recovery password and network before provisioning. Confirm these generated
settings before installation:

- `CONFIG_SECURE_FLASH_ENC_ENABLED=y`
- `CONFIG_SECURE_FLASH_ENCRYPTION_MODE_RELEASE=y`
- `CONFIG_NVS_ENCRYPTION=y`
- `CONFIG_NVS_SEC_KEY_PROTECT_USING_FLASH_ENC=y`
- `CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.encrypted.csv"`
- `CONFIG_PARTITION_TABLE_OFFSET=0x10000`
- Signed OTA and boot rollback enabled as in the OTA guide.

`partitions.encrypted.csv` uses a partition table at `0x10000` to accommodate
the larger encryption bootloader. NVS moves to `0x11000` (24 KiB), PHY data to
`0x17000`, and the first application slot to `0x20000` (size `0x1e0000`). The
second slot remains at `0x200000` (size `0x1f0000`), OTA metadata at `0x3f0000`,
and a new 4 KiB `nvs_keys` partition at `0x3f2000` has the `encrypted` flag.
Signed images must fit the smaller first slot. The NVS data partition itself
must not have that flag: its entries are encrypted by the NVS library. Startup
loads keys through IDF and calls its explicit secure NVS initializer. New
device-specific keys are generated only when IDF reports an uninitialized key
partition and the entire NVS data partition is erased. Corrupt/unreadable keys
are never regenerated automatically. Temporary key material is zeroized.

## First installation and migration

Use a spare/unprovisioned ESP32 for initial hardware acceptance. Check its eFuse
state before following Espressif's release-mode provisioning procedure. Ensure
the image, partition table and bootloader all come from the encrypted build,
and keep power stable during first-boot encryption. Do not enable this through
an application-only OTA upload: it cannot provision the bootloader or layout.

There is **no in-place conversion of existing plaintext NVS** in this project.
Migrating an existing unencrypted device requires deliberate USB reprovisioning
with a full flash erase to remove the old layout, plaintext credentials and
old OTA images. Record settings beforehand and expect to sign in again. This
erase is destructive and is not part of normal firmware updates. Do not
apply generic erase/reflash commands to an already encrypted device. Follow the
SDK's procedure appropriate to its actual eFuse state.

Encrypted startup refuses to proceed if hardware flash encryption is inactive.
It returns NVS/key initialization errors without automatically erasing storage
or falling back to plaintext. Conversely, unencrypted firmware refuses storage
initialization when hardware flash encryption is active. This prevents a wrong
OTA build from wiping credentials through legacy NVS recovery. Authentication
and networking start only after successful storage initialization.

## Updates and recovery

Keep the encrypted configuration and original signing key for every subsequent
build. Upload the signed `build-encrypted/daddy-status.bin` through the existing
OTA endpoint; IDF writes the application to encrypted flash. Never erase or
replace `nvs_keys` during updates. Losing these keys makes existing NVS contents
unreadable. Reset Microsoft sign-in through the browser to clear authorization
without replacing encryption keys or erasing other settings.

With release mode and a device-generated flash key, ordinary plaintext USB
reflashing is unavailable. Preserve working signed OTA access and follow the
SDK's release-mode recovery limitations. Hardware Secure Boot, migration that
preserves plaintext credentials, and physical attack resistance beyond flash
readout are outside this profile's scope.

## Validation

CI builds the signed encrypted profile as well as existing configurations. Host
tests cover both encryption modes, configuration/device mismatches, NVS errors,
non-destructive encrypted failures and legacy unencrypted recovery. These tests
mock the SDK boundary; they do not prove hardware cryptography or provisioning.

Before deploying to the frame, validate on a spare device:

1. Check release-mode eFuse state and successful encrypted NVS initialization.
2. Save settings and sign in, then reboot and confirm refresh-token reuse.
3. Inspect a raw flash readout, where permitted, for known test credential values;
   verify the key partition is protected and credentials are not plaintext.
4. Reset sign-in, reauthenticate, and verify settings survive.
5. Apply a signed encrypted OTA update and verify settings and sign-in survive.
6. Verify an incompatible unencrypted test update cannot initialize NVS and
   rolls back, preserving credentials. Test missing/corrupt keys only on a
   disposable device; startup must fail without erasing NVS.

Hardware provisioning and these acceptance checks have not yet been performed.
