include bios/conf.mk

#
# Do libraries
#
libname = utils
libdir = libutils
include mk/lib.mk

libname = fdt
libdir = libfdt
include mk/lib.mk

subdirs = bios
include mk/subdir.mk
include mk/compile.mk
include bios/link.mk
