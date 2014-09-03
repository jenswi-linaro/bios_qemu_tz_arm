CROSS_PREFIX	?= arm-linux-gnueabihf
CROSS_COMPILE	?= $(CROSS_PREFIX)-
include mk/gcc.mk

cpuarch = cortex-a15
cflags	 = -mcpu=$(cpuarch) -mthumb
cflags	+= -mthumb-interwork -mlong-calls
cflags += -fno-short-enums -mno-apcs-float -fno-common
cflags += -mno-unaligned-access
aflags	 = -mcpu=$(cpuarch)

libutil_with_isoc := y

DEBUG		?= 1
ifeq ($(DEBUG),1)
cflags += -O0
else
cflags += -Os
endif

cflags += -g
aflags += -g

cflags += -g3
aflags += -g3

WARNS ?= 3
