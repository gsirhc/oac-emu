# BIOS-bochs-legacy (LGPL) built from the pinned rombios.c with patches/ applied, using dev86.

BUILD_DATE = 08/23/26
DEV86_CFLAGS = -w -std=gnu89 -Wno-implicit-function-declaration -Wno-int-conversion
SRCS = src/rombios.c src/rombios.h src/apmbios.S src/biossums.c
PATCHES = $(sort $(wildcard patches/*.patch))
B = $(CURDIR)/build
BCC = $(B)/dev86/bin/bcc
AS86 = $(B)/dev86/bin/as86

../BIOS-bochs-legacy: $(B)/bios/BIOS-bochs-legacy
	cp $< $@

$(SRCS) src/dev86.tar.gz:
	bash ../fetch-bios.sh

$(BCC): src/dev86.tar.gz
	rm -rf $(B)/dev86-src $(B)/dev86
	mkdir -p $(B)/dev86-src $(B)/dev86/bin $(B)/dev86/lib/bcc
	tar xzf $< -C $(B)/dev86-src --strip-components 1
	$(MAKE) -C $(B)/dev86-src CC="cc $(DEV86_CFLAGS)" HOSTCC="cc $(DEV86_CFLAGS)" bcc86 as86 ld86
	cp $(B)/dev86-src/bin/as86 $(B)/dev86/bin/
	cp $(B)/dev86-src/lib/bcc-cc1 $(B)/dev86/lib/bcc/
	cp $(B)/dev86-src/bin/Bcc $(BCC)

# Rebuilds when a patch is added or removed, not only when one changes.
$(B)/patches.list: FORCE
	@mkdir -p $(B)
	@echo $(PATCHES) | cmp -s - $@ || echo $(PATCHES) > $@

# rombios.h includes ../bxversion.h, which Bochs' configure generates.
$(B)/bios/BIOS-bochs-legacy: $(SRCS) $(PATCHES) $(B)/patches.list $(BCC) short_jcc.py Makefile
	rm -rf $(B)/bios
	mkdir -p $(B)/bios
	cp $(SRCS) $(B)/bios/
	for p in $(PATCHES); do patch -s -d $(B)/bios -p1 < $$p || exit 1; done
	printf '#define VERSION "3.0.devel"\n' > $(B)/bxversion.h
	cc -o $(B)/bios/biossums $(B)/bios/biossums.c
	cd $(B)/bios && cc -E -P -DLEGACY -DBIOS_BUILD_DATE='"$(BUILD_DATE)"' rombios.c > _rombiosl_.c
	cd $(B)/bios && $(BCC) -B$(B)/dev86/lib/bcc/ -o rombiosl.s -C-c -D__i86__ -0 -S _rombiosl_.c
	cd $(B)/bios && sed -e 's/^\.text//' -e 's/^\.data//' rombiosl.s > _rombiosl_.s
	cd $(B)/bios && python3 $(CURDIR)/short_jcc.py _rombiosl_.s BIOS-bochs-legacy rombiosl.txt $(AS86) -u- -w- -g -0 -j -O
	cd $(B)/bios && ./biossums BIOS-bochs-legacy

clean:
	rm -rf build ../BIOS-bochs-legacy

.PHONY: clean FORCE
