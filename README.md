# Daddy Status

An ESP32 status light for the study door: read your Microsoft Teams presence and
show whether it is a good time to come in. Originally built during COVID, now
refactored for ESP-IDF **6.1**, with verified HTTPS, recoverable device login,
explicit stale-status handling, and one persistent LED task.

Uses Microsoft Graph [`GET /v1.0/me/presence`](https://learn.microsoft.com/en-us/graph/api/presence-get?view=graph-rest-1.0)
with the delegated `Presence.Read` permission. This requires a **work or school
account**; the endpoint does not support personal Microsoft accounts.

## Hardware

- Classic ESP32 development board with at least 4 MB flash.
- Two WS2812 LEDs connected in series.
- GPIO **25** to the first LED's data input; first LED's data output to the second
  LED's data input. GPIO is configurable.
- A suitable supply for the LEDs, with **common ground** between supply, LEDs,
  and ESP32. Follow the voltage and logic-level requirements for your particular
  LEDs; use a suitable level shifter when needed. Do not power the LED supply from
  an ESP32 GPIO. Avoid pins used for flash or other board hardware.

This build targets `esp32`, not ESP32-S3/C3 or other variants. A detected serial
port alone does not identify the attached board.

## Microsoft app registration

1. In your organization's Microsoft Entra tenant, [register an application](https://learn.microsoft.com/en-us/entra/identity-platform/quickstart-register-app).
   A single-tenant registration is sufficient. Record its **Directory (tenant) ID**
   and **Application (client) ID**, both GUIDs.
2. Enable **Allow public client flows** in the registration's authentication
   settings. Device-code authentication is a [public-client flow](https://learn.microsoft.com/en-us/entra/identity-platform/msal-client-applications).
   No client secret belongs in this firmware.
3. Add Microsoft Graph **delegated** `Presence.Read`. The firmware also requests
   `offline_access` to obtain a refresh token. Complete user/admin consent as
   required by your organization. Tenant policy must allow device-code login.
4. During first login, open the frame’s local web page (see below) and sign in
   as the user whose presence should drive the light. The [device-code flow](https://learn.microsoft.com/en-us/entra/identity-platform/v2-oauth2-device-code)
   can return pending, slowdown, expired, or denied responses; these are handled
   without an unbounded token-polling loop.

## Build and flash

Install [ESP-IDF v6.1 and the ESP32 tools](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32/get-started/).
The exact managed LED/cJSON versions and hashes are in `main/idf_component.yml`
and `dependencies.lock`.

```sh
. /path/to/esp-idf/export.sh
idf.py menuconfig
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

Use the actual serial port of this device, such as `/dev/ttyACM0` or `COM3` where
appropriate. Exit the serial monitor with **Ctrl+]**. To use the included VS Code
build/flash/monitor tasks, launch VS Code from the shell after exporting ESP-IDF.
Flash and monitor tasks prompt for the port. Debugger configuration is specific
to your board and probe and is not supplied.

In `menuconfig`, set the initial defaults (saved browser settings take precedence):

| Setting | Default / notes |
| --- | --- |
| Wi-Fi SSID / password | Empty; supply a 2.4 GHz network. Empty password only for an open network. |
| Tenant / client IDs | Empty; supply both GUIDs from the app registration. |
| LED data GPIO | 25; choose an output pin appropriate for your board. |
| Presence poll interval | 15 seconds |
| Stale timeout | 60 seconds; must exceed the polling interval |
| LED brightness | 100%; rainbow also applies its intended 30% intensity |
| NTP server | `pool.ntp.org`; replace if your network requires another server |
| Status diagnostics | Off; enable for heap/stack observation |
| Recovery Wi-Fi password | Empty disables recovery; configure a private 12–63 character password before flashing |

With a configured recovery password, missing Wi-Fi settings enable setup Wi-Fi.
Missing Microsoft settings show magenta. The page remains reachable through
connected station Wi-Fi or an enabled recovery AP. The
network must permit DNS, NTP, and HTTPS to Microsoft. HTTPS waits for clock
synchronization and uses the ESP-IDF CA bundle; do not disable certificate checks
to work around network or clock failures.

`sdkconfig`, build output, and managed downloads are ignored. Wi-Fi credentials
may be compiled into firmware defaults and saved in NVS, so keep configured builds private. Access tokens
stay in RAM; refresh tokens persist in NVS and survive normal reflashing. The
application does not log token values or authorization headers. Keep SDK HTTP/TLS
logging at its normal level when handling live credentials. Flash encryption is
not enabled by this project; physical flash protection needs separate device
provisioning.

## What the lights mean

| Display | Meaning |
| --- | --- |
| Green | Available, away, be-right-back, or idle |
| One yellow LED | Busy |
| Blinking red | Do not disturb, meeting, call, or presenting |
| Rainbow | Off work, offline, or out of office |
| Blinking yellow | Initial authentication in progress; open the frame’s web page |
| Blinking blue | Connecting, synchronizing clock, unknown, or stale presence |
| Solid magenta | Invalid configuration; check serial monitor |

Temporary service/authentication errors preserve a fresh presence until its stale
timeout. Wi-Fi loss shows blue as soon as the display loop observes it (normally
within 100 ms). Unknown Graph activities show blue, never green. Receiving a token
alone does not indicate availability. Away/offline mappings preserve the original
project's behavior and are centralized in `main/presence.c`.

## Recovery

- **Blue at startup:** check Wi-Fi and NTP access. If the clock is synchronized,
  check Microsoft connectivity and serial logs. Reconnects and transient requests
  retry with backoff; numeric `Retry-After` values are respected up to a 24-hour
  operational maximum. Larger values are capped to prevent an effectively
  permanent pause.
- **Yellow:** finish device login on the frame’s web page. Expired/denied device
  codes finish that attempt; the worker can start another attempt after backoff.
- **Magenta:** open Settings and recovery on the frame page and complete the
  Microsoft configuration. The page is reachable when station Wi-Fi is connected
  or a valid recovery AP is enabled.
- **HTTP 403:** check delegated permission, consent, and tenant policy. Repeated
  token refresh does not solve a permission denial.
- **Revoked refresh token:** the device returns to the device-code login flow.
- **Change account:** use Reset Microsoft sign-in on the frame page. It restarts
  and clears local authorization while retaining settings. This does not revoke
  consent at Microsoft. For a deliberate full device erase and reflash:

  ```sh
  idf.py -p /dev/ttyUSB0 erase-flash
  idf.py -p /dev/ttyUSB0 flash monitor
  ```

  Erasing flash deletes all device data, including saved authorization. It is not
  required for normal updates. NVS also reinitializes if the SDK reports an
  incompatible format or exhausted pages; that recovery requires login again.

## Development and validation

```sh
idf.py build          # also downloads the pinned cJSON sources
./tests/run.sh        # host C compiler + AddressSanitizer/UndefinedBehaviorSanitizer
clang-format -i main/*.c main/include/*.h tests/*.c tests/fakes/*.h tests/fakes/freertos/*.h
```

CI compiles default and diagnostics-enabled firmware and runs the host suites.
Tests cover parsing/encoding, authentication, storage, presence mapping, state
freshness, LED frames and worker control flow, and HTTP response handling. Parser
nesting is limited to 16 to fit the embedded stack. See [architecture](docs/architecture.md),
[validation and hardware acceptance](docs/validation.md), and the
[refactoring stages](docs/refactoring-plan.md).

The old GNU Make build and copied LED/URI libraries have been removed. LED output
uses Espressif's managed `led_strip`; JSON uses managed `cjson`, with upstream
licenses retained in their downloaded packages. Form encoding is implemented
locally with bounded allocation and byte-wise writes.

## Original demo

[![demo](https://img.youtube.com/vi/txYKa6VPBUU/0.jpg)](https://www.youtube.com/watch?v=txYKa6VPBUU)

## Sign in without opening the picture frame

On a phone or computer on the same Wi-Fi, open `http://<frame-ip>/`. Find the
frame in your router’s connected-device list under the DHCP hostname
`daddy-status`; reserve its address there for a stable bookmark. The IP address
is also logged at connection time if a serial monitor is available. Hostname
resolution depends on your router; this firmware does not advertise mDNS.

The page displays the current Microsoft user code and a sign-in link. Open the
link, enter the code, and return to the page to see confirmation. Codes expire
and are replaced automatically; saved authorization survives power loss. No
redirect URI or confidential-client secret is needed. The dashboard reports sign-in separately from presence freshness and service errors.

The server uses local HTTP on port 80 and exposes the temporary user code to
other users of that network. Use a trusted home LAN and do not forward its port
to the internet. Microsoft passwords are entered only on Microsoft’s HTTPS site;
access, refresh, and opaque device tokens are never served. Responses are not
cached. Wi-Fi and Entra settings can be changed under Settings and recovery.

## Reproduce the tested picture frame

`sdkconfig.frame` records GPIO13, brightness 100%, a 10-second presence interval,
and a 60-second stale threshold. Generic defaults still support other wiring.
For a **fresh** local build, use:

```sh
idf.py -B build-frame -D SDKCONFIG=sdkconfig.frame.local \
  -D 'SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.frame' menuconfig
idf.py -B build-frame -D SDKCONFIG=sdkconfig.frame.local \
  -D 'SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.frame' build
```

Enter Wi-Fi and Entra settings in menuconfig. Existing sdkconfig values take
precedence over defaults, so this profile does not overwrite an existing device
configuration. The generated local configuration and build directory are ignored.
Hardware results and remaining checks are in `docs/validation.md`.

Successful presence requests are scheduled on the configured interval rather
than sleeping a full interval after each request. Slow requests skip missed
slots; service failures and throttling use separate retry delays. The Wi-Fi
worker waits up to 30 seconds for connection and DHCP before cancelling/retrying.

## Frame dashboard

Open the frame's local page to see Teams activity, last successful update,
fresh/stale status, Wi-Fi connectivity, Microsoft sign-in, selected light pattern,
polling interval, LED GPIO, brightness and uptime. Error messages distinguish
clock synchronization, permissions, application registration and transient failures.
A selected pattern reports software intent; it cannot detect unplugged LEDs.

The LED test buttons override normal display for ten seconds on the device,
then return to Teams automatically, even if the browser closes. Return to Teams
cancels immediately. Yellow tests the first LED only, matching busy mode.
Any trusted LAN client can operate the tests. The per-boot request token prevents
ordinary cross-site submissions; it does not replace network access control.
`/api/auth` remains available for compatibility; `/api/status` drives the dashboard.


## Saved settings and recovery Wi-Fi

The page can save Wi-Fi, tenant/client IDs, time server, polling/stale intervals
and brightness. GPIO stays a build setting so browser input cannot change wiring.
Leave the password blank to keep it, or explicitly select an open network to
clear it. Station passwords must be 8–63 printable ASCII characters or a
64-digit hexadecimal PSK. Stored passwords are never sent back by the API. Saving changes
restarts the device after two seconds. Tenant/client changes require sign-in
again; ordinary brightness or timing changes retain authorization.

Each save starts a three-minute connection trial on the next boot. A successful
station IP connection promotes the settings; it does not prove Microsoft access
or time-server availability. A timeout or restart during the trial restores the
previous settings. Changes are rejected while a trial or restart is pending.
A storage error means a change is not confirmed; reload before retrying. Saved
settings survive normal flashes and override menuconfig defaults.

Configure a private **Recovery Wi-Fi password** in menuconfig before flashing.
After three minutes without station Wi-Fi, the frame enables WPA2 network
**Daddy-Status-Setup**. With no configured SSID it starts immediately. Connect
from your phone, accept staying on a network without internet, and open
**http://192.168.4.1/** manually. There is no captive portal or internet routing.
The setup network turns off when station Wi-Fi reconnects. Failed connection
trials reboot once to roll back; recovery Wi-Fi can take another three minutes
if the previous network is also unavailable. An empty/invalid setup password
keeps the AP disabled, leaving USB recovery available. Keep the password with
your private device configuration; it is never exposed through the dashboard.

`GET /api/settings` returns non-secret settings. `POST /api/settings` accepts
`action=save` with settings or `action=reset_auth`, requiring the same per-boot
`X-Frame-Token` header as LED controls. Anyone on the trusted LAN who can load
the page can change settings or reset sign-in. The local HTTP connection is not
encrypted; do not expose it to untrusted networks.
