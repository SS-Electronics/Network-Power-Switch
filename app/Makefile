# File:   app/Makefile
# Module: Network-Power-Switch application build fragment
# Info:   Included by FreeRTOS-OS/Makefile when APP_DIR is set.
#         Declares app object files (app-obj-y), extra include/define flags
#         (APP_INCLUDES) and the application linker script.
#         Object paths are relative to APP_DIR; anything outside app/ is
#         reached with ../ (built under FreeRTOS-OS/build/).

# ── Application sources ───────────────────────────────────────────────────
app-obj-y += app_main.o
app-obj-y += src/relay.o
app-obj-y += src/button.o
app-obj-y += src/cmd_codec.o
app-obj-y += src/mqtt_link.o
app-obj-y += src/entropy_hw.o

# ── Generated board files (top-level `make gen`) ──────────────────────────
app-obj-y += board/board_config.o
app-obj-y += board/irq_table_generated.o
app-obj-y += board/irq_hw_init_generated.o
app-obj-y += board/irq_periph_dispatch_generated.o

# ── lwIP apps the OS net/ build does not compile ──────────────────────────
LWIP_APPS := ../FreeRTOS-OS/net/lwip/src/apps
app-obj-y += $(LWIP_APPS)/mqtt/mqtt.o
app-obj-y += $(LWIP_APPS)/altcp_tls/altcp_tls_mbedtls_mem.o
# altcp_tls_mbedtls.c ported to mbedTLS 3.6 (see header of the copy)
app-obj-y += third_party/lwip_altcp_tls/altcp_tls_mbedtls.o

# ── STM32H7 RNG HAL (TRNG entropy; arch/Makefile does not build it) ───────
app-obj-y += ../FreeRTOS-OS/arch/devices/STM/stm32h7xx-hal-driver/Src/stm32h7xx_hal_rng.o

# ── mbedTLS 3.6 (lib/mbedtls submodule) ───────────────────────────────────
# Every library file is compiled; mbedtls_config_nps.h compiles the unused
# ones to empty objects and --gc-sections drops dead code.
MBEDTLS_DIR := $(APP_DIR)/../lib/mbedtls
app-obj-y += $(patsubst $(APP_DIR)/%.c,%.o,$(wildcard $(MBEDTLS_DIR)/library/*.c))

# ── Include paths / defines ───────────────────────────────────────────────
APP_INCLUDES += -I$(APP_DIR)
APP_INCLUDES += -I$(APP_DIR)/inc
APP_INCLUDES += -I$(APP_DIR)/os_conf_include
APP_INCLUDES += -I$(APP_DIR)/../FreeRTOS-OS/net/lwip/src/apps/altcp_tls

# With LWIP_ALTCP_TLS=1, lwIP core files the OS compiles (altcp_alloc.c)
# include mbedtls/ssl.h, so mbedTLS headers + config go on the global flags.
INCLUDES += -I$(APP_DIR)/inc
INCLUDES += -I$(MBEDTLS_DIR)/include
INCLUDES += '-DMBEDTLS_CONFIG_FILE="mbedtls_config_nps.h"'

# ── Linker script: image lives at 0x08020000 behind the Ethernet bootloader.
# Overrides the OS default (included after arch/Makefile, link is deferred).
LINKER_SCRIPT := $(APP_DIR)/board/nps_h723_app.ld
