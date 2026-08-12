/*
 * trackball_demo.c - 轨迹球数据采集示例
 *
 * 数据流:
 *   FPGA 采集轨迹球数据 → 写入 BRAM 环形缓冲 → 触发中断(events_0)
 *   PC 端:阻塞等待中断 → DMA 读取数据 → 解析
 *
 * 编译: gcc -o trackball_demo trackball_demo.c
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdint.h>
#include <signal.h>

#define C2H_DEV       "/dev/xdma0_c2h_0"       /* FPGA→PC DMA 通道 */
#define H2C_DEV       "/dev/xdma0_h2c_0"       /* PC→FPGA DMA 通道 */
#define EVENT_DEV     "/dev/xdma0_events_0"    /* 中断通知通道 */

/* 轨迹球数据包格式(根据你的 FPGA 协议定义) */
#pragma pack(push, 1)
struct trackball_packet {
    uint8_t  source;     /* 数据源: 0=PS2 1=USB 2=RS422 3=CAN */
    uint8_t  button;     /* 按键状态 */
    int16_t  x_delta;    /* X 轴位移 */
    int16_t  y_delta;    /* Y 轴位移 */
    uint32_t timestamp;  /* FPGA 时间戳 */
};
#pragma pack(pop)

static volatile int running = 1;

static void sig_handler(int sig)
{
    running = 0;
}

/*
 * 等待 FPGA 中断通知
 * 返回: >0 有事件, 0 超时, <0 错误
 */
static int wait_event(int fd, int timeout_ms)
{
    uint32_t evt;
    fd_set rfds;
    struct timeval tv;
    int ret;

    FD_ZERO(&rfds);
    FD_SET(fd, &rfds);
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    ret = select(fd + 1, &rfds, NULL, NULL, &tv);
    if (ret < 0) {
        if (errno == EINTR)
            return 0;
        perror("select");
        return -1;
    }
    if (ret == 0)
        return 0;  /* 超时 */

    /* 读取消耗中断事件(每次读会阻塞直到下一个中断) */
    ret = read(fd, &evt, sizeof(evt));
    return ret > 0 ? 1 : -1;
}

/*
 * 通过 DMA 从 FPGA 读取数据
 * addr: FPGA 端 AXI 地址(BRAM 偏移)
 * size: 读取字节数
 * 返回: 实际读取字节数, <0 错误
 */
static ssize_t read_dma(int fd, void *buf, size_t size, uint64_t addr)
{
    ssize_t ret = pread(fd, buf, size, addr);
    if (ret < 0)
        perror("read dma");
    return ret;
}

/*
 * 通过 DMA 写数据到 FPGA
 * addr: FPGA 端 AXI 地址(BRAM 偏移)
 * size: 写入字节数
 * 返回: 实际写入字节数, <0 错误
 */
static ssize_t write_dma(int fd, const void *buf, size_t size, uint64_t addr)
{
    ssize_t ret = pwrite(fd, buf, size, addr);
    if (ret < 0)
        perror("write dma");
    return ret;
}

int main(int argc, char *argv[])
{
    int c2h_fd, h2c_fd, evt_fd;
    struct trackball_packet pkt;
    ssize_t nbytes;
    int ret;

    signal(SIGINT, sig_handler);

    /* 打开 C2H 通道(读 FPGA 数据) */
    c2h_fd = open(C2H_DEV, O_RDONLY);
    if (c2h_fd < 0) {
        perror("open " C2H_DEV);
        return 1;
    }

    /* 打开 H2C 通道(写数据到 FPGA) */
    h2c_fd = open(H2C_DEV, O_WRONLY);
    if (h2c_fd < 0) {
        perror("open " H2C_DEV);
        close(c2h_fd);
        return 1;
    }

    /* 打开 events 通道(等中断) */
    evt_fd = open(EVENT_DEV, O_RDONLY);
    if (evt_fd < 0) {
        perror("open " EVENT_DEV);
        close(c2h_fd);
        close(h2c_fd);
        return 1;
    }

    printf("XDMA 轨迹球采集启动\n");
    printf("  C2H (读): %s\n", C2H_DEV);
    printf("  H2C (写): %s\n", H2C_DEV);
    printf("  Event:    %s\n", EVENT_DEV);
    printf("等待轨迹球数据...(Ctrl+C 退出)\n\n");

    /* 主循环:中断驱动 */
    while (running) {
        /* 1. 阻塞等待 FPGA 中断(超时 1 秒) */
        ret = wait_event(evt_fd, 1000);
        if (ret <= 0)
            continue;

        /* 2. 中断到达,DMA 读取数据 */
        /* addr 是 FPGA 端 BRAM 中数据缓冲的 AXI 地址 */
        nbytes = read_dma(c2h_fd, &pkt, sizeof(pkt), 0x0000);
        if (nbytes < 0)
            continue;

        if (nbytes == sizeof(pkt)) {
            const char *src_name[] = {"PS2", "USB", "RS422", "CAN"};
            const char *src = pkt.source < 4 ?
                src_name[pkt.source] : "UNKNOWN";

            printf("[%s] btn=%d  dx=%d  dy=%d  ts=%u\n",
                   src, pkt.button,
                   pkt.x_delta, pkt.y_delta,
                   pkt.timestamp);

            /* 示例:收到数据后可以写命令回 FPGA */
            /* uint8_t ack = 0x01; */
            /* write_dma(h2c_fd, &ack, sizeof(ack), 0x1000); */
        }
    }

    printf("\n退出\n");
    close(c2h_fd);
    close(h2c_fd);
    close(evt_fd);
    return 0;
}
