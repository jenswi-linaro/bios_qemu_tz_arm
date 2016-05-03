link-script = bios/kern.ld.S
link-script-pp = $(out-dir)bios/kern.ld
link-script-dep = $(out-dir).kern.ld.d

all: $(out-dir)bios.elf $(out-dir)bios.dmp $(out-dir)bios.bin
all: $(out-dir)bios.symb_sizes
cleanfiles += $(out-dir)bios.elf $(out-dir)bios.dmp $(out-dir)bios.bin
cleanfiles += $(out-dir)bios.map $(out-dir)bios.symb_sizes
cleanfiles += $(out-dir)bios.bin
cleanfiles += $(link-script-pp) $(link-script-dep)

link-ldflags  = $(LDFLAGS)
link-ldflags += -T $(link-script-pp) -Map=$(out-dir)bios.map

link-ldadd  = $(LDADD)
link-ldadd += $(addprefix -L,$(libdirs))
link-ldadd += $(addprefix -l,$(libnames))


blob-objs += $(out-dir)secure_blob.o $(out-dir)nsec_blob.o
blob-objs += $(out-dir)nsec_rootfs.o
cleanfiles += $(out-dir)secure_blob.bin $(out-dir)nsec_blob.bin
cleanfiles += $(out-dir)nsec_rootfs.bin

ifdef BIOS_NSEC_DTB
blob-objs += $(out-dir)nsec_dtb.o
cleanfiles += $(out-dir)nsec_dtb.bin
endif

objs += $(blob-objs)
cleanfiles += $(blob-objs)

ldargs-bios.elf := $(link-ldflags) $(objs) $(link-ldadd) $(libgcc)

link-script-cppflags :=  \
	$(filter-out $(CPPFLAGS_REMOVE) $(cppflags-remove), \
		$(nostdinc) $(CPPFLAGS) \
		$(addprefix -I,$(incdirs$(sm))) $(cppflags$(sm)))


-include $(link-script-dep)

ifndef BIOS_SECURE_BLOB
$(error BIOS_SECURE_BLOB not defined!)
endif
$(out-dir)secure_blob.bin: $(BIOS_SECURE_BLOB) FORCE
	@echo '  LN      $@'
	@mkdir -p $(dir $@)
	@rm -f $@
	$(q)ln -s $(abspath $<) $@


$(out-dir)secure_blob.o: $(out-dir)secure_blob.bin FORCE
	@echo '  OBJCOPY $@'
	$(q)$(OBJCOPY) -I binary -O elf32-littlearm -B arm \
		--rename-section .data=secure_blob $< $@

ifndef BIOS_NSEC_BLOB
$(error BIOS_NSEC_BLOB not defined!)
endif
$(out-dir)nsec_blob.bin: $(BIOS_NSEC_BLOB) FORCE
	@echo '  LN      $@'
	@mkdir -p $(dir $@)
	@rm -f $@
	$(q)ln -s $(abspath $<) $@

$(out-dir)nsec_blob.o: $(out-dir)nsec_blob.bin FORCE
	@echo '  OBJCOPY $@'
	$(q)$(OBJCOPY) -I binary -O elf32-littlearm -B arm \
		--rename-section .data=nsec_blob $< $@

ifdef BIOS_NSEC_DTB
$(out-dir)nsec_dtb.bin: $(BIOS_NSEC_DTB) FORCE
	@echo '  LN      $@'
	@mkdir -p $(dir $@)
	@rm -f $@
	$(q)ln -s $(abspath $<) $@

$(out-dir)nsec_dtb.o: $(out-dir)nsec_dtb.bin FORCE
	@echo '  OBJCOPY $@'
	$(q)$(OBJCOPY) -I binary -O elf32-littlearm -B arm \
		--rename-section .data=nsec_dtb $< $@
endif

ifndef BIOS_NSEC_ROOTFS
$(error BIOS_NSEC_ROOTFS not defined!)
else ifeq ($(BIOS_NSEC_ROOTFS),/dev/null)
$(out-dir)nsec_rootfs.bin: FORCE
	@echo '  MAKE    $@'
	@mkdir -p $(dir $@)
	@rm -f $@
	$(q) echo 'Empty' > $@
else
$(out-dir)nsec_rootfs.bin: $(BIOS_NSEC_ROOTFS) FORCE
	@echo '  LN      $@'
	@mkdir -p $(dir $@)
	@rm -f $@
	$(q)ln -s $(abspath $<) $@
endif

$(out-dir)nsec_rootfs.o: $(out-dir)nsec_rootfs.bin FORCE
	@echo '  OBJCOPY $@'
	$(q)$(OBJCOPY) -I binary -O elf32-littlearm -B arm \
		--rename-section .data=nsec_rootfs $< $@

$(link-script-pp): $(link-script)
	@echo '  CPP     $@'
	@mkdir -p $(dir $@)
	$(q)$(CPP) -Wp,-P,-MT,$@,-MD,$(link-script-dep) \
		$(link-script-cppflags) $< > $@


$(out-dir)bios.elf: $(objs) $(libdeps) $(link-script-pp)
	@echo '  LD      $@'
	$(q)$(LD) $(ldargs-bios.elf) -o $@

$(out-dir)bios.dmp: $(out-dir)bios.elf
	@echo '  OBJDUMP $@'
	$(q)$(OBJDUMP) -l -x -d $< > $@

$(out-dir)bios.bin: $(out-dir)bios.elf
	@echo '  OBJCOPY $@'
	$(q)$(OBJCOPY) -O binary $< $@

$(out-dir)bios.symb_sizes: $(out-dir)bios.elf
	@echo '  GEN     $@'
	$(q)$(NM) --print-size --reverse-sort --size-sort $< > $@
