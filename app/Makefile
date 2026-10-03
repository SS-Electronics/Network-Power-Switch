# File:   app/Makefile
# Module: Network-Power-Switch application build fragment (STM32F401CDU6)
# Info:   Included by FreeRTOS-OS/Makefile when APP_DIR is set.
#         Declares app object files (app-obj-y), extra include paths
#         (APP_INCLUDES) and the application linker script.
#         Object paths are relative to APP_DIR; anything outside app/ is
#         reached with ../ (built under FreeRTOS-OS/build/).

# ── Application sources ───────────────────────────────────────────────────
app-obj-y += app_main.o
app-obj-y += src/relay.o
app-obj-y += src/button.o
app-obj-y += src/cmd_codec.o
app-obj-y += src/at_parse.o
app-obj-y += src/esp_at.o
app-obj-y += src/mqtt_link.o

# ── Generated board files (top-level `make gen`) ──────────────────────────
app-obj-y += board/board_config.o
app-obj-y += board/irq_table_generated.o
app-obj-y += board/irq_hw_init_generated.o
app-obj-y += board/irq_periph_dispatch_generated.o

# ── STM32F4 IWDG HAL: safety/wdog.c needs it, but the OS F411 build path
# does not compile it (CONFIG_HAL_IWDG_MODULE_ENABLED only adds the header).
app-obj-y += ../FreeRTOS-OS/arch/devices/STM/stm32f4xx-hal-driver/Src/stm32f4xx_hal_iwdg.o

# ── Include paths ─────────────────────────────────────────────────────────
APP_INCLUDES += -I$(APP_DIR)
APP_INCLUDES += -I$(APP_DIR)/inc
APP_INCLUDES += -I$(APP_DIR)/os_conf_include

# ── Linker script: STM32F401CD memory map (384 KB flash / 96 KB SRAM).
# Overrides the OS F411 script (included after arch/Makefile, link is deferred).
LINKER_SCRIPT := $(APP_DIR)/board/nps_f401cd.ld
