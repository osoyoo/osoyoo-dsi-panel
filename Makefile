# OSOYOO unified MIPI-DSI direct-connect panel driver
#
# Builds two modules:
#   osoyoo-dsi-panel.o        - unified panel driver (3.5" ST7701S + 7"/10.1" ILI9881C)
#   osoyoo-panel-regulator.o  - STM32 companion @ i2c 0x45: reset gpio-controller

TARGET_PANEL = osoyoo-dsi-panel
TARGET_REG   = osoyoo-panel-regulator

KDIR = /lib/modules/$(shell uname -r)/build
PWD  = $(shell pwd)

obj-m := $(TARGET_PANEL).o
obj-m += $(TARGET_REG).o

default:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(RM) *.o *.ko *.mod *.mod.c Module.symvers modules.order
