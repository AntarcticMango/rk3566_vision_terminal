CROSS_COMPILE ?= /work/linux/buildroot/output/rockchip_rk3566/host/bin/aarch64-buildroot-linux-gnu-
CC := $(CROSS_COMPILE)gcc

CFLAGS ?= -Wall -Wextra -O2
LDFLAGS ?=

TARGET = ota_slotctl
SRCS = ota_slotctl.c

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^

clean:
	rm -f $(TARGET)

install:
	install -D -m 0755 $(TARGET) $(DESTDIR)/usr/bin/$(TARGET)