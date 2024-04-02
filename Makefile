VERSION 		:= "0.1.0"
KERNELRELEASE	:= $(shell uname -r)
DKMS_PATH 		:= /usr/src/ryzenmonitor-$(VERSION)

obj-m += ryzenmonitor.o
CFLAGS_ryzenmonitor.o:=-DRYZENMONITOR_VERSION=$(VERSION)

.PHONY: all modules clean

all: modules

modules:
	$(MAKE) -C /lib/modules/$(KERNELRELEASE)/build M=$(PWD) modules

clean:
	$(MAKE) -C /lib/modules/$(KERNELRELEASE)/build M=$(PWD) clean

dkms-install:
	mkdir -p $(DKMS_PATH)
	cp $(CURDIR)/dkms.conf	    $(DKMS_PATH)/.
	cp $(CURDIR)/*.h 			$(DKMS_PATH)/.
	cp $(CURDIR)/*.c 			$(DKMS_PATH)/.
	cp $(CURDIR)/Makefile 		$(DKMS_PATH)/.

	sed -e "s/@VERSION@/$(VERSION)/" \
	    -i $(DKMS_PATH)/dkms.conf

	dkms add ryzenmonitor/$(VERSION)
	dkms build ryzenmonitor/$(VERSION)
	dkms install ryzenmonitor/$(VERSION)

dkms-uninstall:
	dkms remove ryzenmonitor/$(VERSION)
	rm -rf $(DKMS_PATH)
