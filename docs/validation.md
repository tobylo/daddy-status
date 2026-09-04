# Validation and device acceptance

## Automated checks

Run `idf.py build`, then `tests/run.sh` from an exported ESP-IDF 6.1 environment.
Host tests require a C compiler with AddressSanitizer and UndefinedBehaviorSanitizer.
They compile production code, with fakes only at hardware/network boundaries.

| Suite | Coverage |
| --- | --- |
| Protocol | Split/exact-limit/oversized buffers, encoding, malformed and deeply nested JSON, typed fields, retry headers, IDs and bearer tokens |
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

## Hardware acceptance — not yet performed

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

Live Microsoft authentication, TLS negotiation, flash persistence under power
loss, Wi-Fi/RMT timing, and the 24-hour soak remain unverified until these checks
are performed on the intended hardware/account.
