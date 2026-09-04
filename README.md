# daddy-status

ESP32 application that polls current Microsoft Teams presence status via their Presence Graph API (preview).
Originally written using ESP-IDF v4.0; the refactor builds with ESP-IDF v6.1.

Hardware required:
- ESP32
- two WS2812 RGBs

## Demo
[![demo](https://img.youtube.com/vi/txYKa6VPBUU/0.jpg)](https://www.youtube.com/watch?v=txYKa6VPBUU)

## TODO
- write how-to

## libraries

Espressif managed `led_strip` component (replaces the original Lucas Bruder driver).

encode_uri by David Farrell
https://github.com/dnmfarrell/URI-Encode-C

## Build (ESP-IDF 6.1)

The refactor targets a classic ESP32 and pins managed dependencies in
`main/idf_component.yml` and `dependencies.lock`. Install ESP-IDF **v6.1** and its
ESP32 tools, then:

```sh
. /path/to/esp-idf/export.sh
idf.py menuconfig  # Daddy Status and WiFi setup: use your own configuration
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

The default build uses placeholder credentials and does not connect to your
account. `sdkconfig` is ignored because it contains your Wi-Fi password. Keep
firmware images and build output private when built with real credentials.
The old GNU Make build is no longer supported. See
[the staged refactoring plan](docs/refactoring-plan.md) for remaining work.

## Automated checks

After `idf.py build` downloads the pinned cJSON component, run `tests/run.sh`
with a host C compiler. It checks response chunk assembly, exact buffer limits,
form encoding, malformed JSON, and retry headers under AddressSanitizer and
UndefinedBehaviorSanitizer. CI runs these checks after compiling the firmware.

HTTPS uses the ESP-IDF CA bundle and waits for SNTP clock synchronization. The
network must permit DNS, NTP, and HTTPS to Microsoft. Access tokens remain in
RAM; refresh tokens persist in the `graphapi` NVS namespace. Existing stored
refresh tokens are reused; obsolete stored access tokens are erased when tokens
are updated. This does not enable flash encryption: physical flash protection
requires separate device provisioning. Device-code login instructions appear on
the serial monitor; token values and authorization headers are not logged.
