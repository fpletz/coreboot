TAG-$(CONFIG_GRUB2_MASTER)=
NAME-$(CONFIG_GRUB2_MASTER)=HEAD

unexport KCONFIG_AUTOCONFIG
unexport CFLAGS
unexport CPPFLAGS
unexport CCASFLAGS
unexport CC
unexport BUILD_CC
unexport TARGET_CC
unexport TARGET_CFLAGS
unexport TARGET_CPPFLAGS
unexport TARGET_STRIP
unexport TARGET_OBJCOPY
unexport HOST_CFLAGS
unexport HOST_CPPFLAGS
unexport HOST_CC


all: grub2

checkout:
	echo "    GIT        GRUB2 $(NAME-y)"
	test -d grub2 || \
		git clone git://git.sv.gnu.org/grub.git grub2
	cd grub2 && \
		git checkout master && \
		git pull; \
		test -n "$(TAG-y)" && \
			git branch -f $(NAME-y) $(TAG-y) && \
			git checkout $(NAME-y) || true

config: checkout
	echo "    CONFIG     GRUB2 $(NAME-y)"
	mkdir -p grub2/build
	cd grub2 && ./autogen.sh
	cd grub2/build && ../configure BUILD_CC="$(HOSTCC)" CC="$(HOSTCC)" \
	TARGET_CC="$(CC)" \
	TARGET_OBJCOPY="$(OBJCOPY)" TARGET_STRIP="$(STRIP)" CFLAGS=-O2 TARGET_CFLAGS=-Os --with-platform=coreboot

grub2: config
	echo "    MAKE       GRUB2 $(NAME-y)"
	$(MAKE) -C grub2/build CC="$(HOSTCC)"
	pkgdatadir=. grub2/build/grub-mkstandalone --grub-mkimage=./grub-mkimage -O i386-coreboot -o payload.elf --modules='ata ahci pata ehci uhci ohci usb_keyboard usbms part_msdos xfs ext2 fat at_keyboard part_gpt usbserial_usbdebug cbfs' --install-modules='ls linux search configfile normal cbtime cbls memrw iorw minicmd lsmmap lspci halt reboot hexdump pcidump regexp setpci lsacpi chain test serial multiboot cbmemc linux16 gzio echo help cat cryptodisk luks acpi cmosdump cmp cpio cpuid crypto elf file gcry_rijndael gcry_sha1 gcry_sha256 gcry_sha512 gcry_twofish gcry_whirlpool gcry_blowfish gcry_serpent lzopio parttool password_pbkdf2 pbkdf2 probe progress search_fs_file tar gfxmenu gfxterm_background gfxterm_menu hdparm hashsum iso9660 jpeg png mdraid1x zfs zfsinfo zfscrypt' --fonts= --themes= --locales= -d grub2/build/grub-core/ /boot/grub/grub.cfg=grub2/coreboot.cfg

clean:
	test -d grub2 && $(MAKE) -C grub2 clean || exit 0

distclean:
	rm -rf grub2

.PHONY: checkout config grub2 clean distclean
