
CROSS_COMPILE ?= /work/linux/buildroot/output/rockchip_rk3566/host/bin/aarch64-buildroot-linux-gnu-
CC := $(CROSS_COMPILE)gcc

CFLAGS ?= -Wall -Wextra -O2
TARGET = device_manager
SRCS = device_manager.c

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $^

install: $(TARGET)
	install -D -m 0755 $(TARGET) $(DESTDIR)/usr/bin/device_manager

clean:
	rm -f $(TARGET)
