# 虚拟 CAN 平台设计

## 文档信息

| 项目 | 内容 |
|------|------|
| **文档版本** | 2.0 |
| **创建日期** | 2026-01-02 |
| **最后修订** | 2026-01-03 |
| **作者** | UDS 诊断系统设计团队 |
| **状态** | 审查中 |

---

## 1. 引言

### 1.1 目的

本文档定义 UDS 诊断系统中虚拟 CAN 平台的设计,包括 Linux SocketCAN 集成、CAN 消息路由、流量控制、性能优化等。

### 1.2 范围

**包含内容**:
- Linux SocketCAN 接口封装
- 虚拟 CAN 总线配置
- CAN 消息路由和分发
- ISO-TP (ISO 15765-2) 传输层实现
- 多 ECU 网络模拟
- 性能优化和错误处理
- UDS 服务集成与 Bootloader 刷写支持

**依赖文档**:
- [03-UDS_Protocol_Design.md](03-UDS_Protocol_Design.md) - UDS 协议定义与服务规范
- [04-Bootloader_and_Flash_Design.md](04-Bootloader_and_Flash_Design.md) - Bootloader 刷写流程
- [01-System_Requirements_Specification.md](01-System_Requirements_Specification.md) - 系统需求基线

### 1.3 文档版本与改进说明

**v2.0 改进内容** (2026-01-03):

| 改进项 | 说明 | 对应章节 |
|--------|------|----------|
| **CAN ID 分配统一** | 与 03-UDS 协议对齐，采用 0x600/0x680 系列 | 4.2 |
| **超时参数统一** | P2/P2*/S3/STmin 与 03/04 文档保持一致 | 5.6, 10.2 |
| **UDS 服务集成** | 新增 UDS 17 核心服务的 ISO-TP 集成说明 | 5.7 |
| **Bootloader 场景** | 新增刷写场景的 CAN 通信流程与性能优化 | 7.4 |
| **NRC 错误映射** | CAN/ISO-TP 错误与 UDS NRC 的映射关系 | 10.4 |
| **性能基准** | 新增吞吐量、延迟等关键性能指标 | 11.0 |
| **多总线拓扑** | 完善多 vCAN 总线的路由与网关设计 | 7.3 |

---

## 2. SocketCAN 基础

### 2.1 SocketCAN 概述

SocketCAN 是 Linux 内核提供的 CAN 总线驱动框架,将 CAN 总线抽象为网络接口,使用标准的 Socket API 进行通信。

**特点**:
- 标准 BSD Socket 接口
- 支持 CAN 2.0A/B 和 CAN FD
- 内置 CAN 消息过滤
- 支持虚拟 CAN 接口 (vCAN)

### 2.2 SocketCAN 工作原理

```
┌─────────────────────────────────────────────────────────┐
│                    应用层                                │
│  ┌──────────────┬──────────────┬──────────────┐         │
│  │ UDS 应用     │ 诊断仪工具   │ 测试程序     │         │
│  └──────────────┴──────────────┴──────────────┘         │
├─────────────────────────────────────────────────────────┤
│                    Socket API                           │
│  socket(AF_CAN, SOCK_RAW, CAN_RAW)                      │
│  bind(), send(), recv()                                 │
├─────────────────────────────────────────────────────────┤
│                    SocketCAN 内核模块                   │
│  ┌──────────────┬──────────────┬──────────────┐         │
│  │ vCAN 驱动    │ can-dev      │ 网络层       │         │
│  └──────────────┴──────────────┴──────────────┘         │
├─────────────────────────────────────────────────────────┤
│                    虚拟硬件                              │
│  vCAN0, vCAN1 (虚拟 CAN 总线)                           │
└─────────────────────────────────────────────────────────┘
```

### 2.3 CAN 帧结构

**标准 CAN 帧** (can_frame):
```c
struct can_frame {
    canid_t can_id;  // 32 位 CAN ID
    uint8_t    can_dlc; // 数据长度码 (0-8)
    uint8_t    data[8]  __attribute__((aligned(8))); // 数据
};
```

**CAN FD 帧** (canfd_frame):
```c
struct canfd_frame {
    canid_t can_id;  // 32 位 CAN ID
    uint8_t    len;    // 数据长度 (0-64)
    uint8_t    flags;  // 标志位
    uint8_t    data[64] __attribute__((aligned(8))); // 数据
};
```

**CAN ID 格式**:
```
┌─────────────────────────────────────────────────────────┐
│ CAN ID (32 位)                                          │
├─────────────────────────────────────────────────────────┤
│ Bit 31: EFF 标志 (扩展帧)                               │
│ Bit 30: RTR 标志 (远程帧)                               │
│ Bit 29: 错误帧标志                                      │
│ Bit 0-28: CAN 标识符 (标准帧 11 位,扩展帧 29 位)        │
└─────────────────────────────────────────────────────────┘

标准帧 (CAN 2.0A): can_id = 0x123 (11 位)
扩展帧 (CAN 2.0B): can_id = 0x18FF1234 | CAN_EFF_FLAG (29 位)
```

---

## 3. 虚拟 CAN 总线配置

### 3.1 vCAN 接口创建

**创建虚拟 CAN 接口**:
```bash
# 加载 vCAN 内核模块
sudo modprobe vcan

# 创建 vCAN0 接口
sudo ip link add dev vCAN0 type vcan

# 启动接口
sudo ip link set up vCAN0

# 验证
ip link show vCAN0
```

**多虚拟总线**:
```bash
# 创建多个虚拟总线
sudo ip link add dev vCAN0 type vcan
sudo ip link add dev vCAN1 type vcan
sudo ip link add dev vCAN2 type vcan

# 启动所有接口
sudo ip link set up vCAN0
sudo ip link set up vCAN1
sudo ip link set up vCAN2
```

### 3.2 配置参数

| 参数 | 默认值 | 说明 |
|------|--------|------|
| 接口名称 | vCAN0 | 虚拟 CAN 接口名 |
| 波特率 | N/A | 虚拟接口不需要 |
| 消息队列长度 | 128 | 内核接收队列长度 |
| 发送队列长度 | 128 | 内核发送队列长度 |

**调整队列长度**:
```bash
# 推荐方式: 直接设置网卡发送队列长度
sudo ip link set vCAN0 txqueuelen 256

# 或者: 通过 sysfs 设置 (不同发行版路径可能略有差异)
echo 256 | sudo tee /sys/class/net/vCAN0/tx_queue_len
```

---

## 4. CAN 通信架构

### 4.1 通信层次

```
┌─────────────────────────────────────────────────────────┐
│              应用层 (Application Layer)                  │
│  ┌──────────────┬──────────────┬──────────────┐         │
│  │ UDS 诊断仪   │ 刷写工具     │ 测试框架     │         │
│  └──────────────┴──────────────┴──────────────┘         │
├─────────────────────────────────────────────────────────┤
│            UDS 诊断服务层 (UDS Service Layer)            │
│  ┌──────────────┬──────────────┬──────────────┐         │
│  │ 会话管理     │ 诊断服务     │ 刷写管理     │         │
│  └──────────────┴──────────────┴──────────────┘         │
├─────────────────────────────────────────────────────────┤
│          ISO-TP 传输层 (ISO 15765-2)                     │
│  ┌──────────────┬──────────────┬──────────────┐         │
│  │ 多帧分割     │ 多帧重组     │ 流量控制     │         │
│  └──────────────┴──────────────┴──────────────┘         │
├─────────────────────────────────────────────────────────┤
│           SocketCAN 适配层 (SocketCAN Adapter)           │
│  ┌──────────────┬──────────────┬──────────────┐         │
│  │ Socket 封装  │ 消息过滤     │ 错误处理     │         │
│  └──────────────┴──────────────┴──────────────┘         │
├─────────────────────────────────────────────────────────┤
│              物理层 (SocketCAN Driver)                   │
│  ┌──────────────┬──────────────┬──────────────┐         │
│  │ vCAN0        │ vCAN1        │ vCAN2        │         │
│  └──────────────┴──────────────┴──────────────┘         │
└─────────────────────────────────────────────────────────┘
```

### 4.2 CAN ID 分配

**诊断相关 CAN ID** (与 03-UDS 协议对齐):

| 功能 | CAN ID 格式 | 说明 | 示例 (ECU_ID=0x01) |
|------|-------------|------|--------------------|
| 物理寻址-请求 | 0x600 + ECU_ID | Tester → ECU | 0x601 |
| 物理寻址-响应 | 0x680 + ECU_ID | ECU → Tester | 0x681 |
| 功能寻址-请求 | 0x7DF | 广播到所有 ECU | 0x7DF |
| 功能寻址-响应 | 0x680 + ECU_ID | 多 ECU 响应 | 0x681-0x6FF |

**ECU ID 分配范围**:
- ECU_ID: 0x01 - 0x7F (支持最多 127 个 ECU)
- 0x00: 保留 (用于广播)
- 0x80-0xFF: 保留 (扩展功能)

**示例分配**:
```
ECU #1 (Bootloader/Application):
  - ECU_ID: 0x01
  - 请求 ID: 0x601
  - 响应 ID: 0x681

ECU #2 (Gateway):
  - ECU_ID: 0x02
  - 请求 ID: 0x602
  - 响应 ID: 0x682

ECU #3 (Sensor Node):
  - ECU_ID: 0x03
  - 请求 ID: 0x603
  - 响应 ID: 0x683

Tester:
  - 物理请求: 0x601-0x67F (单播)
  - 功能请求: 0x7DF (广播)
```

**地址类型选择** (参考 03-UDS 协议 1.3 节):
- **物理寻址**: 点对点通信，必须返回响应 (0x10 会话控制、0x27 安全访问等)
- **功能寻址**: 广播查询，可选响应 (0x19 读取 DTC、0x22 读取 DID 等)

**CAN ID 掩码配置**:
```c
// 接收特定 ECU 的请求
rfilter[0].can_id   = 0x601;  // ECU #1 请求
rfilter[0].can_mask = 0x7FF;  // 精确匹配

// 接收功能寻址广播
rfilter[1].can_id   = 0x7DF;  // 功能寻址
rfilter[1].can_mask = 0x7FF;  // 精确匹配

// 接收所有诊断响应
rfilter[2].can_id   = 0x680;  // 响应 ID 基址
rfilter[2].can_mask = 0x780;  // 匹配 0x680-0x6FF
```

---

## 5. ISO-TP 传输层实现

### 5.1 ISO-TP 概述

ISO-TP (ISO 15765-2) 是 CAN 总线上的网络层协议,支持超过 8 字节的长消息传输。

**帧类型**:
- **单帧** (Single Frame, SF): ≤ 7 字节数据
- **首帧** (First Frame, FF): > 7 字节数据的第一帧
- **连续帧** (Consecutive Frame, CF): 后续数据帧
- **流控帧** (Flow Control, FC): 流量控制

### 5.2 帧格式

#### 单帧 (SF)

```
┌───────────┬────────────┬──────────────────────┐
│ PCI (1B)  │ Length (1B) │ Data (0-6B)          │
├───────────┼────────────┼──────────────────────┤
│ 0x0L      │ L          │ Data                 │
│           │ (数据长度)  │                      │
└───────────┴────────────┴──────────────────────┘

L = 数据长度 (0-6)
PCI = 0x00 | L

示例: 0x03 22 F1 90
  0x03: 单帧,长度=3
  0x22: SID (Read DID)
  0xF1 90: DID
```

#### 首帧 (FF)

```
┌───────────┬──────────────────┬──────────────────────┐
│ PCI (1B)  │ Length (2B)      │ Data (0-6B)          │
├───────────┼──────────────────┼──────────────────────┤
│ 0x10      │ Length_Hi | Lo   │ First 6 Bytes        │
│           │ (总数据长度)      │                      │
└───────────┴──────────────────┴──────────────────────┘

示例: 0x10 0x12 62 F1 90 [5 Bytes VIN]
  0x10: 首帧
  0x0012 = 18: 总长度 18 字节
  0x62: 响应 SID
  0xF1 90: DID
  [5 字节 VIN]: 前 5 字节数据
```

#### 连续帧 (CF)

```
┌───────────┬──────────────────────┐
│ PCI (1B)  │ Data (0-7B)          │
├───────────┼──────────────────────┤
│ 0x2n      │ Data                 │
│           │                      │
└───────────┴──────────────────────┘

n = 序列号 (0-15), 循环使用

示例: 0x21 [7 Bytes VIN]
  0x21: 连续帧,序列号 1
  [7 字节 VIN]: 后续数据
```

#### 流控帧 (FC)

```
┌───────────┬────────────┬──────────────────┐
│ PCI (1B)  │ FS (1B)    │ BS (1B)          │
├───────────┼────────────┼──────────────────┤
│ 0x30      │ FC_flag   │ BlockSize        │
│           │            │ (块大小)         │
├───────────┴────────────┴──────────────────┤
│ STmin (1B)                                 │
│ (最小帧间隔)                               │
└────────────────────────────────────────────┘

FS (Flow Status):
  0 = ContinueToSend (继续发送)
  1 = Wait (等待)
  2 = Overflow/Abort (溢出/中止)

BS (BlockSize):
  0 = 连续发送,不限数量
  1-255 = 发送 BS 帧后等待 FC

STmin (SeparationTime):
  0 = 最小延迟 (127us - 255us)
  1-127 = 毫秒数
  0xF1-0xF9 = 100us - 900us
```

### 5.3 多帧传输流程

**发送端流程**:
```
应用数据 (18 字节)
    ↓
分割成多帧
    ↓
┌────────────────────────────┐
│ 发送首帧 (FF)               │
│ 0x10 0x0012 [前 6 字节]     │
└────────────────────────────┘
    ↓
等待流控帧 (FC)
    ↓
收到 FC: BS=0, STmin=5ms
    ↓
┌────────────────────────────┐
│ 发送连续帧 (CF)             │
│ 0x21 [7 字节]               │
│ 0x22 [7 字节]               │
│ 0x23 [2 字节]               │
└────────────────────────────┘
    ↓
传输完成
```

**接收端流程**:
```
收到首帧 (FF)
    ↓
解析总长度: 18 字节
    ↓
分配接收缓冲区
    ↓
发送流控帧 (FC)
    0x30 0x00 0x05 (BS=0, STmin=5ms)
    ↓
┌────────────────────────────┐
│ 接收连续帧 (CF)             │
│ 0x21 [7 字节] → 缓冲区      │
│ 0x22 [7 字节] → 缓冲区      │
│ 0x23 [2 字节] → 缓冲区      │
└────────────────────────────┘
    ↓
检查接收完整性
    ↓
组装完整消息
    ↓
交付应用层
```

### 5.4 ISO-TP 参数与推荐默认值

为保证与 UDS 刷写/诊断场景兼容，平台默认采用“保守稳定”的 ISO-TP 参数，必要时允许按测试用例覆盖。

| 参数 | 推荐默认值 | 说明 |
|------|------------|------|
| Addressing | Normal 11-bit | 与本项目 0x600/0x680 诊断 ID 方案匹配 |
| BS (BlockSize) | 0 | 连续发送不限块，减少 FC 往返 (vCAN 环境更稳定) |
| STmin | 5 ms | 帧间隔节流，避免多 ECU 场景 CF 洪泛 |
| Rx Buffer | ≥ 4096 bytes | 覆盖常见 DID/VIN/刷写块的重组需求 |

### 5.5 ISO-TP 与 SocketCAN 的接口形态

平台实现允许两种等价路径，便于开发与测试对照：

- **用户态 ISO-TP 栈**: 应用以 `CAN_RAW` 读写帧，自行完成 SF/FF/CF/FC 解析、重组与超时控制。
- **内核 ISO-TP Socket**: 使用 `CAN_ISOTP` 让内核处理分段与重组，应用直接收发“长报文”。

设计约束：无论采用哪种路径，对上层 UDS 交付的语义必须一致（超时、乱序、丢帧、FC Wait/Overflow 行为在测试中可复现）。

### 5.6 UDS over ISO-TP 集成

本平台的关键目标是把 [03-UDS_Protocol_Design.md](03-UDS_Protocol_Design.md) 定义的 UDS APDU（服务请求/响应）稳定承载到 vCAN，并为 [04-Bootloader_and_Flash_Design.md](04-Bootloader_and_Flash_Design.md) 的刷写流程提供一致的传输语义。

**分层数据流**:
```
UDS APDU (SID + Payload)
    ↓  (ISO-TP 分段/重组)
ISO-TP N-PDU
    ↓  (SocketCAN 发送/接收 can_frame)
CAN 帧 (8B)
```

**会话与连接上下文**:
- ISO-TP 重组状态按 “(总线, 对端 CAN ID)” 维度隔离，避免多 ECU 并发时串包。
- UDS 会话状态按 “ECU” 维度维护（会话类型、安全等级、S3 计时器）。

**功能寻址约束** (降低多 ECU 并发冲突风险):
- 功能寻址请求 (`0x7DF`) 允许多 ECU 响应，但建议响应只使用 **单帧**（避免长响应的总线冲突与重组歧义）。
- 需要长数据（例如大块数据、刷写数据、较长 DID）的业务，统一要求 tester 改用物理寻址请求（`0x600 + ECU_ID`）。

**UDS 定时器对齐**:
- UDS 的 P2/P2*/S3 由服务层控制（见 10.2），ISO-TP 的 N_* 仅用于传输层自我保护。
- 对于刷写/校验等长耗时操作，UDS 服务层应优先返回 `0x78 ResponsePending` 来延长业务等待，而不是无限增大 ISO-TP 超时。

---

## 6. 消息路由和分发

### 6.1 路由表设计

**路由表结构**:
```c
struct CanRouteEntry {
    uint8_t src_bus;          // 源总线 (0=vCAN0, 1=vCAN1, ...)
    uint32_t can_id;          // CAN ID
    uint32_t can_mask;        // CAN ID 掩码
    uint8_t ecu_id;           // 目标 ECU ID
    uint8_t is_tp;            // 是否 ISO-TP 消息
    uint8_t forward;          // 0=本地处理; 1=转发到 dst_bus
    uint8_t dst_bus;          // 目标总线 (forward=1 时有效)
    void (*handler)(const struct can_frame *frame, void *context);
    void *context;            // 处理函数上下文
};
```

**路由匹配原则**:
- 匹配维度为 `(src_bus, can_id & can_mask)`。
- 同一 CAN ID 可在不同总线上存在不同路由条目（便于多 vCAN 拓扑与网关场景）。
- `forward=1` 的条目优先用于“网关/分段总线”转发；`forward=0` 的条目用于 ECU/Tester 本地协议栈处理。
**跨总线 CAN ID 转换策略** (网关场景):

| 场景 | 策略 | 示例 | 说明 |
|------|------|------|------|
| **同域透传** | CAN ID 保持不变 | vCAN0(0x7DF) → vCAN1(0x7DF) | 本项目诊断域使用统一 0x600/0x680 方案，推荐策略 |
| **跨域映射** | CAN ID 按映射表转换 | vCAN0(0x7E0) → vCAN1(0x601) | 集成第三方 ECU 时可能需要，增加维护复杂度 |
| **域隔离** | 仅允许白名单 ID 跨总线 | 只转发 0x7DF 广播，阻断其他 | 安全敏感场景使用 |

推荐优先使用"同域透传"策略（CAN ID 不改写），简化调试与一致性验证。若确需跨域映射，应在路由表中用独立 handler 处理 `frame->can_id` 的改写。
**示例路由表**:
```c
const struct CanRouteEntry route_table[] = {
    // 物理寻址 - ECU #1 (ECU_ID=0x01): 请求 0x601
    { 0, 0x601, 0x7FF, 1, 1, 0, 0, ecu1_handler, NULL },

    // 物理寻址 - ECU #2 (ECU_ID=0x02): 请求 0x602
    { 0, 0x602, 0x7FF, 2, 1, 0, 0, ecu2_handler, NULL },

    // 功能寻址 - 广播
    { 0, 0x7DF, 0x7FF, 0xFF, 1, 0, 0, broadcast_handler, NULL },

    // 响应消息 (0x680 + ECU_ID)
    // 接收 ECU #1 响应: 0x681
    { 0, 0x681, 0x7FF, 1, 1, 0, 0, ecu1_response_handler, NULL },

    // 接收所有 ECU 响应: 0x680-0x6FF
    { 0, 0x680, 0x780, 0xFF, 1, 0, 0, ecu_response_handler, NULL },

    // 网关转发示例: 将 vCAN0 上的广播请求转发到 vCAN1
    { 0, 0x7DF, 0x7FF, 0xFF, 1, 1, 1, NULL, NULL },
};
```

### 6.2 消息分发流程

```
CAN 帧接收
    ↓
识别源总线 (src_bus)
    ↓
解析 CAN ID
    ↓
查找路由表
    ↓
┌──────────────────────────────┐
│ 找到匹配的路由条目?          │
└────┬─────────────────────┬───┘
     │ Yes                 │ No
     ↓                     ↓
┌──────────────────────────────┐
│ forward=1 ?                  │
└────┬─────────────────────┬───┘
     │ Yes                 │ No
     ↓                     ↓
转发到 dst_bus        调用处理函数
     │
     ↓
┌──────────────────────────────┐
│ 是否为 ISO-TP 消息?          │
└────┬─────────────────────┬───┘
     │ Yes                 │ No
     ↓                     ↓
ISO-TP 解析          直接处理
     │
     ↓
多帧重组
     │
     ↓
交付应用层
```

---

## 7. 多 ECU 网络模拟

### 7.1 ECU 抽象

**ECU 对象**:
```c
struct VirtualECU {
    uint8_t ecu_id;                // ECU ID
    char name[32];                 // ECU 名称
    uint32_t request_can_id;       // 请求 CAN ID
    uint32_t response_can_id;      // 响应 CAN ID
    int socket_fd;                 // Socket 描述符
    struct DiagnosticSession *session;  // 诊断会话
    void *user_data;               // 用户数据
};
```

### 7.2 多 ECU 拓扑

#### 7.2.1 单总线拓扑 (基本场景)

```
         vCAN0 总线
              │
    ┌─────────┼─────────┐
    │         │         │
    Tester    ECU #1      ECU #2
    (Tool)   (ECU_ID=0x01) (ECU_ID=0x02)

    功能请求: 0x7DF
    ECU#1 响应: 0x681
    ECU#2 响应: 0x682

说明:
    - Tester → ECU#1 物理请求: 0x601
    - Tester → ECU#2 物理请求: 0x602
    - Tester → All  功能请求: 0x7DF
    - ECU#1 → Tester 物理响应: 0x681
    - ECU#2 → Tester 物理响应: 0x682
```

#### 7.2.2 多总线拓扑 (网关场景)

```
        vCAN0 总线                 vCAN1 总线
             │                         │
    ┌────────┼────────┐       ┌────────┼────────┐
    │        │        │       │        │        │
  Tester  ECU #1  Gateway  ECU #3   ECU #4
  (Tool) (ECU_ID  (ECU_ID  (ECU_ID  (ECU_ID
         =0x01)   =0x02)   =0x03)   =0x04)
                     │
                     │ (桥接)
                     │
                     └─────────────────┘

消息路由:
  1) Tester 发送功能请求 0x7DF 到 vCAN0
  2) Gateway(ECU#2) 接收 vCAN0:0x7DF
  3) Gateway 转发到 vCAN1:0x7DF (透传)
  4) ECU#1/ECU#2 (vCAN0) 和 ECU#3/ECU#4 (vCAN1) 各自响应
  5) 响应消息反向聚合:
     - ECU#1 → 0x681 (vCAN0)
     - ECU#2 → 0x682 (vCAN0, 网关本身)
     - ECU#3 → 0x683 (vCAN1) → Gateway → 0x683 (vCAN0)
     - ECU#4 → 0x684 (vCAN1) → Gateway → 0x684 (vCAN0)

约束:
  - Gateway 需在两个总线上都监听相应 CAN ID
  - Gateway 的路由表需显式定义 vCAN0 ↔ vCAN1 的转发规则
  - 推荐对网关转发添加速率限制，避免环路风暴
  - ISO-TP 重组状态按 (bus, peer_id) 隔离，避免跨总线串包
```

### 7.3 网关功能 (可选)

**场景**: ECU #1 作为网关,转发 vCAN0 和 vCAN1 之间的消息

```c
// 简化网关转发：由路由表决定是否转发以及转发目标
int gateway_forward(const struct CanRouteEntry *entry,
                    const struct can_frame *frame,
                    int *bus_sockets) {
    if (!entry || !entry->forward) {
        return 0; // not forwarded
    }

    // 如需跨域转换 CAN ID，可在此处按策略改写 frame.can_id
    // 本项目诊断域默认使用 0x600/0x680 方案，通常无需改写。
    return write(bus_sockets[entry->dst_bus], frame, sizeof(*frame));
}
```

### 7.4 Bootloader 刷写场景支持 (与 04 对齐)

本节用于把 [04-Bootloader_and_Flash_Design.md](04-Bootloader_and_Flash_Design.md) 的刷写服务链路落到“虚拟 CAN + ISO-TP”上，明确消息方向、CAN ID 与并发约束，保证测试可复现。

**基本假设**:
- 目标 ECU: ECU_ID=0x01 (请求 0x601 / 响应 0x681)
- 刷写使用物理寻址（禁止使用 0x7DF 功能寻址下发刷写数据）
- 传输层默认 STmin=5ms、BS=0（见 5.4 / 10.2）

**典型刷写序列** (示例，具体约束以 04 为准):
```
Tester(0x601) → ECU(0x681)

1) 0x10 DiagnosticSessionControl (Programming Session)
2) 0x27 SecurityAccess (Seed/Key)
3) 0x34 RequestDownload (目标地址/长度)
4) 循环 N 次:
    0x36 TransferData (BlockSequenceCounter + Data)
    ECU 0x76 TransferDataResponse (BlockSequenceCounter)
5) 0x37 RequestTransferExit
6) 可选: 0x31 RoutineControl (校验/激活)
7) 0x11 ECUReset (重启进入新应用)
```

**并发与路由规则**:
- 刷写期间，一个 ECU 只允许一个“活跃下载会话”（单 tester 绑定）；其他 tester 对同一 ECU 的物理请求应返回忙/条件不满足类错误（具体 NRC 见 04/03）。
- 平台路由表必须支持“按 ECU_ID 隔离 ISO-TP 重组状态”，避免多 ECU 并行刷写时交叉污染。

**压测/故障注入建议**:
- 对 0x36 TransferData 支持可控的发送节流（STmin）与队列 back-pressure，避免用户态队列溢出导致非预期丢帧。
- 对刷写关键链路提供可选故障注入：丢 CF、重复 CF、乱序 CF、延迟 FC，用于验证 04 中的块序号幂等/超时/恢复策略。

---

## 8. SocketCAN API 封装

### 8.1 初始化

```c
int can_init(const char *interface, uint32_t *can_id, uint32_t *can_mask) {
    int sockfd;
    struct sockaddr_can addr;
    struct ifreq ifr;

    // 创建 Socket
    sockfd = socket(AF_CAN, SOCK_RAW, CAN_RAW);
    if (sockfd < 0) {
        return -1;
    }

    // 获取接口索引
    strcpy(ifr.ifr_name, interface);
    ioctl(sockfd, SIOCGIFINDEX, &ifr);

    // 绑定 Socket
    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;
    bind(sockfd, (struct sockaddr *)&addr, sizeof(addr));

    // 设置 CAN ID 过滤器 (支持多条)
    // 典型场景：同时监听 ECU 物理请求 + 功能广播
    struct can_filter rfilter[2];
    rfilter[0].can_id   = can_id ? *can_id : 0;         // 例如 0x601
    rfilter[0].can_mask = can_mask ? *can_mask : 0x7FF; // 精确匹配

    rfilter[1].can_id   = 0x7DF; // 功能寻址
    rfilter[1].can_mask = 0x7FF;

    setsockopt(sockfd, SOL_CAN_RAW, CAN_RAW_FILTER,
               &rfilter, sizeof(rfilter));

    return sockfd;
}
```

### 8.2 发送消息

```c
int can_send(int sockfd, uint32_t can_id, const uint8_t *data, uint8_t dlc) {
    struct can_frame frame;

    frame.can_id = can_id;
    frame.can_dlc = dlc;
    memcpy(frame.data, data, dlc);

    return write(sockfd, &frame, sizeof(frame));
}
```

### 8.3 接收消息

```c
int can_recv(int sockfd, uint32_t *can_id, uint8_t *data, uint8_t *dlc) {
    struct can_frame frame;

    int nbytes = read(sockfd, &frame, sizeof(frame));
    if (nbytes < 0) {
        return -1;
    }

    *can_id = frame.can_id;
    *dlc = frame.can_dlc;
    memcpy(data, frame.data, frame.can_dlc);

    return nbytes;
}
```

---

## 9. 性能优化

### 9.1 批量消息处理

```c
#define BATCH_SIZE 16

void process_can_messages_batch(int sockfd) {
    struct can_frame frames[BATCH_SIZE];
    int count;

    // 批量读取
    count = read(sockfd, frames, sizeof(frames));

    // 批量处理
    for (int i = 0; i < count; i++) {
        dispatch_message(&frames[i]);
    }
}
```

### 9.2 零拷贝优化

```c
// 使用 mmap 映射接收缓冲区
uint8_t *rx_buffer = mmap(NULL, buffer_size, ...);

// 直接操作接收缓冲区,避免 memcpy
process_frame((struct can_frame *)rx_buffer);
```

### 9.3 多线程处理

```
主线程
  │
  ├─ 接收线程 (RX Thread)
  │   └─ 读取 CAN 帧 → 接收队列
  │
  ├─ 处理线程 (Process Thread)
  │   └─ 从接收队列 → 处理 → 发送队列
  │
  └─ 发送线程 (TX Thread)
      └─ 从发送队列 → 发送 CAN 帧
```

---

## 10. 错误处理

### 10.1 CAN 总线错误

**错误类型**:
- **总线关闭** (Bus Off): CAN 控制器检测到严重错误
- **错误被动** (Error Passive): 错误计数器超过阈值
- **警告** (Error Warning): 错误计数器达到警告级别

**错误帧**:
```
can_id & CAN_ERR_FLAG: 错误帧
```

### 10.2 超时处理

本平台同时涉及 **ISO-TP 传输层定时器** 与 **UDS 服务层定时器**。为保持与 [03-UDS_Protocol_Design.md](03-UDS_Protocol_Design.md) / [04-Bootloader_and_Flash_Design.md](04-Bootloader_and_Flash_Design.md) 一致，统一采用以下默认值与约束。

**UDS 服务层定时器** (与 03/04 对齐):

| 定时器 | 默认值 | 说明 |
|-------|--------|------|
| P2 | 50 ms | 请求后，期望收到首个响应/响应Pending的时间 |
| P2* | 2000 ms | 收到 0x78 ResponsePending 后，延长等待时间 |
| S3 | 10000 ms | 会话保持超时 (无 TesterPresent/无业务请求) |

**ISO-TP 传输层关键参数/定时器** (建议默认):

| 参数/定时器 | 默认值 | 说明 |
|------------|--------|------|
| STmin | 5 ms | 连续帧最小间隔 (与 03/04 压测建议保持一致) |
| BS | 0 | 0=不分块限制 (连续发送，避免频繁 FC) |
| N_Bs | 1000 ms | 发送端等待 FC 的超时 |
| N_Cr | 1000 ms | 接收端等待下一帧 (CF) 的超时 |
| N_Cs | STmin + 50 ms | 发送端连续帧发送超时保护 |

**一致性约束** (推荐):
- N_Bs、N_Cr 应小于等于 P2*，避免 ISO-TP 超时先于 UDS 上层超时引发误判。
- 对于长耗时刷写/校验例程：UDS 层优先使用 0x78 ResponsePending 进行“业务级延时”，而不是无限放大 N_* 传输层超时。

### 10.3 错误恢复

```c
void handle_can_error(int error) {
    switch (error) {
        case CAN_BUS_OFF:
            // 重启 CAN 控制器
            can_restart();
            break;

        case ERROR_PASSIVE:
            // 等待错误计数器降低
            wait_for_recovery();
            break;

        case TIMEOUT:
            // 重传消息
            retransmit_message();
            break;
    }
}
```

### 10.4 CAN/ISO-TP 错误与 UDS NRC 映射

为避免“传输层故障被误当作业务否定响应”，本平台按以下原则处理错误与 NRC：

**原则**:
- **请求未完整接收**（ISO-TP 重组失败、CF 超时、序号错误等）：ECU 端丢弃重组状态，**不返回 UDS NRC**；tester 侧按 P2/P2* 超时重试。
- **请求已完整接收但暂不可处理**（资源忙、队列满、刷写互斥等）：返回 UDS NRC（优先 `0x21 busyRepeatRequest` 或 `0x22 conditionsNotCorrect`）。
- **请求语义非法**（长度/格式/参数越界）：返回 UDS NRC（例如 `0x13 incorrectMessageLengthOrInvalidFormat`、`0x31 requestOutOfRange`）。

**建议映射表** (实现可按测试用例开关细化):

| 场景/事件 | 检测点 | 处理建议 | 备注 |
|----------|--------|----------|------|
| CAN Bus-Off / Socket send 失败 | 发送响应时 | 不返回 NRC；记录错误并终止本次响应 | tester 侧体现为超时，可重试 |
| RX 队列溢出/丢帧 | 接收端 | 若未形成完整请求：不返回 NRC；若已完整但处理队列满：返回 `0x21` | 建议上报监控指标 |
| ISO-TP 等待 FC 超时 (N_Bs) | 发送端 | 终止本次多帧发送；不返回 NRC | 属于传输层异常 |
| ISO-TP 等待 CF 超时 (N_Cr) | 接收端 | 丢弃重组状态；不返回 NRC | 属于传输层异常 |
| CF 序列号错误/乱序 | 接收端 | 丢弃重组状态；不返回 NRC | 故障注入常用点 |
| 收到不支持/非法服务 | UDS 服务层 | 返回 `0x11 serviceNotSupported` / `0x12 subFunctionNotSupported` | 由 03 定义 |
| 刷写互斥冲突（已有活跃下载会话） | 刷写管理层 | 返回 `0x22` 或 `0x21` | 与 7.4 并发规则一致 |
| TransferData 块序号错误 | 刷写管理层 | 返回 `0x73 wrongBlockSequenceCounter` | 与 04 幂等规则保持一致 |

---

## 11. 性能与运维调优

### 11.1 性能基准 (建议)

以下指标用于“虚拟平台自身能力”评估，避免把刷写/诊断失败误归因到 UDS 逻辑。建议在单 ECU 与多 ECU 两类场景各跑一轮基准。

| 指标 | 单 ECU 建议目标 | 多 ECU (≥4) 建议目标 | 说明 |
|------|------------------|----------------------|------|
| CAN 帧吞吐 (frames/s) | ≥ 5,000 | ≥ 10,000 (总计) | vCAN 环境以稳定性优先 |
| ISO-TP 长报文完成延迟 (p99) | ≤ 20 ms | ≤ 50 ms | 与 STmin/调度相关 |
| UDS 请求-响应往返 (p99) | ≤ 50 ms | ≤ 100 ms | 不含 ECU 业务处理耗时 |
| 丢帧率 | 0 | 0 | 丢帧应仅由“故障注入”触发 |
| CPU 使用率 | ≤ 1 核心 | ≤ 2 核心 | 仅作开发机参考 |

- **队列水位**: vCAN 设备的 `txqueuelen` 与应用层 RX/TX 队列需成对配置 (默认 128, 压测时建议 256-512), 避免用户态溢出导致丢包。
- **流量整形**: 压测或多 ECU 场景下，启用发送端节流 (基于 STmin) 与接收端 back-pressure，防止 CF 洪泛。
- **时间源精度**: 超时计时统一使用单调时钟；在容器/虚拟机中需验证时钟漂移对 STmin/P2 的影响。
- **故障注入**: 提供可选开关模拟丢帧、乱序、延迟与 Error Frame，验证 ISO-TP 重传与 UDS NRC 行为。
- **监控指标**: 采集吞吐 (帧/秒)、重传次数、FC Wait/Overflow 次数、队列使用率、CAN 错误计数，供测试基准与调优。
- **多总线拓扑**: 支持 vcan0/vcan1 分段不同 ECU 或场景，路由表需显式标注源/目的总线，避免跨总线误转发。

---

## 版本历史

| 版本 | 日期 | 变更描述 |
|------|------|--------|
| 2.0 | 2026-01-03 | 与 03/04 对齐 CAN ID/超时参数；补齐 UDS over ISO-TP 集成、刷写场景、错误与 NRC 映射、性能基准 |
| 1.0 | 2026-01-02 | 虚拟 CAN 平台设计初始版本 |

---

## 参考文档

- [02-System_Architecture_Design.md](02-System_Architecture_Design.md) - 系统架构
- [03-UDS_Protocol_Design.md](03-UDS_Protocol_Design.md) - UDS 协议
- Linux SocketCAN Documentation: https://www.kernel.org/doc/Documentation/networking/can.txt
- ISO 15765-2:2016 - Diagnostic communication over CAN
