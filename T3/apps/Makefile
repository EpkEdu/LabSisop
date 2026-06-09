# Compila a aplicacao de teste com o toolchain do buildroot (i686).
# O binario resultante deve ser copiado para o target (ver pre-build.sh).

BUILDROOT_DIR := $(CURDIR)/../../linuxdistro/buildroot-2025.02.11
CC            := $(BUILDROOT_DIR)/output/host/bin/i686-buildroot-linux-gnu-gcc
CFLAGS        := -O2 -Wall -static

all: disktest

disktest: disktest.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f disktest
