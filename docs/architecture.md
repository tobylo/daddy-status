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
| `wifi` | Station connection events and password-protected recovery AP |
| `settings` | Immutable boot snapshot, versioned NVS record, trial/rollback and reset intent |
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

The LED worker is also compiled against fake queue and driver interfaces to test
initialization cleanup, latest-mode delivery, retry after output failure, and
mode changes within the same task. This checks control flow, not RTOS scheduling.

### Browser authentication

`web_server.c` serves an embedded, dependency-free page and a read-only JSON
endpoint using ESP-IDF HTTP Server. The authentication worker publishes only
its event, user code, and monotonic deadline through a synchronous observer.
A short critical section protects copies shared with the HTTP task; network
calls and JSON allocation happen outside it. Expired codes are withheld even
if the worker is waiting for Wi-Fi. Terminal results clear the code. Three
HTTP client sockets and five-second send/receive timeouts bound server use.
The existing network worker remains the only owner of OAuth tokens and polling.

### Connection and request timing

Only the Wi-Fi reconnect task initiates connections. Driver start, disconnect,
and IP-ready events are retained in event-group bits, so events arriving before
or during a wait are not lost. Association is logged but does not mark the
network ready. Each attempt has 30 seconds to obtain an IP; on timeout the task
cancels it and waits up to five seconds for disconnect acknowledgement. If that
fails, it stops/starts the station and waits for its start event before retrying.
Backoff grows from one to 30 seconds and resets after an IP-ready connection.
Unexpected stop/start SDK failures retain the application's fatal-error policy.

Successful Graph requests schedule the next start on a monotonic interval grid
relative to that cycle's start. Requests that overrun a slot skip it; retries
continue to use exponential delay and Retry-After. Authentication configuration
failures retry no sooner than five minutes; permission-denied Graph requests
retry no sooner than one minute without shortening a longer Retry-After.
`service_error_t` separates safe diagnostic categories from raw server payloads;
authentication owns its last category and the worker copies it into status.
The dashboard consumes these categories independently of the sign-in state.

### Status dashboard and temporary LED override

The main task publishes a complete app status/value snapshot to the HTTP module
under its existing short critical section. Graph polling copies the received
activity into a bounded 64-byte field; JSON serialization and browser textContent
preserve it as text. The dashboard exposes last-success age, freshness, selected
pattern and safe service-error categories, independently of OAuth sign-in state.

The HTTP task accepts only a small JSON LED-test request with the per-boot random
control token from the same-origin status response. No CORS permissions are
advertised. This blocks ordinary cross-site form/fetch requests; it is not user
authentication, and trusted LAN clients can read the token and operate tests.
Main checks a ten-second monotonic override deadline each iteration and remains
the only task feeding the LED mode queue. HTTP never touches the LED driver.
Tests expire without browser timers or a follow-up request. Cancel clears the
override; concurrent browser tests use the latest accepted request.


### Saved settings and recovery

Settings initialize after NVS and before all workers. A versioned blob contains
active/candidate settings, pending/tried markers and authorization-reset intent.
The module validates bounded strings, GUIDs and numeric ranges before writes.
Invalid stored records fall back to compiled defaults; invalid compiled defaults
fall back to empty recovery settings. General NVS I/O failures retain the fatal
startup policy. The boot snapshot is immutable; all modules read it without
runtime mutation. A mutex serializes HTTP mutations and main-task trial checks;
flash writes do not run in critical sections.

A save stages a candidate and schedules restart. Boot persists the tried marker
before exposing a candidate to workers. Main promotes it after DHCP success, or
restarts after three minutes to roll back; promotion write failures retry at
most every five seconds within that deadline. An interrupted trial also rolls
back. This checks station reachability, not Graph/NTP correctness. NVS's blob
storage is the persistence boundary; host mocks cannot establish physical
power-loss guarantees.

Reset requests and identity changes persist reset intent. Boot erases saved
authorization before starting the sole OAuth worker, then clears that intent;
a failed erase prevents startup. Rollback across identities also erases tokens.
The settings HTTP task never races the worker by erasing tokens in a live session.

The recovery AP's WPA2 configuration is installed before Wi-Fi starts, while
its interface is inactive. Main enables AP+station mode after three minutes
without an IP (immediately for empty SSID) and disables AP on connection.
The station worker continues retrying. Empty SSID skips attempts to avoid
repeated station restarts disrupting provisioning. The main task has a 6 KiB stack for NVS transitions; the HTTP server has an 8 KiB
stack and limits settings bodies to 1536 bytes. Recovery uses a private compiled
password, never an open AP; the trusted-LAN request-token model also applies to
settings and sign-in reset.
