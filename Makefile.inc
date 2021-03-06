##
## This file is part of the coreboot project.
##
## Copyright (C) 2011 secunet Security Networks AG
##
## This program is free software; you can redistribute it and/or modify
## it under the terms of the GNU General Public License as published by
## the Free Software Foundation; version 2 of the License.
##
## This program is distributed in the hope that it will be useful,
## but WITHOUT ANY WARRANTY; without even the implied warranty of
## MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
## GNU General Public License for more details.
##
## You should have received a copy of the GNU General Public License
## along with this program; if not, write to the Free Software
## Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
##

#######################################################################
# misleadingly named, this is the coreboot version
export KERNELVERSION := $(shell if [ -d "$(top)/.git" -a -f "`which git`" ]; \
       then git describe --dirty --always || git describe; \
       else echo 4.0$(KERNELREVISION); fi)

#######################################################################
# Test for coreboot toolchain (except when explicitely not requested)
ifneq ($(NOCOMPILE),1)
# only run if we're doing a build (not for tests, kconfig, ...)
ifneq ($(CONFIG_ANY_TOOLCHAIN),y)
_toolchain=$(shell $(CC_x86_32) -v 2>&1 |grep -q "gcc version .*coreboot toolchain" && echo coreboot)
ifeq ($(CONFIG_COMPILER_LLVM_CLANG),y)
_toolchain=$(shell $(CC_x86_32) -v 2>&1 |grep -q "clang version" && echo coreboot)
endif
ifneq ($(_toolchain),coreboot)
$(error Please use the coreboot toolchain (or prove that your toolchain works))
endif
endif
endif

#######################################################################
# Basic component discovery
MAINBOARDDIR=$(call strip_quotes,$(CONFIG_MAINBOARD_DIR))
export MAINBOARDDIR

## Final build results, which CBFSTOOL uses to create the final
## rom image file, are placed under $(objcbfs).
## These typically have suffixes .debug .elf .bin and .map
export objcbfs := $(obj)/cbfs/$(call strip_quotes,$(CONFIG_CBFS_PREFIX))

## Based on the active configuration, Makefile conditionally collects
## the required assembly includes and saves them in a file.
## Such files that do not have a clear one-to-one relation to a source
## file under src/ are placed and built under $(objgenerated)
export objgenerated := $(obj)/generated

#######################################################################
# root rule to resolve if in build mode (ie. configuration exists)
real-target: $(obj)/config.h coreboot
coreboot: build-dirs $(obj)/coreboot.rom $(obj)/cbfstool $(obj)/rmodtool

#######################################################################
# our phony targets
PHONY+= clean-abuild coreboot lint lint-stable build-dirs

#######################################################################
# root source directories of coreboot
subdirs-y := src/lib src/console src/device src/ec src/southbridge src/soc
subdirs-y += src/northbridge src/superio src/drivers src/cpu src/vendorcode
subdirs-y += util/cbfstool util/sconfig util/nvramtool
subdirs-y += src/arch/arm src/arch/x86
subdirs-y += src/mainboard/$(MAINBOARDDIR)

subdirs-y += site-local

#######################################################################
# Add source classes and their build options
classes-y := ramstage romstage bootblock smm smmstub cpu_microcode

# Add dynamic classes for rmodules
$(foreach supported_arch,$(ARCH_SUPPORTED), \
	    $(eval $(call define_class,rmodules_$(supported_arch),$(supported_arch))))

#######################################################################
# Helper functions for ramstage postprocess
spc :=
spc +=
$(spc) :=
$(spc) +=

# files-in-dir-recursive,dir,files
files-in-dir-recursive=$(filter $(1)%,$(2))

# parent-dir,dir/
parent-dir=$(dir $(if $(patsubst /%,,$(1)),,/)$(subst $( ),/,$(strip $(subst /, ,$(1)))))

# filters out exactly the directory specified
# filter-out-dir,dir_to_keep,dirs
filter-out-dir=$(filter-out $(1),$(2))

# filters out dir_to_keep and all its parents
# filter-out-dirs,dir_to_keep,dirs
filter-out-dirs=$(if $(filter-out ./ /,$(1)),$(call filter-out-dirs,$(call parent-dir,$(1)),$(call filter-out-dir,$(1),$(2))),$(call filter-out-dir,$(1),$(2)))

# dir-wildcards,dirs
dir-wildcards=$(addsuffix %,$(1))

# files-in-dir,dir,files
files-in-dir=$(filter-out $(call dir-wildcards,$(call filter-out-dirs,$(1),$(sort $(dir $(2))))),$(call files-in-dir-recursive,$(1),$(2)))

#######################################################################
# reduce command line length by linking the objects of each
# directory into an intermediate file
ramstage-postprocess=$(foreach d,$(sort $(dir $(1))), \
	$(eval $(d)ramstage.o: $(call files-in-dir,$(d),$(1)); $$(LD_ramstage) -o $$@ -r $$^ ) \
	$(eval ramstage-objs:=$(d)ramstage.o $(filter-out $(call files-in-dir,$(d),$(1)),$(ramstage-objs))))

CPPFLAGS_romstage += -D__PRE_RAM__
ifeq ($(CONFIG_TRACE),y)
CFLAGS_ramstage += -finstrument-functions
endif
ifeq ($(CONFIG_COVERAGE),y)
CFLAGS_ramstage += -fprofile-arcs -ftest-coverage
endif

ifeq ($(CONFIG_USE_BLOBS),y)
forgetthis:=$(shell git submodule update --init --checkout 3rdparty)
endif

CPPFLAGS_bootblock += -D__BOOT_BLOCK__ -D__PRE_RAM__

CPPFLAGS_smmstub += -D__SMM__
CPPFLAGS_smm += -D__SMM__

# SMM TSEG base is dynamic
ifneq ($(CONFIG_SMM_MODULES),y)
ifeq ($(CONFIG_SMM_TSEG),y)
CFLAGS_smm += -fpic
endif
endif

ramstage-c-deps:=$$(OPTION_TABLE_H)
romstage-c-deps:=$$(OPTION_TABLE_H)
bootblock-c-deps:=$$(OPTION_TABLE_H)
smm-c-deps:=$$(OPTION_TABLE_H)

#######################################################################
# Add handler to compile ACPI's ASL
define ramstage-objs_asl_template
$(obj)/$(1).ramstage.o: src/$(1).asl $(obj)/config.h
	@printf "    IASL       $$(subst $(top)/,,$$(@))\n"
	$(CC_ramstage) -x assembler-with-cpp -E -MMD -MT $$(@) -D__ACPI__ -P -include $(src)/include/kconfig.h -I$(obj) -I$(src) -I$(src)/include -I$(src)/arch/$(ARCHDIR-$(ARCH-ramstage-y))/include -I$(src)/mainboard/$(MAINBOARDDIR) $$< -o $$(basename $$@).asl
	cd $$(dir $$@); $(IASL) -p $$(notdir $$@) -tc $$(notdir $$(basename $$@)).asl
	mv $$(basename $$@).hex $$(basename $$@).c
	$(CC_ramstage) $$(CFLAGS_ramstage) $$(CPPFLAGS_ramstage) $$(if $$(subst dsdt,,$$(basename $$(notdir $(1)))), -DAmlCode=AmlCode_$$(basename $$(notdir $(1)))) -c -o $$@ $$(basename $$@).c
	# keep %.o: %.c rule from catching the temporary .c file after a make clean
	mv $$(basename $$@).c $$(basename $$@).hex
endef

#######################################################################
# Parse plaintext cmos defaults into binary format
# arg1: source file
# arg2: binary file name
cbfs-files-processor-nvramtool= \
	$(eval $(2): $(1) $(src)/mainboard/$(MAINBOARDDIR)/cmos.layout | $(objutil)/nvramtool/nvramtool ; \
		printf "    CREATE     $(2) (from $(1))\n"; $(objutil)/nvramtool/nvramtool -y $(src)/mainboard/$(MAINBOARDDIR)/cmos.layout -D $(2).tmp -p $(1) && mv $(2).tmp $(2))

#######################################################################
# Link VSA binary to ELF-ish stage
# arg1: source file
# arg2: binary file name
cbfs-files-processor-vsa= \
	$(eval $(2): $(1) ; \
		printf "    CREATE     $(2) (from $(1))\n";  $(OBJCOPY_ramstage) --set-start 0x20 --adjust-vma 0x60000 -I binary -O elf32-i386 -B i386 $(1) $(2).tmp && $(LD_ramstage) -m elf_i386 -e 0x60020 --section-start .data=0x60000 $(2).tmp -o $(2))

#######################################################################
# Add handler for arbitrary files in CBFS
$(call add-special-class,cbfs-files)
cbfs-files-handler= \
		$(eval tmp-cbfs-method:=$(word 2, $(subst :, ,$($(2)-file)))) \
		$(eval $(2)-file:=$(call strip_quotes,$(word 1, $(subst :, ,$($(2)-file))))) \
		$(if $(wildcard $(1)$($(2)-file)), \
			$(eval tmp-cbfs-file:= $(wildcard $(1)$($(2)-file))), \
			$(eval tmp-cbfs-file:= $($(2)-file))) \
		$(if $(strip $($(2)-required)), \
			$(if $(wildcard $(tmp-cbfs-file)),, \
				$(info This build configuration requires $($(2)-required)) \
				$(eval FAILBUILD:=1) \
			)) \
		$(if $(tmp-cbfs-method), \
			$(eval tmp-old-cbfs-file:=$(tmp-cbfs-file)) \
			$(eval tmp-cbfs-file:=$(shell mkdir -p $(obj)/mainboard/$(MAINBOARDDIR); mktemp $(obj)/mainboard/$(MAINBOARDDIR)/cbfs-file.XXXXXX).out) \
			$(call cbfs-files-processor-$(tmp-cbfs-method),$(tmp-old-cbfs-file),$(tmp-cbfs-file))) \
		$(eval cbfs-files += $(tmp-cbfs-file)|$(2)|$($(2)-type)|$($(2)-compression)|$($(2)-position)) \
		$(eval $(2)-name:=) \
		$(eval $(2)-type:=) \
		$(eval $(2)-compression:=) \
		$(eval $(2)-position:=) \
		$(eval $(2)-required:=)

#######################################################################
# a variety of flags for our build
CBFS_COMPRESS_FLAG:=none
ifeq ($(CONFIG_COMPRESS_RAMSTAGE),y)
CBFS_COMPRESS_FLAG:=LZMA
endif

CBFS_PAYLOAD_COMPRESS_FLAG:=none
ifeq ($(CONFIG_COMPRESSED_PAYLOAD_LZMA),y)
CBFS_PAYLOAD_COMPRESS_FLAG:=LZMA
endif

ifneq ($(CONFIG_LOCALVERSION),"")
COREBOOT_EXTRA_VERSION := -$(call strip_quotes,$(CONFIG_LOCALVERSION))
endif

CPPFLAGS_common := -Isrc -Isrc/include -I$(obj)
CPPFLAGS_common += -Isrc/device/oprom/include
CPPFLAGS_common += -include $(src)/include/kconfig.h

CFLAGS_common += -Os -pipe -g -nostdinc
CFLAGS_common += -nostdlib -Wall -Wundef -Wstrict-prototypes -Wmissing-prototypes
CFLAGS_common += -Wwrite-strings -Wredundant-decls -Wno-trigraphs
CFLAGS_common += -Wstrict-aliasing -Wshadow
ifeq ($(CONFIG_WARNINGS_ARE_ERRORS),y)
CFLAGS_common += -Werror
endif
CFLAGS_common += -fno-common -ffreestanding -fno-builtin -fomit-frame-pointer

additional-dirs := $(objutil)/cbfstool $(objutil)/romcc $(objutil)/ifdtool \
		   $(objutil)/ifdfake $(objutil)/options $(objutil)/fletcher \
		   $(objutil)/cbootimage

#######################################################################
# generate build support files
$(obj)/build.h: .xcompile
	@printf "    GEN        build.h\n"
	rm -f $(obj)/build.h
	printf "/* build system definitions (autogenerated) */\n" > $(obj)/build.ht
	printf "#ifndef __BUILD_H\n" >> $(obj)/build.ht
	printf "#define __BUILD_H\n\n" >> $(obj)/build.ht
	printf "#define COREBOOT_VERSION \"$(KERNELVERSION)\"\n" >> $(obj)/build.ht
	printf "#define COREBOOT_EXTRA_VERSION \"$(COREBOOT_EXTRA_VERSION)\"\n" >> $(obj)/build.ht
	printf "#define COREBOOT_BUILD \"`LANG= date`\"\n" >> $(obj)/build.ht
	printf "#define COREBOOT_BUILD_YEAR_BCD 0x`LANG= date +"%y"`\n" >> $(obj)/build.ht
	printf "#define COREBOOT_BUILD_MONTH_BCD 0x`LANG= date +"%m"`\n" >> $(obj)/build.ht
	printf "#define COREBOOT_BUILD_DAY_BCD 0x`LANG= date +"%d"`\n" >> $(obj)/build.ht
	printf "#define COREBOOT_BUILD_WEEKDAY_BCD 0x`LANG= date +"%w"`\n" >> $(obj)/build.ht
	printf "#define COREBOOT_DMI_DATE \"`LANG= date +"%m/%d/%Y"`\"\n" >> $(obj)/build.ht
	printf "\n" >> $(obj)/build.ht
	printf "#define COREBOOT_COMPILE_TIME \"`LANG= date +%T`\"\n" >> $(obj)/build.ht
	printf "#define COREBOOT_COMPILE_BY \"$(subst \,@,$(shell PATH=$$PATH:/usr/ucb whoami))\"\n" >> $(obj)/build.ht
	printf "#define COREBOOT_COMPILE_HOST \"$(shell hostname -s 2>/dev/null || hostname 2>/dev/null)\"\n" >> $(obj)/build.ht
	printf "#define COREBOOT_COMPILE_DOMAIN \"$(shell test `uname -s` = "Linux" && dnsdomainname || domainname 2>/dev/null)\"\n" >> $(obj)/build.ht
	printf "#endif\n" >> $(obj)/build.ht
	mv $(obj)/build.ht $(obj)/build.h

$(obj)/ldoptions: $(obj)/config.h
	awk '/^#define ([^"])* ([^"])*$$/ {gsub("\\r","",$$3); print $$2 " = " $$3 ";";}' $< > $@

build-dirs:
	mkdir -p $(objcbfs) $(objgenerated)

#######################################################################
# Build the tools
CBFSTOOL:=$(objutil)/cbfstool/cbfstool
RMODTOOL:=$(objutil)/cbfstool/rmodtool

$(obj)/cbfstool: $(CBFSTOOL)
	cp $< $@

$(obj)/rmodtool: $(RMODTOOL)
	cp $< $@

_WINCHECK=$(shell uname -o 2> /dev/null)
STACK=
ifeq ($(_WINCHECK),Msys)
	STACK=-Wl,--stack,16384000
endif
ifeq ($(_WINCHECK),Cygwin)
	STACK=-Wl,--stack,16384000
endif

# this allows ccache to prepend itself
# (ccache handling happens first)
ROMCC_BIN= $(objutil)/romcc/romcc
ROMCC?=$(ROMCC_BIN)
$(ROMCC_BIN): $(top)/util/romcc/romcc.c
	@printf "    HOSTCC     $(subst $(obj)/,,$(@)) (this may take a while)\n"
	@# Note: Adding -O2 here might cause problems. For details see:
	@# http://www.coreboot.org/pipermail/coreboot/2010-February/055825.html
	$(HOSTCC) -g $(STACK) -Wall -o $@ $<

IFDTOOL:=$(objutil)/ifdtool/ifdtool
$(IFDTOOL): $(top)/util/ifdtool/ifdtool.c
	@printf "    HOSTCC     $(subst $(obj)/,,$(@))\n"
	$(HOSTCC) $(HOSTCFLAGS) -o $@ $<

IFDFAKE:=$(objutil)/ifdfake/ifdfake
$(IFDFAKE): $(top)/util/ifdfake/ifdfake.c
	@printf "    HOSTCC     $(subst $(obj)/,,$(@))\n"
	$(HOSTCC) $(HOSTCFLAGS) -o $@ $<

FLETCHER:=$(objutil)/fletcher/fletcher
$(FLETCHER): $(top)/util/fletcher/fletcher.c
	@printf "    HOSTCC     $(subst $(obj)/,,$(@))\n"
	$(HOSTCC) $(HOSTCFLAGS) -o $@ $<

CBOOTIMAGE:=$(objutil)/cbootimage/cbootimage

$(CBOOTIMAGE): $(top)/util/nvidia-cbootimage/Makefile
	@printf "    MAKE     $(subst $(obj)/,,$(@))\n"
	$(MAKE) -C $(top)/util/nvidia-cbootimage
	cp $(top)/util/nvidia-cbootimage/src/cbootimage $@

$(top)/util/nvidia-cbootimage/Makefile:
	cd $(top)/util/nvidia-cbootimage/; \
	./autogen.sh >/dev/null

#######################################################################
# needed objects that every mainboard uses
# Creation of these is architecture and mainboard independent
$(obj)/mainboard/$(MAINBOARDDIR)/static.c: $(src)/mainboard/$(MAINBOARDDIR)/devicetree.cb  $(objutil)/sconfig/sconfig
	@printf "    SCONFIG    $(subst $(src)/,,$(<))\n"
	mkdir -p $(obj)/mainboard/$(MAINBOARDDIR)
	$(objutil)/sconfig/sconfig $(MAINBOARDDIR) $(obj)/mainboard/$(MAINBOARDDIR)

ramstage-y+=$(obj)/mainboard/$(MAINBOARDDIR)/static.c
romstage-y+=$(obj)/mainboard/$(MAINBOARDDIR)/static.c

$(objutil)/%.o: $(objutil)/%.c
	@printf "    HOSTCC     $(subst $(objutil)/,,$(@))\n"
	$(HOSTCC) -MMD -I$(subst $(objutil)/,util/,$(dir $<)) -I$(dir $<) $(HOSTCFLAGS) -c -o $@ $<

$(obj)/%.ramstage.o $(abspath $(obj))/%.ramstage.o: $(obj)/%.c $(obj)/config.h $(OPTION_TABLE_H)
	@printf "    CC         $(subst $(obj)/,,$(@))\n"
	$(CC_ramstage) -MMD $(CFLAGS_ramstage) $(CPPFLAGS_ramstage) -c -o $@ $<

$(obj)/%.romstage.o $(abspath $(obj))/%.romstage.o: $(obj)/%.c $(obj)/config.h $(OPTION_TABLE_H)
	@printf "    CC         $(subst $(obj)/,,$(@))\n"
	$(CC_romstage) -MMD -D__PRE_RAM__ $(CFLAGS_romstage) $(CPPFLAGS_romstage) -c -o $@ $<

$(obj)/%.bootblock.o $(abspath $(obj))/%.bootblock.o: $(obj)/%.c $(obj)/config.h $(OPTION_TABLE_H)
	@printf "    CC         $(subst $(obj)/,,$(@))\n"
	$(CC_bootblock) -MMD $(CFLAGS_bootblock) $(CPPFLAGS_bootblock) -c -o $@ $<

#######################################################################
# Clean up rules
clean-abuild:
	rm -rf coreboot-builds

clean-for-update-target:
	rm -f $(obj)/ramstage* $(obj)/coreboot.romstage $(obj)/coreboot.pre* $(obj)/coreboot.bootblock $(obj)/coreboot.a
	rm -rf $(obj)/bootblock* $(obj)/romstage* $(obj)/location.*
	rm -f $(obj)/option_table.* $(obj)/crt0.S $(obj)/ldscript
	rm -f $(obj)/mainboard/$(MAINBOARDDIR)/static.c $(obj)/mainboard/$(MAINBOARDDIR)/config.py $(obj)/mainboard/$(MAINBOARDDIR)/static.dot
	rm -f $(obj)/mainboard/$(MAINBOARDDIR)/crt0.s $(obj)/mainboard/$(MAINBOARDDIR)/crt0.disasm
	rm -f $(obj)/mainboard/$(MAINBOARDDIR)/romstage.inc
	rm -f $(obj)/mainboard/$(MAINBOARDDIR)/bootblock.* $(obj)/mainboard/$(MAINBOARDDIR)/dsdt.*
	rm -f $(obj)/cpu/x86/smm/smm_bin.c $(obj)/cpu/x86/smm/smm.* $(obj)/cpu/x86/smm/smm
	$(MAKE) -C payloads/external/SeaBIOS -f Makefile.inc clean OUT=$(abspath $(obj)) HOSTCC="$(HOSTCC)" CC="$(CC_x86_32)" LD="$(LD_x86_32)"

clean-target:
	rm -f $(obj)/coreboot*

#######################################################################
# Development utilities
printcrt0s:
	@echo crt0s=$(crt0s)
	@echo ldscripts=$(ldscripts)

update:
	dongle.py -c /dev/term/1 $(obj)/coreboot.rom EOF

lint lint-stable:
	FAILED=0; LINTLOG=`mktemp .tmpconfig.lintXXXXX`; \
	for script in util/lint/$@-*; do \
		echo; echo `basename $$script`; \
		grep "^# DESCR:" $$script | sed "s,.*DESCR: *,," ; \
		echo ========; \
		$$script > $$LINTLOG; \
		if [ `cat $$LINTLOG | wc -l` -eq 0 ]; then \
			printf "success\n\n"; \
		else \
			echo test failed: ; \
			cat $$LINTLOG; \
			rm -f $$LINTLOG; \
			FAILED=$$(( $$FAILED + 1 )); \
		fi; \
		echo ========; \
	done; \
	test $$FAILED -eq 0 || { echo "ERROR: $$FAILED test(s) failed."; rm -f $$LINTLOG && exit 1; }; \
	rm -f $$LINTLOG

gitconfig:
	mkdir -p .git/hooks
	for hook in commit-msg pre-commit ; do                       \
		if [ util/gitconfig/$$hook -nt .git/hooks/$$hook -o  \
		! -x .git/hooks/$$hook ]; then			     \
			cp util/gitconfig/$$hook .git/hooks/$$hook;  \
			chmod +x .git/hooks/$$hook;		     \
		fi;						     \
	done
	git config remote.origin.push HEAD:refs/for/master
	(git config --global user.name >/dev/null && git config --global user.email >/dev/null) || (printf 'Please configure your name and email in git:\n\n git config --global user.name "Your Name Comes Here"\n git config --global user.email your.email@example.com\n'; exit 1)

crossgcc: crossgcc-i386 crossgcc-arm

.PHONY: crossgcc-i386 crossgcc-arm
crossgcc-i386: clean-for-update
	$(MAKE) -C util/crossgcc build-i386-without-gdb

crossgcc-arm: clean-for-update
	$(MAKE) -C util/crossgcc build-armv7a-without-gdb

crosstools: crosstools-i386

.PHONY: crosstools-i386 crosstools-arm
crosstools-i386: clean-for-update
	$(MAKE) -C util/crossgcc build-i386

crosstools-arm: clean-for-update
	$(MAKE) -C util/crossgcc build-armv7a

crossgcc-clean: clean-for-update
	$(MAKE) -C util/crossgcc clean

tools: $(objutil)/kconfig/conf $(objutil)/cbfstool/cbfstool $(objutil)/cbfstool/rmodtool $(objutil)/nvramtool/nvramtool $(ROMCC_BIN) $(objutil)/sconfig/sconfig $(IFDTOOL) $(IFDFAKE) $(CBOOTIMAGE)

###########################################################################
# Common recipes for all stages
###########################################################################

# find-substr is required for stages like romstage_null and romstage_xip to
# eliminate the _* part of the string
find-substr = $(word 1,$(subst _, ,$(1)))

# find-class is used to identify the class from the name of the stage
# The input to this macro can be something like romstage.x or romstage.x.y
# find-class recursively strips off the suffixes to extract the exact class name
# e.g.: if romstage.x is provided to find-class, it will remove .x and return romstage
# if romstage.x.y is provided, it will first remove .y, call find-class with romstage.x
# and remove .x the next time and finally return romstage
find-class = $(if $(filter $(1),$(basename $(1))),$(if $(CC_$(1)), $(1), $(call find-substr,$(1))),$(call find-class,$(basename $(1))))

$(objcbfs)/%.bin: $(objcbfs)/%.elf
	$(eval class := $(call find-class,$(@F)))
	@printf "    OBJCOPY    $(subst $(obj)/,,$(@))\n"
	$(OBJCOPY_$(class)) -O binary $< $@

$(objcbfs)/%.elf: $(objcbfs)/%.debug
	$(eval class := $(call find-class,$(@F)))
	@printf "    OBJCOPY    $(subst $(obj)/,,$(@))\n"
	cp $< $@.tmp
	$(NM_$(class)) -n $@.tmp | sort > $(basename $@).map
	$(OBJCOPY_$(class)) --strip-debug $@.tmp
	$(OBJCOPY_$(class)) --add-gnu-debuglink=$< $@.tmp
	mv $@.tmp $@

###########################################################################
# Build the final rom image
###########################################################################

COREBOOT_ROM_DEPENDENCIES:=
ifeq ($(CONFIG_PAYLOAD_ELF),y)
COREBOOT_ROM_DEPENDENCIES+=$(CONFIG_PAYLOAD_FILE)
endif
ifeq ($(CONFIG_PAYLOAD_SEABIOS),y)
COREBOOT_ROM_DEPENDENCIES+=seabios
endif
ifeq ($(CONFIG_PAYLOAD_FILO),y)
COREBOOT_ROM_DEPENDENCIES+=filo
endif
ifeq ($(CONFIG_PAYLOAD_GRUB2),y)
COREBOOT_ROM_DEPENDENCIES+=grub2
endif

extract_nth=$(word $(1), $(subst |, ,$(2)))

ifneq ($(CONFIG_UPDATE_IMAGE),y)
prebuild-files = \
	       $(foreach file,$(cbfs-files), \
	       $(CBFSTOOL) $@.tmp \
	       add$(if $(filter stage,$(call extract_nth,3,$(file))),-stage)$(if $(filter payload,$(call extract_nth,3,$(file))),-payload) \
	       -f $(call extract_nth,1,$(file)) \
	       -n $(call extract_nth,2,$(file)) $(if $(filter-out stage,$(call extract_nth,3,$(file))),-t $(call extract_nth,3,$(file))) \
	       $(if $(call extract_nth,4,$(file)),-b $(call extract_nth,4,$(file))) &&)
prebuilt-files = $(foreach file,$(cbfs-files), $(call extract_nth,1,$(file)))

$(obj)/coreboot.pre1: $(objcbfs)/bootblock.bin $$(prebuilt-files) $(CBFSTOOL) $$(cpu_ucode_cbfs_file)
	$(CBFSTOOL) $@.tmp create -s $(CONFIG_COREBOOT_ROMSIZE_KB)K \
	-B $(objcbfs)/bootblock.bin -a 64 \
	$(CBFSTOOL_PRE1_OPTS)
	$(prebuild-files) true
	$(call add-cpu-microcode-to-cbfs,$@.tmp)
	mv $@.tmp $@
else
.PHONY: $(obj)/coreboot.pre1
$(obj)/coreboot.pre1: $(CBFSTOOL)
	mv $(obj)/coreboot.rom $@
endif

ifeq ($(CONFIG_PAYLOAD_LINUX),y)
ifneq ($(strip $(call strip_quotes,$(CONFIG_LINUX_COMMAND_LINE))),)
      ADDITIONAL_PAYLOAD_CONFIG+=-C $(CONFIG_LINUX_COMMAND_LINE)
endif
ifneq ($(strip $(call strip_quotes,$(CONFIG_LINUX_INITRD))),)
      ADDITIONAL_PAYLOAD_CONFIG+=-I $(CONFIG_LINUX_INITRD)
endif
endif

ifeq ($(CONFIG_HAVE_REFCODE_BLOB),y)
REFCODE_BLOB=$(obj)/refcode.rmod
$(REFCODE_BLOB): $(RMODTOOL)
	$(RMODTOOL) -i $(CONFIG_REFCODE_BLOB_FILE) -o $@
endif

$(obj)/coreboot.rom: $(obj)/coreboot.pre $(objcbfs)/ramstage.elf $(CBFSTOOL) $(call strip_quotes,$(COREBOOT_ROM_DEPENDENCIES)) $$(INTERMEDIATE) $$(VBOOT_STUB) $(REFCODE_BLOB)
	@printf "    CBFS       $(subst $(obj)/,,$(@))\n"
	cp $(obj)/coreboot.pre $@.tmp
	$(CBFSTOOL) $@.tmp add-stage -f $(objcbfs)/ramstage.elf -n $(CONFIG_CBFS_PREFIX)/ramstage -c $(CBFS_COMPRESS_FLAG)
ifeq ($(CONFIG_PAYLOAD_NONE),y)
	@printf "    PAYLOAD    none (as specified by user)\n"
endif
ifneq ($(CONFIG_PAYLOAD_FILE),)
	@printf "    PAYLOAD    $(CONFIG_PAYLOAD_FILE) (compression: $(CBFS_PAYLOAD_COMPRESS_FLAG))\n"
	$(CBFSTOOL) $@.tmp add-payload -f $(CONFIG_PAYLOAD_FILE) -n $(CONFIG_CBFS_PREFIX)/payload -c $(CBFS_PAYLOAD_COMPRESS_FLAG) $(ADDITIONAL_PAYLOAD_CONFIG)
endif
ifneq ($(CONFIG_SEABIOS_PS2_TIMEOUT),)
ifneq ($(CONFIG_SEABIOS_PS2_TIMEOUT),0)
	@printf "    SeaBIOS    Wait up to $(CONFIG_SEABIOS_PS2_TIMEOUT) ms for PS/2 keyboard controller initialization\n"
	$(CBFSTOOL) $@.tmp add-int -i $(CONFIG_SEABIOS_PS2_TIMEOUT) -n etc/ps2-keyboard-spinup
endif
endif
ifeq ($(CONFIG_INCLUDE_CONFIG_FILE),y)
	@printf "    CONFIG     $(DOTCONFIG)\n"
	if [ -f $(DOTCONFIG) ]; then \
	echo "# This image was built using git revision" `git rev-parse HEAD` > $(obj)/config.tmp ; \
	sed -e '/^#/d' -e '/^ *$$/d' $(DOTCONFIG) >> $(obj)/config.tmp ; \
	$(CBFSTOOL) $@.tmp add -f $(obj)/config.tmp -n config -t raw; rm -f $(obj)/config.tmp ; fi
endif
ifeq ($(CONFIG_VBOOT_VERIFY_FIRMWARE),y)
	$(CBFSTOOL) $@.tmp add-stage -f $(VBOOT_STUB) -n $(CONFIG_CBFS_PREFIX)/vboot -c $(CBFS_COMPRESS_FLAG)
endif
ifeq ($(CONFIG_HAVE_REFCODE_BLOB),y)
	$(CBFSTOOL) $@.tmp add-stage -f $(REFCODE_BLOB) -n $(CONFIG_CBFS_PREFIX)/refcode -c $(CBFS_COMPRESS_FLAG)
endif
ifeq ($(CONFIG_PXE_ROM),y)
	$(CBFSTOOL) $@.tmp add -f $(CONFIG_PXE_ROM_FILE) -n pci$(CONFIG_PXE_ROM_ID).rom -t raw
endif
ifeq ($(CONFIG_CPU_INTEL_FIRMWARE_INTERFACE_TABLE),y)
ifeq ($(CONFIG_CPU_MICROCODE_IN_CBFS),y)
	@printf "    UPDATE-FIT \n"
	$(CBFSTOOL) $@.tmp update-fit -n cpu_microcode_blob.bin -x $(CONFIG_CPU_INTEL_NUM_FIT_ENTRIES)
endif
endif
	mv $@.tmp $@
	@printf "    CBFSPRINT  $(subst $(obj)/,,$(@))\n\n"
	$(CBFSTOOL) $@ print

cbfs-files-$(CONFIG_BOOTSPLASH) += bootsplash.jpg
bootsplash.jpg-file := $(call strip_quotes,$(CONFIG_BOOTSPLASH_FILE))
bootsplash.jpg-type := bootsplash

ifeq ($(CONFIG_ARCH_ROMSTAGE_ARM),y)
ROMSTAGE_ELF := romstage.elf
endif
ifeq ($(CONFIG_ARCH_ROMSTAGE_X86_32),y)
ROMSTAGE_ELF := romstage_xip.elf
endif

$(obj)/coreboot.pre: $(objcbfs)/$(ROMSTAGE_ELF) $(obj)/coreboot.pre1 $(CBFSTOOL)
	@printf "    CBFS       $(subst $(obj)/,,$(@))\n"
	cp $(obj)/coreboot.pre1 $@.tmp
	$(CBFSTOOL) $@.tmp add-stage \
		-f $(objcbfs)/$(ROMSTAGE_ELF) \
		-n $(CONFIG_CBFS_PREFIX)/romstage -c none \
		$(CBFSTOOL_PRE_OPTS)
	mv $@.tmp $@

JENKINS_PAYLOAD=none
what-jenkins-does:
	util/abuild/abuild -B -J -y -c 4 -z -p $(JENKINS_PAYLOAD)
	(cd payloads/libpayload; $(MAKE) CONFIG_CCACHE=y V=$(V) Q=$(Q) junit.xml)
	$(MAKE) V=$(V) Q=$(Q) -C util/cbmem junit.xml
