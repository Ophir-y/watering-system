# PlantCare convenience targets. Run from WSL / Linux.
#   make test        run firmware unit tests (host) + dashboard checks
#   make all         build dashboard, embed it, build firmware
#   make flash       build everything and flash over USB
#   make ota DEVICE=plantcare.local PASSWORD=...   upload firmware over Wi-Fi
#   make mock        run the dashboard against the in-browser simulated device

SHELL := /bin/bash
FW    := firmware
DASH  := dashboard
PIO   ?= pio
DEVICE ?= plantcare.local
FW_BIN := $(FW)/.pio/build/esp32dev/firmware.bin

.PHONY: all dashboard firmware test test-firmware test-dashboard flash ota monitor mock dev clean

all: dashboard firmware

$(DASH)/node_modules: $(DASH)/package-lock.json
	cd $(DASH) && npm ci
	@touch $@

dashboard: $(DASH)/node_modules
	cd $(DASH) && npm run build:embed

firmware:
	cd $(FW) && $(PIO) run -e esp32dev

test: test-firmware test-dashboard

test-firmware:
	cd $(FW) && $(PIO) test -e native

test-dashboard: $(DASH)/node_modules
	cd $(DASH) && npm run lint && npm run typecheck && npm test

flash: all
	cd $(FW) && $(PIO) run -e esp32dev -t upload

ota: all
	@test -n "$(PASSWORD)" || (echo "usage: make ota DEVICE=plantcare.local PASSWORD=<admin password>"; exit 1)
	@TOKEN=$$(curl -fsS -X POST -H 'Content-Type: application/json' \
	    -d '{"password":"$(PASSWORD)"}' http://$(DEVICE)/api/login | sed -E 's/.*"token":"([^"]+)".*/\1/') && \
	  curl -fsS -H "Authorization: Bearer $$TOKEN" -F "firmware=@$(FW_BIN)" http://$(DEVICE)/api/system/ota && echo

monitor:
	cd $(FW) && $(PIO) device monitor

mock: $(DASH)/node_modules
	cd $(DASH) && npm run dev:mock

dev: $(DASH)/node_modules
	cd $(DASH) && npm run dev

clean:
	rm -rf $(FW)/.pio $(DASH)/dist $(FW)/src/generated/web_assets.h
