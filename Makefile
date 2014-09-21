SHELL = /bin/bash

.PHONY: all
all:

# Make these default for now
ARCH            ?= arm32
arch_$(ARCH)	:= y

O	?= out
ifneq ($O,)
out-dir := $O/
endif

ifneq ($V,1)
q := @
cmd-echo := true
else
q :=
cmd-echo := echo
endif

include bios/bios.mk

.PHONY: clean
clean:
	@echo '  CLEAN   .'
	${q}rm -f $(cleanfiles)

.PHONY: cscope
cscope:
	@echo '  CSCOPE  .'
	${q}rm -f cscope.*
	${q}find $(PWD) -name "*.[chSs]" > cscope.files
	${q}cscope -b -q -k
