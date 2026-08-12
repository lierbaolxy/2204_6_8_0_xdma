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
#define EVENT_DEV     "/dev/xdma0_events_0"    /* 中断通知通道 */
#define CONTROL_DEV   "/dev/xdma0_control"     /* 寄存器读写 */

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
    struct {
        uint32_t dummy;
    } evt;
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
    /* 用 pread 指定 FPGA 端地址,C2H/H2C 通道支持 */
    ssize_t ret = pread(fd, buf, size, addr);
    if (ret < 0)
        perror("read dma");
    return ret;
}

/*
 * 读写 FPGA 寄存器(control 设备不支持 lseek,必须用 pread/pwrite)
 */
static int write_register(int fd, uint32_t addr, uint32_t value)
{
    if (pwrite(fd, &value, sizeof(value), addr) != sizeof(value)) {
        perror("pwrite register");
        return -1;
    }
    return 0;
}

static uint32_t read_register(int fd, uint32_t addr)
{
    uint32_t value = 0;
    pread(fd, &value, sizeof(value), addr);
    return value;
}

int main(int argc, char *argv[])
{
    int c2h_fd, evt_fd, ctrl_fd;
    struct trackball_packet pkt;
    ssize_t nbytes;
    int ret;

    signal(SIGINT, sig_handler);

    /* 打开设备 */
    c2h_fd = open(C2H_DEV, O_RDWR);
    if (c2h_fd < 0) {
        perror("open " C2H_DEV);
        return 1;
    }

    evt_fd = open(EVENT_DEV, O_RDONLY);
    if (evt_fd < 0) {
        perror("open " EVENT_DEV);
        close(c2h_fd);
        return 1;
    }

    ctrl_fd = open(CONTROL_DEV, O_RDWR);
    if (ctrl_fd < 0) {
        perror("open " CONTROL_DEV);
        close(c2h_fd);
        close(evt_fd);
        return 1;
    }

    printf("XDMA 轨迹球采集启动\n");
    printf("  C2H:    %s\n", C2H_DEV);
    printf("  Event:  %s\n", EVENT_DEV);
    printf("  Ctrl:   %s\n", CONTROL_DEV);

    /* 可选:通过寄存器配置 FPGA 采集参数 */
    /* write_register(ctrl_fd, 0x4000, 0x01); */ /* 使能采集 */

    printf("等待轨迹球数据...\n\n");

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
        }
    }

    printf("\n退出\n");
    close(c2h_fd);
    close(evt_fd);
    close(ctrl_fd);
    return 0;
}
