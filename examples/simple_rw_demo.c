/*
 * simple_rw_demo.c - XDMA 读写双向最简示例
 *
 * 流程:
 *   1. 通过 H2C 往 FPGA 地址 0x0 写入 32 字节
 *   2. 通过 C2H 从 FPGA 地址 0x0 读回 32 字节
 *   3. 对比写入和读回的数据
 *
 * 编译: gcc -o simple_rw_demo simple_rw_demo.c
 * 运行: ./simple_rw_demo
 */
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <time.h>

#define BUF_SIZE  32
#define FPGA_ADDR 0x0

/* 计算两个时间点的差值(微秒) */
static long diff_us(struct timespec *start, struct timespec *end)
{
    return (end->tv_sec - start->tv_sec) * 1000000L +
           (end->tv_nsec - start->tv_nsec) / 1000L;
}

int main(void)
{
    int h2c_fd, c2h_fd;
    uint8_t write_buf[BUF_SIZE];
    uint8_t read_buf[BUF_SIZE];
    struct timespec t_start, t_end;
    long write_us, read_us, total_us;
    int i;

    /* 准备要写入的数据: 0x00, 0x01, 0x02, ... */
    for (i = 0; i < BUF_SIZE; i++)
        write_buf[i] = i;

    /* 1. 打开 H2C 通道(写) */
    h2c_fd = open("/dev/xdma0_h2c_0", O_WRONLY);
    if (h2c_fd < 0) {
        perror("open /dev/xdma0_h2c_0");
        return 1;
    }

    /* 2. 写数据到 FPGA(计时) */
    clock_gettime(CLOCK_MONOTONIC, &t_start);
    if (pwrite(h2c_fd, write_buf, BUF_SIZE, FPGA_ADDR) != BUF_SIZE) {
        perror("write h2c");
        close(h2c_fd);
        return 1;
    }
    clock_gettime(CLOCK_MONOTONIC, &t_end);
    write_us = diff_us(&t_start, &t_end);

    printf("写入 %d 字节到 FPGA 地址 0x%x (%ld us):\n", BUF_SIZE, FPGA_ADDR, write_us);
    for (i = 0; i < BUF_SIZE; i++) {
        printf("%02x ", write_buf[i]);
        if ((i + 1) % 16 == 0)
            printf("\n");
    }
    close(h2c_fd);

    /* 3. 打开 C2H 通道(读) */
    c2h_fd = open("/dev/xdma0_c2h_0", O_RDONLY);
    if (c2h_fd < 0) {
        perror("open /dev/xdma0_c2h_0");
        return 1;
    }

    /* 4. 从 FPGA 读回数据(计时) */
    clock_gettime(CLOCK_MONOTONIC, &t_start);
    if (pread(c2h_fd, read_buf, BUF_SIZE, FPGA_ADDR) != BUF_SIZE) {
        perror("read c2h");
        close(c2h_fd);
        return 1;
    }
    clock_gettime(CLOCK_MONOTONIC, &t_end);
    read_us = diff_us(&t_start, &t_end);

    printf("\n从 FPGA 地址 0x%x 读回 %d 字节 (%ld us):\n", FPGA_ADDR, BUF_SIZE, read_us);
    for (i = 0; i < BUF_SIZE; i++) {
        printf("%02x ", read_buf[i]);
        if ((i + 1) % 16 == 0)
            printf("\n");
    }
    close(c2h_fd);

    /* 5. 对比数据 */
    total_us = write_us + read_us;
    if (memcmp(write_buf, read_buf, BUF_SIZE) == 0)
        printf("\n✓ 数据一致,读写通路正常\n");
    else
        printf("\n✗ 数据不一致(可能 FPGA 端未实现回环,读到的是 FPGA 自身数据)\n");

    printf("\n--- 耗时统计 ---\n");
    printf("  写入 (H2C): %ld us\n", write_us);
    printf("  读取 (C2H): %ld us\n", read_us);
    printf("  总耗时:     %ld us\n", total_us);

    return 0;
}
