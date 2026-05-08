obj-m         := timerdriver.o
BUILDROOT_DIR := $(CURDIR)/../..
KDIR          := $(BUILDROOT_DIR)/output/build/linux-6.12.27
COMPILER      := $(BUILDROOT_DIR)/output/host/bin/i686-buildroot-linux-gnu-gcc

all: install

install: build
	$(MAKE) -C $(KDIR) M=$(CURDIR) modules_install INSTALL_MOD_PATH=$(BUILDROOT_DIR)/output/target

build:
	$(MAKE) CC=$(COMPILER) -C $(KDIR) M=$(CURDIR) modules

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) clean