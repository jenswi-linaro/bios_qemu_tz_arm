include bios/conf.mk

cppflags += -DPLATFORM_FLAVOR=PLATFORM_FLAVOR_ID_$(PLATFORM_FLAVOR)
cppflags += -Iinclude
cppflags += -DCOMMAND_LINE="\"$(BIOS_COMMAND_LINE)\""

#
# Do libraries
#
libname = utils
libdir = libutils
include mk/lib.mk

libname = fdt
libdir = libfdt
include mk/lib.mk

subdirs = bios drivers
include mk/subdir.mk
include mk/compile.mk
include bios/link.mk
