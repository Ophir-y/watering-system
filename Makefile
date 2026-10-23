# PlantCare convenience targets. Run from WSL / Linux.
#   make test        run firmware unit tests (host) + dashboard checks
#   make all         build the dashboard, embed it, build the main station and every node board
#   make flash       build everything and flash the main station over USB
#   make flash-node BOARD=supermini_c3   flash a plant node (supermini_c3, xiao_c3 or xiao_s3)
#   make ota DEVICE=plantcare.local PASSWORD=...   update the main station over Wi-Fi
#   make mock        run the dashboard against the in-browser simulated station and nodes

SHELL := /bin/bash
FW    := firmware
DASH  := dashboard
PIO   ?= pio
DEVICE ?= plantcare.local
BOARD ?= supermini_c3
FW_BIN := $(FW)/.pio/build/hub/firmware.bin

.PHONY: all dashboard firmware test test-firmware test-dashboard flash flash-node ota monitor mock dev clean

all: dashboard firmware

$(DASH)/node_modules: $(DASH)/package-lock.json
	cd $(DASH) && npm ci
	@touch $@

dashboard: $(DASH)/node_modules
	cd $(DASH) && npm run build:embed

firmware:
	cd $(FW) && $(PIO) run -e hub -e node_supermini_c3 -e node_xiao_c3 -e node_xiao_s3

test: test-firmware test-dashboard

test-firmware:
	cd $(FW) && $(PIO) test -e native

test-dashboard: $(DASH)/node_modules
	cd $(DASH) && npm run lint && npm run typecheck && npm test

flash: all
	cd $(FW) && $(PIO) run -e hub -t upload

flash-node:
	cd $(FW) && $(PIO) run -e node_$(BOARD) -t upload

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
