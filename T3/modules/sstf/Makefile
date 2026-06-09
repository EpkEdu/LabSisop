obj-m         := sstf.o

BUILDROOT_DIR := $(CURDIR)/../../../linuxdistro/buildroot-2025.02.11
KDIR          := $(BUILDROOT_DIR)/output/build/linux-6.12.27
COMPILER      := $(BUILDROOT_DIR)/output/host/bin/i686-buildroot-linux-gnu-gcc

# Cabecalho interno do subsistema de blocos (elevator.h inclui "blk-mq.h").
# Usa $(src) (fornecido pelo kbuild = diretorio deste modulo).
ccflags-y     := -I$(src)/../../../linuxdistro/buildroot-2025.02.11/output/build/linux-6.12.27/block

all: install

install: build
	$(MAKE) -C $(KDIR) M=$$PWD modules_install INSTALL_MOD_PATH=$(BUILDROOT_DIR)/output/target

build:
	$(MAKE) ARCH=i386 CC=$(COMPILER) -C $(KDIR) M=$$PWD modules

clean:
	$(MAKE) -C $(KDIR) M=$$PWD clean
