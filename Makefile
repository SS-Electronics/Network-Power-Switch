# File:   Makefile  (top-level)
# Module: Network-Power-Switch build entry point
# Info:   STM32F401CDU6 + ESP8266 (ESP-AT) relay controller on FreeRTOS-OS.
#         FreeRTOS-OS is used unmodified; everything board-specific is
#         configured from app/ (see app/kconfig_f401.conf header).
#
# Build:
#   make                 board/IRQ gen + Kconfig + compile → FreeRTOS-OS/build/nps.elf
#   make rebuild         clean + full build (use after editing any header:
#                        the OS build has no header dependency tracking)
#   make flash           program nps.elf over SWD (OpenOCD, ST-LINK)
#   make size            section sizes of the last build
#   make test            host unit tests (gcc + ASan/UBSan, no board needed)
#   make clean           remove build artefacts and generated board files
#
# Generated (gitignored) outputs land in app/board/.

OS_DIR       := FreeRTOS-OS
APP_SRC      := app
APP_DIR_OS   := ../$(APP_SRC)
TARGET_NAME  := nps
CONFIG_BOARD := nps_f401
KCONF        := $(APP_SRC)/kconfig_f401.conf
ELF          := $(OS_DIR)/build/$(TARGET_NAME).elf
BOARD_CFG_H  := $(APP_SRC)/board/board_config.h

OS_MAKE      := $(MAKE) -C $(OS_DIR) APP_DIR=$(APP_DIR_OS) \
                TARGET_NAME=$(TARGET_NAME) CONFIG_BOARD=$(CONFIG_BOARD)

.PHONY: all app gen config rebuild flash size test clean

all: app

# ── Board BSP + IRQ table generation ────────────────────────────────────
# The OS generator only knows the F411 clock tree (HSI 16 MHz → 100 MHz).
# The F401 is rated 84 MHz, so the generated macros are rewritten:
#   HSI 16 /M16 = 1 MHz ×N336 = 336 MHz VCO, /P4 = 84 MHz SYSCLK, /Q7 = 48 MHz
#   APB1 = /2 = 42 MHz, APB2 = /1 = 84 MHz (dividers fixed in the OS RCC
#   init), flash 2 wait states at 84 MHz / 3.3 V (RM0368 Table 5).
gen:
	@python3 $(OS_DIR)/scripts/gen_irq_table.py $(APP_SRC)/board/irq_table.xml \
		--outdir $(APP_SRC)/board
	@python3 $(OS_DIR)/scripts/gen_board_config.py \
		$(APP_SRC)/board/$(CONFIG_BOARD).xml --outdir $(APP_SRC)/board
	@sed -i -E \
		-e 's/^(#define BOARD_RCC_PLLN +)200U/\1336U/' \
		-e 's/^(#define BOARD_RCC_PLLP +)RCC_PLLP_DIV2/\1RCC_PLLP_DIV4/' \
		-e 's/^(#define BOARD_RCC_PLLQ +)4U/\17U/' \
		-e 's/^(#define BOARD_SYSCLK_HZ +)100000000UL/\184000000UL/' \
		-e 's/^(#define BOARD_APB1_HZ +)50000000UL/\142000000UL/' \
		-e 's/^(#define BOARD_APB2_HZ +)100000000UL/\184000000UL/' \
		-e 's/^(#define BOARD_FLASH_LATENCY +)FLASH_LATENCY_3/\1FLASH_LATENCY_2/' \
		$(BOARD_CFG_H)
	@grep -qE '^#define BOARD_RCC_PLLN +336U'          $(BOARD_CFG_H) && \
	 grep -qE '^#define BOARD_RCC_PLLP +RCC_PLLP_DIV4' $(BOARD_CFG_H) && \
	 grep -qE '^#define BOARD_RCC_PLLQ +7U'            $(BOARD_CFG_H) && \
	 grep -qE '^#define BOARD_FLASH_LATENCY +FLASH_LATENCY_2' $(BOARD_CFG_H) || \
	 { echo "### ERROR: 84 MHz clock rewrite of $(BOARD_CFG_H) failed (generator output changed?)"; exit 1; }

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
	@arm-none-eabi-size -A -x $(ELF) | grep -E "isr|text|rodata|data|bss|heap|Total"
	@arm-none-eabi-size $(ELF)

# ── Host unit tests: pure-C modules only (no OS / HAL) ──────────────────
HOST_CC := gcc -std=c99 -Wall -Wextra -Werror -fsanitize=address,undefined -g -I$(APP_SRC)/inc

test:
	@mkdir -p build-host
	@$(HOST_CC) $(APP_SRC)/test/test_cmd_codec.c $(APP_SRC)/src/cmd_codec.c -o build-host/test_cmd_codec
	@$(HOST_CC) $(APP_SRC)/test/test_at_parse.c $(APP_SRC)/src/at_parse.c -o build-host/test_at_parse
	@./build-host/test_cmd_codec
	@./build-host/test_at_parse

clean:
	@$(OS_MAKE) clean
	@rm -rf build-host
