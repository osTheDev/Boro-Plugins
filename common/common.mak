# Shared build settings: each plugin's Makefile sets TARGET and its own OBJS,
# then includes this.
OBJS += ../common/ini.o ../common/keys.o ../common/overlay.o

CFLAGS = -Os -G0 -Wall -Wextra -fno-builtin-printf
ASFLAGS = $(CFLAGS)

BUILD_PRX = 1
PSP_FW_VERSION = 660

USE_KERNEL_LIBC = 1
USE_KERNEL_LIBS = 1

LIBS += -lpspsystemctrl_kernel -lpspctrl_driver -lpspdisplay_driver

PSPSDK = $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build_prx.mak
