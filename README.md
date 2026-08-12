# Xilinx XDMA 驱动 Linux 6.8 内核移植

本仓库基于 Xilinx XDMA 2018.3 官方驱动源码,移植适配到 **Ubuntu 22.04 + Linux 6.8.0-136-generic** 内核,并修复了用户态测试工具段错误问题。所有测试通过,数据完整性校验正常。

## 环境信息

| 项目 | 版本 |
|------|------|
| 操作系统 | Ubuntu 22.04 LTS |
| 内核 | 6.8.0-136-generic |
| 硬件 | Xilinx FPGA PCIe 卡(PCI ID 10ee:7021) |
| gcc | 11.x(build-essential) |
| 源码版本 | XDMA 2018.3(v2018.3.50) |

## 目录结构

```
├── xdma/              # 内核驱动源码及 Makefile
├── libxdma/           # libxdma 库源码(独立编译用)
├── include/           # 公共头文件
├── tools/             # 用户态测试工具源码及预编译产物
├── tests/             # 测试脚本及测试数据
├── COPYING            # GPL 许可证
├── LICENSE            # BSD 许可证
└── readme.txt         # 原始说明文件
```

## 编译方法

### 1. 安装依赖

```bash
sudo apt-get install -y build-essential linux-headers-$(uname -r)
```

### 2. 编译内核驱动

```bash
cd xdma
make
# 产物:xdma.ko
```

### 3. 编译用户态工具

```bash
cd tools
make
# 产物:dma_to_device, dma_from_device, performance, reg_rw
```

> 仓库中已附带预编译产物,但 `xdma.ko` 的 `vermagic` 绑定 `6.8.0-136-generic`,**其他内核版本必须重新编译**。

## 安装与加载

```bash
# 安装驱动模块
sudo cp xdma/xdma.ko /lib/modules/$(uname -r)/updates/
sudo depmod -a
sudo modprobe xdma

# 验证
lsmod | grep xdma
ls -l /dev/xdma*
```

## 非 root 用户访问配置

### 1. 创建 xdma 用户组并加入用户

```bash
sudo groupadd xdma
sudo usermod -aG xdma $USER
# 重新登录或 newgrp xdma 生效
```

### 2. 配置 udev 规则

创建 `/etc/udev/rules.d/99-xdma.rules`:

```
# Xilinx XDMA character device permissions
SUBSYSTEM=="xdma", KERNEL=="xdma*", GROUP="xdma", MODE="0660"
```

### 3. 应用规则

```bash
sudo udevadm control --reload-rules
sudo udevadm trigger --subsystem-match=xdma
```

验证设备节点权限应为 `crw-rw---- root xdma`。

> 这是正规操作:udev 规则设置设备组归属和权限是 Linux 标准做法,不需要修改驱动源码,也不会影响安全性(仅授权 xdma 组成员访问)。

## 测试验证

```bash
cd tests
# 确保驱动已加载且当前用户在 xdma 组中
./run_test.sh
```

预期输出:
```
Info: All PCIe DMA memory mapped tests passed.
Info: All tests in run_tests.sh passed.
```

---

## 移植过程问题记录

以下记录从原始源码解压到最终验证通过期间遇到的所有问题及修复方案,按发生顺序排列。

### 问题 1:缺少编译工具链

**现象:** 执行 `make` 时报 `gcc: 未找到命令`。

**原因:** 系统未安装编译工具链。

**修复:** 安装 build-essential:
```bash
sudo apt-get install -y build-essential
```

---

### 问题 2:源码目录无写权限

**现象:** 编译时报 `mkdir: 无法创建目录...权限不够`。

**原因:** 原始源码解压在 root 拥有的目录 `/home/test1234/dma_ip_drivers-2018.3/`,普通用户无法在其中创建编译产物。

**修复:** 将源码复制到用户可写目录:
```bash
cp -r /home/test1234/dma_ip_drivers-2018.3/xdma /home/test1234/xdma_build/
```

---

### 问题 3:PCI DMA API 已移除

**现象:** 编译报错:
```
error: implicit declaration of function 'pci_unmap_page'; did you mean 'dma_unmap_page'?
error: implicit declaration of function 'pci_map_page'
error: implicit declaration of function 'pci_map_sg'
error: implicit declaration of function 'pci_unmap_sg'
```

**原因:** Linux 4.6+ 内核移除了 `pci_*` 系列 DMA 映射函数,统一使用 `dma_*` API,函数签名从 `pci_dev *pdev` 改为 `struct device *dev`。

**修复:** 在 `xdma/libxdma.c` 中替换所有 PCI DMA 函数,参数改为 `&xdev->pdev->dev`:

```c
// 修改前
pci_unmap_page(&pdev->dev, ...);
nents = pci_map_sg(&xdev->pdev->dev, ...);

// 修改后
dma_unmap_page(&pdev->dev, ...);
nents = dma_map_sg(&xdev->pdev->dev, ...);
```

涉及函数:`pci_map_page` → `dma_map_page`、`pci_unmap_page` → `dma_unmap_page`、`pci_map_sg` → `dma_map_sg`、`pci_unmap_sg` → `dma_unmap_sg`。

---

### 问题 4:swait_event_interruptible_timeout 宏被移除

**现象:** 编译报错:
```
error: implicit declaration of function 'swait_event_interruptible_timeout'
```

**原因:** Linux 6.1+ 内核从 `swait.h` 中移除了 `swait_event_interruptible_timeout` 宏,仅保留 `_exclusive` 版本。

**修复:** 在 `xdma/libxdma.c` 顶部添加兼容宏定义:

```c
#ifndef swait_event_interruptible_timeout
#define swait_event_interruptible_timeout(wq, condition, timeout)	\
({									\
	long __ret = timeout;						\
	if (!___wait_cond_timeout(condition))				\
		__ret = __swait_event_interruptible_timeout(wq,		\
						condition, timeout);	\
	__ret;								\
})
#endif
```

> 注:此宏为阶段性修复,最终在问题 13 中被彻底替换为标准 wait API。

---

### 问题 5:swake_up 函数不存在

**现象:** 编译报错:
```
error: implicit declaration of function 'swake_up'; did you mean 'wake_up'?
```

**原因:** 新内核中 `swake_up` 已移除,仅保留 `swake_up_one` 和 `swake_up_all`。

**修复:** 在 `xdma/libxdma.c` 中将所有 `swake_up` 替换为 `swake_up_all`:

```c
// 修改前
swake_up(&engine->shutdown_wq);
swake_up(&transfer->wq);

// 修改后
swake_up_all(&engine->shutdown_wq);
swake_up_all(&transfer->wq);
```

---

### 问题 6:mmiowb 函数不存在

**现象:** 编译报错:
```
error: implicit declaration of function 'mmiowb'
```

**原因:** Linux 5.10+ 内核移除了 `mmiowb()` 函数(在 x86 上本身是 no-op)。

**修复:** 在 `xdma/libxdma.h` 中添加 no-op 兼容宏:

```c
#ifndef mmiowb
#define mmiowb() do { } while (0)
#endif
```

---

### 问题 7:class_create 函数参数数量错误

**现象:** 编译报错:
```
error: too many arguments to function 'class_create'
```

**原因:** Linux 6.4+ 内核中 `class_create()` 签名从 `class_create(THIS_MODULE, name)` 改为 `class_create(name)`,移除了 owner 参数。

**修复:** 在 `xdma/xdma_cdev.c` 中去掉 `THIS_MODULE` 参数:

```c
// 修改前
g_xdma_class = class_create(THIS_MODULE, XDMA_NODE_NAME);

// 修改后
g_xdma_class = class_create(XDMA_NODE_NAME);
```

---

### 问题 8:pci_cleanup_aer_uncorrect_error_status 函数名变更

**现象:** 编译报错:
```
error: implicit declaration of function 'pci_cleanup_aer_uncorrect_error_status'
```

**原因:** Linux 6.8 内核中该函数被重命名为 `pci_aer_clear_nonfatal_status`。

**修复:** 在 `xdma/xdma_mod.c` 中替换函数名:

```c
// 修改前
pci_cleanup_aer_uncorrect_error_status(pdev);

// 修改后
pci_aer_clear_nonfatal_status(pdev);
```

---

### 问题 9:access_ok 宏参数数量错误

**现象:** 编译报错:
```
error: macro "access_ok" passed 3 arguments, but takes just 2
```

**原因:** Linux 5.0+ 内核中 `access_ok()` 签名从 `access_ok(type, addr, size)` 改为 `access_ok(addr, size)`,移除了 type 参数(原本的 VERIFY_READ/VERIFY_WRITE 已不再使用)。

**修复:** 在相关源码中去掉 type 参数:

```c
// 修改前
access_ok(VERIFY_WRITE, arg, size)

// 修改后
access_ok(arg, size)
```

---

### 问题 10:vm_flags 只读成员赋值

**现象:** 编译报错:
```
error: assignment of read-only member 'vm_flags'
```

**原因:** Linux 6.3+ 内核中 `vma->vm_flags` 成为只读成员,必须通过 `vm_flags_set()` 辅助函数修改。

**修复:** 将直接赋值改为函数调用:

```c
// 修改前
vma->vm_flags |= VMEM_FLAGS;

// 修改后
vm_flags_set(vma, VMEM_FLAGS);
```

---

### 问题 11:用户页管理 API 变更

**现象:** 编译报错(或运行时页引用计数错误):
```
get_user_pages_fast 已弃用
put_page 不再适用于 pinned pages
```

**原因:** Linux 6.5+ 内核中,对于长期 pin 的用户页,必须使用 `pin_user_pages_fast` 而非 `get_user_pages_fast`;释放时必须使用 `unpin_user_page` 而非 `put_page`,否则页引用计数会错乱。

**修复:** 在 `xdma/cdev_sgdma.c` 中替换 API:

```c
// 修改前
rv = get_user_pages_fast((unsigned long)buf, pages_nr, FOLL_WRITE, cb->pages);
...
put_page(cb->pages[i]);

// 修改后
rv = pin_user_pages_fast((unsigned long)buf, pages_nr, FOLL_WRITE, cb->pages);
...
unpin_user_page(cb->pages[i]);
```

---

### 问题 12:持自旋锁睡眠导致 scheduling while atomic(首次尝试修复)

**现象:** 驱动加载成功,但运行 `dma_to_device` 测试工具时段错误。`dmesg` 显示:
```
BUG: scheduling while atomic: dma_to_device/29964/0x00000002
Call Trace:
  schedule_timeout+0x95/0x170
  xdma_xfer_submit+0x582/0x890 [xdma]
  char_sgdma_read_write.isra.0+0x34a/0x3b0 [xdma]
dma_to_device[29964]: segfault at ... error 14 in libc.so.6
```

**原因:** `xdma_xfer_submit()` 函数中,在持有 `engine->desc_lock` 自旋锁的情况下调用了 `swait_event_interruptible_timeout()`,而该宏内部会调用 `schedule_timeout()` 睡眠。持锁睡眠违反了内核规则,触发 scheduling while atomic,进而破坏进程地址空间,导致用户态返回时 libc 代码页不可访问而段错误。

**修复(首次):** 在 `xdma/libxdma.c` 的 `xdma_xfer_submit()` 中,等待前释放 `desc_lock`,销毁传输前重新获取:

```c
// 修改前:持锁等待(错误)
spin_lock(&engine->desc_lock);
rv = transfer_queue(engine, xfer);
swait_event_interruptible_timeout(xfer->wq, ...);   // 持锁睡眠!
transfer_destroy(xdev, xfer);
spin_unlock(&engine->desc_lock);

// 修改后:等待前释放锁
spin_lock(&engine->desc_lock);
rv = transfer_queue(engine, xfer);
spin_unlock(&engine->desc_lock);           // 释放锁后再等待
swait_event_interruptible_timeout(xfer->wq, ...);
spin_lock(&engine->desc_lock);            // 重新获取锁销毁传输
transfer_destroy(xdev, xfer);
spin_unlock(&engine->desc_lock);
```

> 此修复缓解了问题,但段错误仍然存在,根因见问题 13。

---

### 问题 13:swait API 导致 preempt_count 不平衡(彻底修复)

**现象:** 问题 12 修复后,`dmesg` 仍报:
```
BUG: scheduling while atomic: dma_to_device/29970/0x00000002
xdma_xfer_submit+0x582/0x890 [xdma]   ← schedule_timeout 调用点
```
通过 `addr2line` 和 `objdump` 反汇编定位,0x582 偏移正是 `__swait_event_interruptible_timeout` 宏内部调用 `schedule_timeout` 的位置。此时 `preempt_count=2`,但代码中所有自旋锁均已正确配对释放。

**原因分析:**
- `swait`(simple wait queue)API 是为 RT(实时)内核设计的轻量级等待队列,语义与普通 `wait_queue` 不同
- Linux 6.8 的 `swait.h` 中已移除 `swait_event_interruptible_timeout` 宏(仅保留 `_exclusive` 版本)
- 驱动自定义的兼容宏调用 `__swait_event_interruptible_timeout`,其内部的 `prepare_to_swait_event`/`finish_swait` 在 6.8 普通内核(非 RT)中与 `preempt_count` 的交互存在异常,导致 `schedule_timeout` 被调用时 `preempt_count` 非零
- 这触发了 `schedule_debug()` 的 scheduling while atomic 检查,后续级联导致进程地址空间破坏和段错误

**验证证据:**
- DMA 数据传输本身是成功的(数据完整性检查通过)
- 段错误发生在传输完成后的清理/返回阶段
- `preempt_count=0x2` 无法用源码中的锁配对来解释

**修复方案:** 放弃 swait API,强制使用标准的 `wait_queue` API。在 `libxdma.h`、`libxdma.c`、`cdev_sgdma.c` 中将所有条件编译:

```c
// 修改前
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,6,0)
    struct swait_queue_head wq;           // 结构体用 swait
#else
    wait_queue_head_t wq;
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,6,0)
    init_swait_queue_head(&xfer->wq);     // 初始化用 swait
    swake_up_all(&transfer->wq);          // 唤醒用 swait
    swait_event_interruptible_timeout(...) // 等待用 swait
#else
    init_waitqueue_head(&xfer->wq);
    wake_up_interruptible(&transfer->wq);
    wait_event_interruptible_timeout(...)
#endif
```

统一改为强制走 `#else` 分支(共 19 处):

```c
#if 0 /* force standard wait queues; swait API causes preempt_count imbalance on 6.8+ */
    struct swait_queue_head wq;
#else
    wait_queue_head_t wq;                 // 始终使用标准 wait queue
#endif

// 所有调用点统一使用:
init_waitqueue_head(&xfer->wq);
wake_up_interruptible(&transfer->wq);
wait_event_interruptible_timeout(xfer->wq, condition, timeout);
```

涉及的文件和改动点:
- `xdma/libxdma.h`:3 处(结构体成员定义:`wq`、`shutdown_wq`、`xdma_perf_wq`)
- `xdma/libxdma.c`:14 处(初始化、唤醒、等待调用)
- `xdma/cdev_sgdma.c`:1 处(`xdma_perf_wq` 初始化)

**验证结果:** 重新编译加载后,`run_test.sh` 全部通过,无段错误,`dmesg` 无 scheduling while atomic 报错。

---

### 问题 14:udev 规则未立即生效

**现象:** 创建 `/etc/udev/rules.d/99-xdma.rules` 后,设备节点权限仍为 `crw------- root root`。

**原因:** udev 规则创建后不会自动应用到已存在的设备节点。

**修复:** 手动触发规则应用:

```bash
sudo udevadm control --reload-rules
sudo udevadm trigger --subsystem-match=xdma
```

触发后设备节点权限变为 `crw-rw---- root xdma`,非 root 用户即可访问。

---

## 关键文件改动清单

| 文件 | 改动内容 |
|------|---------|
| `xdma/libxdma.c` | DMA API 替换、swait→wait、desc_lock 释放时机、兼容宏 |
| `xdma/libxdma.h` | mmiowb 宏、swait→wait 结构体定义 |
| `xdma/cdev_sgdma.c` | pin_user_pages_fast、unpin_user_page、swait→wait |
| `xdma/xdma_cdev.c` | class_create 参数修正 |
| `xdma/xdma_mod.c` | pci_aer_clear_nonfatal_status 替换 |
| `.gitignore` | 排除编译产物 |

## 已知限制

1. **预编译 `xdma.ko` 仅适用于 6.8.0-136-generic 内核**,其他版本必须重新编译
2. 预编译测试工具仅适用于 x86-64 架构
3. 驱动原始版本为 2018.3,未包含 Xilinx 后续版本的修复和功能增强

## 参考

- 原始驱动源码:Xilinx DMA IP Core drivers 2018.3
- 内核 API 变更:https://www.kernel.org/doc/html/latest/process/changes.html
