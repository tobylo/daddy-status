# Refactoring plan

Each numbered stage is a separate PR. Build and test it, wait for CodeRabbit's
review, evaluate and address valid findings, verify the final revision, and
squash-merge before starting the next stage. Never bypass a missing review.

1. Build foundation: pin ESP-IDF 6.1 and managed dependencies, modernize the
   required Wi-Fi/RMT APIs, repair declarations, establish CI and build docs.
2. Correctness: bounded HTTP responses, checked JSON and storage, verified TLS,
   token redaction, explicit OAuth errors, cleanup and retry behavior.
3. Module separation: HTTP transport, authentication, token storage, presence
   mapping; host tests for protocol and parsing edge cases.
4. Application state: a persistent LED task, typed states, startup feedback,
   connectivity and freshness handling, bounded retry and latest-state delivery.
5. Finish: Graph v1.0, configuration cleanup, regression coverage, setup,
   wiring, login, recovery and hardware acceptance documentation.

Hardware acceptance includes reconnects, token revocation, rapid display changes,
and extended heap/stack observation. A compile or host test does not establish
hardware acceptance. Retain the original classic ESP32 and two WS2812 LEDs unless
the owner requests a change. Do not store real credentials in source control.

## Follow-up: browser authentication

1. Serve a small local page and publish device-code sign-in state without tokens.
2. Verify expiry, success/error cleanup, startup failures, and target compilation;
   document discovery and picture-frame acceptance checks.
3. Open one focused PR, address valid CodeRabbit findings, and merge after CI.

## Operational improvements after hardware testing

1. Credential-free frame profile, dated hardware results and Wi-Fi/Graph worker
   regression tests. PR/review/merge; no device behaviour change.
2. Event-driven Wi-Fi reconnection and deadline-based presence cadence; classify
   persistent versus transient failures. PR/review, then pause for hardware
   reboot, disconnect/reconnect, status latency and saved-login verification.
3. Status dashboard and bounded LED test controls; pause for on-device UI/LED
   checks before proceeding.
4. Validated NVS settings, browser setup/recovery and authorization reset;
   pause for provisioning, invalid-setting rollback and restart verification.

Each stage has its own commit/PR and CodeRabbit review. Valid findings are fixed
before merge. Hardware-dependent work does not proceed past its verification
checkpoint without the owner's results.

### Stage 4 implementation design

Stage 3 hardware acceptance was received on 2026-09-07. Stage 4 remains in
progress and must have its own reviewed PR before flashing.

1. Introduce a versioned settings module with compile-time defaults and validated
   NVS overrides. Editable fields: Wi-Fi SSID/password, tenant/client GUIDs, NTP
   host, polling/stale intervals and brightness. Keep the wired GPIO compile-time
   configured. Never return stored passwords through the API or log credentials.
2. Apply settings through a controlled reboot, retaining an immutable settings
   snapshot for each boot. Reject invalid fields before writing flash. Preserve
   known-good settings while trying changed Wi-Fi credentials; promote only after
   connection succeeds and roll back on timeout or an interrupted trial boot.
3. Keep the browser available when Microsoft configuration is missing or invalid.
   The owner selected a password-protected temporary setup network for recovery
   when station Wi-Fi is unavailable.
   Do not enable an open access point or invent a shared default setup password.
4. Extend the dashboard with settings and explicit save/restart feedback. Reuse
   bounded request parsing and the per-boot request token. Reject overlapping
   mutations and preserve usable status feedback across restart/disconnection.
   The LAN interface remains a trusted-network interface, not user authentication.
5. Add explicit Microsoft sign-in reset. Persist the reset intent and process it
   before starting the authentication worker, avoiding a race where an in-flight
   token refresh restores credentials after reset. Changing tenant/client IDs
   must not reuse authorization from a different registration.
6. Cover validation boundaries, NVS read/write failures, interrupted trial boots,
   Wi-Fi rollback, password redaction, reset intent and browser error handling.
   Build all CI profiles, open the PR, wait for CodeRabbit, fix valid findings and
   merge. Pause for hardware save/restart, invalid-setting rejection, failed Wi-Fi
   recovery and sign-in-reset verification before declaring the stage accepted.
