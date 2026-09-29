# Convenience targets for the host harness. Run from firmware/host/.
#
#   make play    build, then open the live SDL window
#   make build   build only
#   make test    build, then run every host/scripts/*.txt against its goldens
#
# Everything is PlatformIO's [env:host] underneath; see README.md.

FW := ..
BIN := .pio/build/host/program

.PHONY: play build test

build:
	cd $(FW) && pio run -e host

play: build
	cd $(FW) && $(BIN) --fs-dir initial_filesystem

test:
	cd $(FW) && host/run-tests.sh
