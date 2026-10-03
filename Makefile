# File:   Makefile  (top-level)
# Module: Network-Power-Switch build entry point
# Info:   NUCLEO-H723ZG Ethernet relay controller on FreeRTOS-OS.
#         The image is linked at 0x08020000 behind the STM32H723 Ethernet
#         bootloader (app/board/nps_h723_app.ld).
#
# Build:
#   make                 board/IRQ gen + Kconfig + compile → FreeRTOS-OS/build/nps.elf
#   make rebuild         clean + full build (use after editing any header:
#                        the OS build has no header dependency tracking)
#   make flash           program nps.elf over SWD (OpenOCD); bootloader sector
#                        0 is never touched because the ELF starts at 0x08020000
#   make size            section sizes of the last build
#   make test            host unit tests (gcc + ASan/UBSan, no board needed)
#   make clean           remove build artefacts and generated board files
#
# Generated (gitignored) outputs land in app/board/.

OS_DIR       := FreeRTOS-OS
APP_SRC      := app
APP_DIR_OS   := ../$(APP_SRC)
TARGET_NAME  := nps
CONFIG_BOARD := nps_h723
KCONF        := $(APP_SRC)/kconfig_h723.conf
ELF          := $(OS_DIR)/build/$(TARGET_NAME).elf

OS_MAKE      := $(MAKE) -C $(OS_DIR) APP_DIR=$(APP_DIR_OS) \
                TARGET_NAME=$(TARGET_NAME) CONFIG_BOARD=$(CONFIG_BOARD)

.PHONY: all app gen config rebuild flash size test clean

all: app

# ── Board BSP + IRQ table generation ────────────────────────────────────
gen:
	@python3 $(OS_DIR)/scripts/gen_irq_table.py $(APP_SRC)/board/irq_table.xml \
		--outdir $(APP_SRC)/board
	@python3 $(OS_DIR)/scripts/gen_board_config.py \
		$(APP_SRC)/board/$(CONFIG_BOARD).xml --outdir $(APP_SRC)/board

# ── Kconfig: copy the preset only when it changed, then regenerate ──────
config:
	@cmp -s $(KCONF) $(OS_DIR)/.config || cp $(KCONF) $(OS_DIR)/.config
	@$(OS_MAKE) config-outputs

app: gen config
	@$(OS_MAKE) all

rebuild: gen config
	@$(OS_MAKE) clean
	@$(MAKE) gen
	@$(OS_MAKE) all

flash: $(ELF)
	@$(OS_MAKE) flash

size: $(ELF)
	@arm-none-eabi-size -A -x $(ELF) | grep -E "isr|text|rodata|data|bss|axi|eth_d2|Total"
	@arm-none-eabi-size $(ELF)

# ── Host unit tests: pure-C modules only (no OS / HAL) ──────────────────
TEST_BIN := build-host/test_cmd_codec

test:
	@mkdir -p build-host
	@gcc -std=c99 -Wall -Wextra -Werror -fsanitize=address,undefined -g \
		-I$(APP_SRC)/inc $(APP_SRC)/test/test_cmd_codec.c $(APP_SRC)/src/cmd_codec.c \
		-o $(TEST_BIN)
	@./$(TEST_BIN)

clean:
	@$(OS_MAKE) clean
	@rm -rf build-host
