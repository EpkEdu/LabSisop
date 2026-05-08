obj-m         := khello.o
BUILDROOT_DIR := ../..
KDIR          := $(BUILDROOT_DIR)/output/build/linux-6.12.27
COMPILER      := $(BUILDROOT_DIR)/output/host/bin/i686-buildroot-linux-gnu-gcc

all: install

install: build
	$(MAKE) -C $(KDIR) M=$$PWD modules_install INSTALL_MOD_PATH=../../target

build:
	$(MAKE) -C $(KDIR) M=$$PWD modules

clean:
	$(MAKE) -C $(KDIR) M=$$PWD clean