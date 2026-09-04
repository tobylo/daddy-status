#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
CJSON=managed_components/espressif__cjson/cJSON
if [[ ! -f "$CJSON/cJSON.c" ]]; then
    echo 'Run idf.py reconfigure first to download the pinned cJSON dependency.' >&2
    exit 1
fi
TEST_BUILD=$(mktemp -d)
trap 'rm -rf "$TEST_BUILD"' EXIT
cc -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
    -Imain/include -I"$CJSON" tests/test_protocol.c main/protocol.c "$CJSON/cJSON.c" \
    -lm -o "$TEST_BUILD/test_protocol"
"$TEST_BUILD/test_protocol"
cc -D_GNU_SOURCE -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
    -Itests/fakes -Imain/include -I"$CJSON" tests/test_auth.c \
    main/auth_client.c main/token_storage.c main/presence.c main/protocol.c "$CJSON/cJSON.c" \
    -lm -o "$TEST_BUILD/test_auth"
"$TEST_BUILD/test_auth"
cc -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
    -Imain/include -I"$CJSON" tests/test_state.c main/app_state.c main/led_frame.c \
    -o "$TEST_BUILD/test_state"
"$TEST_BUILD/test_state"
cc -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter -g -fsanitize=address,undefined \
    -Itests/fakes -Imain/include -I"$CJSON" tests/test_led_worker.c main/ledcontrol.c main/led_frame.c \
    -o "$TEST_BUILD/test_led_worker"
"$TEST_BUILD/test_led_worker"
