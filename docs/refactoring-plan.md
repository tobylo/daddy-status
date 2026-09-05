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
