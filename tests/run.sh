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
TEST_FLAGS=(-DCJSON_NESTING_LIMIT=16 -D_GNU_SOURCE -std=c11 -Wall -Wextra -Werror
    -Wno-unused-parameter -g -fsanitize=address,undefined
    -Itests/fakes -Imain/include -I"$CJSON")
run_test() {
    local name=$1
    shift
    "${CC:-cc}" "${TEST_FLAGS[@]}" "$@" -lm -o "$TEST_BUILD/$name"
    "$TEST_BUILD/$name"
}
run_test improv_serial tests/test_improv_serial.c main/improv.c
run_test improv tests/test_improv.c main/improv.c
run_test protocol tests/test_protocol.c main/protocol.c "$CJSON/cJSON.c"
run_test auth tests/settings_stub.c tests/test_auth.c main/auth_client.c main/token_storage.c main/presence.c \
    main/protocol.c "$CJSON/cJSON.c"
run_test state tests/test_state.c main/app_state.c main/led_frame.c
run_test led_worker tests/settings_stub.c tests/test_led_worker.c main/ledcontrol.c main/led_frame.c main/diagnostics.c
run_test led_coarse_ticks tests/settings_stub.c -DTEST_FREERTOS_HZ=1 tests/test_led_worker.c \
    main/ledcontrol.c main/led_frame.c main/diagnostics.c
run_test http tests/test_http.c main/http_transport.c main/protocol.c "$CJSON/cJSON.c"
run_test web tests/settings_stub.c tests/test_web.c main/protocol.c "$CJSON/cJSON.c"
run_test graph_worker tests/settings_stub.c tests/test_graph_worker.c main/protocol.c main/presence.c \
    main/app_state.c main/diagnostics.c "$CJSON/cJSON.c"
run_test wifi_worker tests/test_wifi_worker.c
run_test poll_timing tests/test_poll_timing.c

run_test settings tests/test_settings.c main/protocol.c "$CJSON/cJSON.c"
run_test ota -DCONFIG_FRAME_OTA_ENABLE=1 -DCONFIG_SECURE_SIGNED_ON_UPDATE=1 \
    -DCONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=1 tests/test_ota.c
run_test ota_disabled tests/test_ota.c

run_test storage_plain -DCONFIG_NVS_ENCRYPTION=0 tests/test_storage.c main/storage.c
run_test storage_encrypted -DCONFIG_NVS_ENCRYPTION=1 \
    -DCONFIG_NVS_SEC_KEY_PROTECT_USING_FLASH_ENC=1 tests/test_storage.c main/storage.c
