/*
 * reg_access.c - FPGA 寄存器读写工具
 *
 * 用法:
 *   读:  ./reg_access /dev/xdma0_control 0x0        (读偏移 0x0)
 *   写:  ./reg_access /dev/xdma0_control 0x0 0x12345678 (写 0x12345678 到 0x0)
 *
 * 编译: gcc -o reg_access reg_access.c
 */
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>

int main(int argc, char *argv[])
{
    int fd;
    uint32_t addr, value;

    if (argc < 3) {
        fprintf(stderr, "用法:\n");
        fprintf(stderr, "  读: %s <device> <addr>\n", argv[0]);
        fprintf(stderr, "  写: %s <device> <addr> <value>\n", argv[0]);
        return 1;
    }

    fd = open(argv[1], O_RDWR);
    if (fd < 0) {
        perror("open");
        return 1;
    }

    addr = strtoul(argv[2], NULL, 0);

    if (argc == 3) {
        /* 读寄存器(用 pread 绕过 control 设备不支持 lseek 的问题) */
        if (pread(fd, &value, sizeof(value), addr) != sizeof(value)) {
            perror("pread");
            close(fd);
            return 1;
        }
        printf("0x%08x = 0x%08x (%u)\n", addr, value, value);
    } else {
        /* 写寄存器(用 pwrite) */
        value = strtoul(argv[3], NULL, 0);
        if (pwrite(fd, &value, sizeof(value), addr) != sizeof(value)) {
            perror("pwrite");
            close(fd);
            return 1;
        }
        printf("写入 0x%08x = 0x%08x\n", addr, value);
    }

    close(fd);
    return 0;
}
