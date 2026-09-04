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
