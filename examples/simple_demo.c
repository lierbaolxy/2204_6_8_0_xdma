/*
 * simple_demo.c - 最简 XDMA 数据读取示例
 *
 * 只做一件事:从 FPGA 读 32 字节,打印出来。
 * 不涉及中断、寄存器、多线程,适合先验证数据通路。
 *
 * 编译: gcc -o simple_demo simple_demo.c
 * 运行: ./simple_demo
 */
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>

int main(void)
{
    int fd;
    uint8_t buf[32];
    int i;

    fd = open("/dev/xdma0_c2h_0", O_RDONLY);
    if (fd < 0) {
        perror("open /dev/xdma0_c2h_0");
        return 1;
    }

    /* 从 FPGA 地址 0x0 读 32 字节 */
    if (pread(fd, buf, sizeof(buf), 0x0) != sizeof(buf)) {
        perror("read");
        close(fd);
        return 1;
    }

    /* 打印 */
    printf("读到 %zu 字节:\n", sizeof(buf));
    for (i = 0; i < (int)sizeof(buf); i++) {
        printf("%02x ", buf[i]);
        if ((i + 1) % 16 == 0)
            printf("\n");
    }

    close(fd);
    return 0;
}
