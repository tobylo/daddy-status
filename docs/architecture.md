# Architecture

`main.c` initializes the platform and consumes status updates. `graph_client.c`
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
| `ledcontrol` | Rendering (single-task redesign in stage 4) |

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
