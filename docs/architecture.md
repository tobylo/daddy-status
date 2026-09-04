# Architecture

`main.c` initializes the platform and consumes complete status snapshots. `graph_client.c`
owns the networking task and orchestrates authentication and presence polling.
The remaining boundaries are:

| Module | Responsibility |
| --- | --- |
| `http_transport` | Verified TLS, request lifetime, bounded response collection |
| `auth_client` | Device flow, refresh flow, access token ownership and expiry |
| `token_storage` | NVS persistence, legacy access-token removal, storage errors |
| `protocol` | Byte-wise form encoding, response buffers, checked JSON fields |
| `presence` | Pure mapping of Graph activities to application presence |
| `wifi` | Station setup and connection events |
| `app_state` | Pure display policy, including freshness and connectivity |
| `led_frame` | Pure RGB frame generation and animation timing |
| `ledcontrol` | One persistent task that owns the LED driver |

One network worker owns an `auth_client_t`; its token pointer remains valid until
that owner refreshes or invalidates it. HTTP callers always free responses, even
on errors. NVS handles are local to each storage operation. A missing/unusable
stored token becomes `ESP_ERR_NOT_FOUND`; transport and storage failures remain
separate errors and must not force a fresh login.

Host tests compile the production authentication, storage, protocol, and presence
code. Fake HTTP responses, NVS, and a monotonic clock allow deterministic checks
of pending/slowdown, expiry, revocation, rotation, throttling, missing/corrupt
storage, and response errors. These fakes do not validate TLS, radio behavior,
flash atomicity, RMT timing, or LED appearance; those require hardware checks.

The network worker overwrites a length-one queue with complete `app_status_t`
snapshots. Only successful presence responses update the freshness timestamp.
The main loop evaluates display policy every 100 ms, even while networking waits
for login, reconnection, or a throttling delay. A second length-one queue carries
display modes to the LED task; animations never create or delete tasks. Desired
colors are complete frames by value. Failed output is logged and retried without
blocking mode reception indefinitely.

Wi-Fi event callbacks update connection bits and notify a reconnect worker.
Reconnect attempts have a 1–30 second backoff. Connection loss is visible within
the display loop interval. Temporary HTTP/authentication failures preserve a fresh
presence until its configured timeout, then show unknown. Successful token
refresh alone never means "available".
