CROSS_COMPILE ?= /work/linux/buildroot/output/rockchip_rk3566/host/bin/aarch64-buildroot-linux-gnu-
CC := $(CROSS_COMPILE)gcc

SRC := rk_web_api.c
TARGET := rk_web_api

CFLAGS := -O2 -Wall -pthread

all:$(TARGET)

$(TARGET):$(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET)

clean:
	rm -f $(TARGET)
	