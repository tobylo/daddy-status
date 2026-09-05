# Validation and device acceptance

## Automated checks

Run `idf.py build`, then `tests/run.sh` from an exported ESP-IDF 6.1 environment.
Host tests require a C compiler with AddressSanitizer and UndefinedBehaviorSanitizer.
They compile production code, with fakes only at hardware/network boundaries.

| Suite | Coverage |
| --- | --- |
| Protocol | Split/exact-limit/oversized buffers, encoding, malformed and deeply nested JSON, typed fields, retry headers including the 24-hour operational ceiling, IDs and bearer tokens |
| Authentication/storage | Device pending/slowdown, expiry, refresh/rotation, missing and invalid NVS, storage errors, revocation, throttling, truncated response without forced login |
| State/frames | Freshness boundaries, recovery, disconnect/config priority, unknown activities, blink phases, brightness and rainbow |
| LED worker | Initialization cleanup, latest-mode queue behavior, failed-output retry, one task across mode changes, coarse RTOS ticks |
| HTTP wrapper | CA-bundle/redirect/timeout configuration, split data, overflow even when callback errors are ignored, incomplete reads, transport/init/header failure cleanup, GET/POST, invalid bearer headers |

CI builds both default and diagnostics-enabled configurations. A diagnostics
build can also be made in a fresh build directory with a separate SDK config:

```sh
idf.py -B /tmp/daddy-diagnostics-build \
  -D SDKCONFIG=/tmp/daddy-diagnostics-sdkconfig \
  -D 'SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.diagnostics' build
```

This produces an unconfigured test build; configure the real device separately.
With diagnostics enabled, active tasks periodically log global free/minimum heap
and their own minimum free stack in bytes. Blocked or sleeping tasks log their
next sample when they resume; the main loop keeps observing heap independently.

## Hardware acceptance — partially performed

A successful compile or simulated driver test does not establish physical
acceptance. Record board model, wiring, firmware commit, test time, and results
when running these checks. Never flash a port solely because it is detected.

1. Confirm the board is a classic ESP32 with two WS2812 LEDs wired as documented.
2. Flash a configured build and complete device login. Verify real Teams statuses
   against the light table, including calls, meetings, presenting, and off work.
3. Change statuses repeatedly, including rainbow → busy → available → DND. Check
   that animations stop immediately and the driver never freezes.
4. Disconnect/reconnect Wi-Fi. Verify blue during loss and automatic recovery,
   both for a short interruption and one longer than the stale timeout.
5. Block Microsoft access while retaining Wi-Fi. Confirm the last presence expires
   to blue, and the same presence is restored once requests succeed again.
6. Let access tokens refresh; revoke authorization and complete a new device login.
   Deny/let a device code expire and confirm a later attempt can succeed.
7. Reboot and normally reflash without erasing NVS; confirm saved refresh-token
   reuse. Test deliberate reset separately from a normal update.
8. Run at least 24 hours with diagnostics enabled. Compare free/minimum heap and
   task stack minima after normal polls, refreshes, reconnects, and mode changes;
   investigate sustained heap loss, tiny stack margins, resets, or watchdog events.

See the dated results below for completed checks. Abrupt power loss during a
write, extended Wi-Fi/RMT stress, and the 24-hour soak remain unverified.

### Browser sign-in acceptance checklist

- Find `daddy-status` in the router and open its IP from a phone on the same LAN.
- Complete first sign-in through Microsoft; verify confirmation and presence LEDs.
- Let a code expire and deny a login; verify replacement and removal of old codes.
- Disconnect Wi-Fi while the page is open; verify stale code removal and recovery.
- Reboot after login; verify stored authorization works without a new code.
- Check two browsers simultaneously and monitor heap during repeated polling.

Host tests cover server startup/route failure cleanup, JSON escaping and fields,
borrowed-code ownership, exact expiry boundaries, success/error clearing, and
production authentication observer events. They do not simulate the TCP stack
or actual FreeRTOS concurrency.

`node tests/test_browser.mjs` exercises the embedded page's actual script using
Node's built-in VM and a minimal DOM/fetch harness, including countdown expiry,
login confirmation, literal code rendering, network loss, and recovery. CI runs
this separately from the sanitized C suites. Visual layout and real browser/TCP
behavior remain part of hardware acceptance.

## Results recorded 2026-09-05

Classic ESP32-D0WD revision 1.0, two WS2812 LEDs powered from 5 V with common
ground and data on GPIO13. Firmware source b4544f9; local configuration uses a
10-second presence delay and 60-second stale threshold (`sdkconfig.frame`).
The actual tested build used the Git-ignored `sdkconfig`, which supplied Wi-Fi
credentials and Entra tenant/client IDs in addition to these hardware settings.
The new `sdkconfig.frame` captures only the non-secret settings; it was not the
source of the historical build. For a new build, follow the README commands with
`sdkconfig.defaults;sdkconfig.frame` and enter Wi-Fi/Entra settings into the
ignored `sdkconfig.frame.local`. The profile alone cannot run authentication.
No network credentials or Entra IDs are included in the profile.

- Microsoft device-code sign-in through the frame's page: passed.
- Verified HTTPS communication and successful Graph presence retrieval: passed.
- Saved authorization reused after restart and normal reflashing: passed.
- Physical blinking red for DND and green for Available: confirmed by owner.
- GPIO25 was incorrect for this frame; corrected to GPIO13 before LED acceptance.
- A dedicated 2.4 GHz WPA2 SSID worked. Earlier U7 association failures occurred
  with strong RSSI; the precise cause was not established. The vendor station
  example and restored project firmware both connected on the later test.
- USB adapter timeouts recovered after physically reconnecting USB.

Not yet verified: every presence mapping, controlled network/API outages,
revocation/denial/expiry on hardware, multi-browser stress, flash-write power
loss, and 24-hour stability. Desktop browser scripting tests are not substitutes
for these checks.

The host worker suites execute production `wifi.c` and `graph_client.c` with
scripted task, event, time and transport boundaries. They cover association
versus DHCP state, failed connection backoff, clock wait, 401 token recovery,
403 denial, 429 Retry-After, preserved freshness during errors and recovery.
They do not model simultaneous FreeRTOS scheduling or radio/TCP behaviour.

## Stage 2 hardware checkpoint — pending

After flashing the connection/polling changes, verify before proceeding:

1. Restart: reconnect to Wi-Fi and reuse saved Microsoft login; LEDs reflect Teams.
2. Toggle the frame's Wi-Fi access off for at least 45 seconds, then restore it:
   blue during loss, automatic reconnection, and correct presence afterward.
3. Toggle Available/DND several times: eventual green/red and no animation freeze.
   Poll starts should be about 10 seconds apart, plus Microsoft propagation delay.
4. Leave running for at least ten minutes; inspect logs for resets, repeated
   connecting-in-progress errors, and LED/transport errors.

Host tests cover timeout cancellation and station restart when cancellation is
not acknowledged; that rare path remains dependent on real SDK event delivery.
No router configuration or credentials are changed by this stage.
