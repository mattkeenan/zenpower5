VERSION         := 0.2.0
TARGET          := $(shell uname -r)
DKMS_ROOT_PATH  := /usr/src/zenpower-$(VERSION)

KBUILD_CFLAGS   += -Wimplicit-fallthrough=3

KERNEL_MODULES	:= /lib/modules/$(TARGET)

ifneq ("","$(wildcard /usr/src/linux-headers-$(TARGET)/*)")
# Ubuntu
KERNEL_BUILD	:= /usr/src/linux-headers-$(TARGET)
else
ifneq ("","$(wildcard /usr/src/kernels/$(TARGET)/*)")
# Fedora
KERNEL_BUILD	:= /usr/src/kernels/$(TARGET)
else
KERNEL_BUILD	:= $(KERNEL_MODULES)/build
endif
endif

obj-m	:= $(patsubst %,%.o,zenpower)
obj-ko	:= $(patsubst %,%.ko,zenpower)
zenpower-objs := zenpower_core.o zenpower_svi2.o zenpower_rapl.o zenpower_temp.o

.PHONY: all modules clean dkms-install dkms-install-swapped dkms-uninstall

all: modules

modules:
	@$(MAKE) -C $(KERNEL_BUILD) M=$(CURDIR) modules

clean:
	@$(MAKE) -C $(KERNEL_BUILD) M=$(CURDIR) clean

dkms-install:
	dkms --version >> /dev/null
	mkdir -p $(DKMS_ROOT_PATH)
	cp $(CURDIR)/dkms.conf $(DKMS_ROOT_PATH)
	cp $(CURDIR)/Makefile $(DKMS_ROOT_PATH)
	cp $(CURDIR)/zenpower.h $(DKMS_ROOT_PATH)
	cp $(CURDIR)/zenpower_core.c $(DKMS_ROOT_PATH)
	cp $(CURDIR)/zenpower_svi2.c $(DKMS_ROOT_PATH)
	cp $(CURDIR)/zenpower_rapl.c $(DKMS_ROOT_PATH)
	cp $(CURDIR)/zenpower_temp.c $(DKMS_ROOT_PATH)

	sed -e "s/@CFLGS@/${MCFLAGS}/" \
	    -e "s/@VERSION@/$(VERSION)/" \
	    -i $(DKMS_ROOT_PATH)/dkms.conf

	dkms add zenpower/$(VERSION)
	dkms build zenpower/$(VERSION)
	dkms install zenpower/$(VERSION)

dkms-uninstall:
	dkms remove zenpower/$(VERSION) --all
	rm -rf $(DKMS_ROOT_PATH)
