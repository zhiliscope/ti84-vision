#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
mkdir -p build-tests
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -g \
  -Icommon common/wire.c tests/wire_test.c -o build-tests/wire_test
./build-tests/wire_test
python3 -m unittest discover -s tests -v
