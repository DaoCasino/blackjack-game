#! /bin/bash


set -e -o pipefail

. "${BASH_SOURCE[0]%/*}/utils.sh"

log "=========== Running unit tests ==========="
(
    ./build/tests/blackjack_unit_test $@
)

log "=========== Running debug unit tests ==========="
(
    ./cicd/build.sh --debug
    ./build-debug/tests/blackjack_unit_test $@
)