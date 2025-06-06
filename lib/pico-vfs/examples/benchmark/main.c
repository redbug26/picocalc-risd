/*
 * Benchmark tests with different block devices and file systems
 *
 * Copyright 2024, Hiroyuki OYAMA
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <errno.h>
#include <fcntl.h>
#include <hardware/clocks.h>
#include <pico/stdlib.h>
#include <stdio.h>
#include <string.h>
#include "blockdevice/flash.h"
#include "blockdevice/sd.h"
#include "filesystem/fat.h"
#include "filesystem/littlefs.h"
#include "filesystem/vfs.h"


#define COLOR_RED(format)    ("\e[31m" format "\e[0m")
#define COLOR_GREEN(format)  ("\e[32m" format "\e[0m")
#define BENCHMARK_SIZE       (1.0 * 1024 * 1024)
#define BUFFER_SIZE          (1024 * 64)

struct combination_map {
    blockdevice_t *device;
    filesystem_t *filesystem;
};

#define NUM_COMBINATION    4
static struct combination_map combination[NUM_COMBINATION];

static void print_progress(const char *label, size_t current, size_t total) {
    int num_dots = (int)((double)current / total * (50 - strlen(label)));
    int num_spaces = (50 - strlen(label)) - num_dots;

    printf("\r%s ", label);
    for (int i = 0; i < num_dots; i++) {
        printf(".");
    }
    for (int i = 0; i < num_spaces; i++) {
        printf(" ");
    }
    printf(" %zu/%zu bytes", current, total);
    fflush(stdout);
}

static void init_filesystem_combination(void) {
    blockdevice_t *flash = blockdevice_flash_create(0.5 * 1024 * 1024, 0);
    blockdevice_t *sd = blockdevice_sd_create(spi0,
                                              PICO_DEFAULT_SPI_TX_PIN,
                                              PICO_DEFAULT_SPI_RX_PIN,
                                              PICO_DEFAULT_SPI_SCK_PIN,
                                              PICO_DEFAULT_SPI_CSN_PIN,
                                              24 * MHZ,
                                              false);
    filesystem_t *fat = filesystem_fat_create();
    filesystem_t *littlefs = filesystem_littlefs_create(500, 16);
    combination[0] = (struct combination_map){.device = flash, .filesystem = littlefs};
    combination[1] = (struct combination_map){.device = flash, .filesystem = fat};
    combination[2] = (struct combination_map){.device = sd, .filesystem = littlefs};
    combination[3] = (struct combination_map){.device = sd, .filesystem = fat};
}

static uint32_t xor_rand(uint32_t *seed) {
    *seed ^= *seed << 13;
    *seed ^= *seed >> 17;
    *seed ^= *seed << 5;
    return *seed;
}

static uint32_t xor_rand_32bit(uint32_t *seed) {
    return xor_rand(seed);
}

static void benchmark_write(void) {
    const char *label = "Write";
    absolute_time_t start_at = get_absolute_time();

    int fd = open("/benchmark", O_WRONLY|O_CREAT);
    if (fd == -1) {
        printf("open error: %s\n", strerror(errno));
        return;
    }

    uint32_t counter = 0;
    xor_rand(&counter);
    uint8_t buffer[BUFFER_SIZE] = {0};
    size_t remind = BENCHMARK_SIZE;
    while (remind > 0) {
        size_t chunk = remind % sizeof(buffer) ? remind % sizeof(buffer) : sizeof(buffer);
        uint32_t *b = (uint32_t *)buffer;
        for (size_t j = 0; j < (chunk / sizeof(uint32_t)); j++) {
            b[j] = xor_rand_32bit(&counter);
        }

        ssize_t write_size = write(fd, buffer, chunk);
        if (write_size == -1) {
            printf("write: error: %s\n", strerror(errno));
            return;
        }
        remind = remind - write_size;
        print_progress(label, BENCHMARK_SIZE - remind, BENCHMARK_SIZE);
    }

    int err = close(fd);
    if (err == -1) {
        printf("close error: %s\n", strerror(errno));
        return;
    }

    double duration = (double)absolute_time_diff_us(start_at, get_absolute_time()) / 1000 / 1000;
    printf(" %.1f KB/s\n", (double)(BENCHMARK_SIZE) / duration / 1024);
}

static void benchmark_read(void) {
    const char *label = "Read";
    absolute_time_t start_at = get_absolute_time();

    int fd = open("/benchmark", O_RDONLY);
    if (fd == -1) {
        printf("open error: %s\n", strerror(errno));
        return;
    }

    uint32_t counter = 0;
    xor_rand(&counter);
    uint8_t buffer[BUFFER_SIZE] = {0};
    size_t remind = BENCHMARK_SIZE;
    while (remind > 0) {
        size_t chunk = remind % sizeof(buffer) ? remind % sizeof(buffer) : sizeof(buffer);
        ssize_t read_size = read(fd, buffer, chunk);
        if (read_size == -1) {
            printf("read error: %s\n", strerror(errno));
            return;
        }

       uint32_t *b = (uint32_t *)buffer;
        for (size_t j = 0; j < chunk / sizeof(uint32_t); j++) {
            volatile uint32_t v = xor_rand_32bit(&counter);
            if (b[j] != v) {
                printf("data mismatch\n");
                return;
            }
        }
        remind = remind - read_size;
        print_progress(label, BENCHMARK_SIZE - remind, BENCHMARK_SIZE);
    }

    int err = close(fd);
    if (err == -1) {
        printf("close error: %s\n", strerror(errno));
        return;
    }

    double duration = (double)absolute_time_diff_us(start_at, get_absolute_time()) / 1000 / 1000;
    printf(" %.1f KB/s\n", (double)(BENCHMARK_SIZE) / duration / 1024);
}

int main(void) {
    stdio_init_all();
    init_filesystem_combination();

    for (size_t i = 0; i < NUM_COMBINATION; i++) {
        struct combination_map setting = combination[i];
        printf("Test of %s on %s:\n", setting.filesystem->name, setting.device->name);

        int err = fs_format(setting.filesystem, setting.device);
        if (err == -1 && errno == 5005) {
            printf("skip, device not connected\n");
            continue;
        }
        if (err == -1) {
            printf("fs_format error: %s\n", fs_strerror(errno));
            return -1;
        }
        err = fs_mount("/", setting.filesystem, setting.device);
        if (err == -1) {
            printf("fs_mount / error: %s\n", fs_strerror(errno));
            return -1;
        }

        benchmark_write();
        benchmark_read();

        err = fs_unmount("/");
        if (err == 01) {
            printf("fs_unmount / error: %s\n", fs_strerror(errno));
            return -1;
        }
    }
    printf(COLOR_GREEN("ok\n"));
}
