# 系统架构设计 (SAD) - UDS 诊断系统

## 文档信息

| 项目 | 内容 |
|------|------|
| 文档名称 | System Architecture Design |
| 版本 | 1.0 |
| 创建日期 | 2026-01-02 |
| 最后修改 | 2026-01-02 |

---

## 1. 架构概述

### 1.1 系统设计理念

UDS 诊断系统采用**分层架构 + 模块化设计**，将复杂的诊断功能分解为多个独立模块，各层通过明确的接口交互，实现高内聚、低耦合的系统设计。

```
┌─────────────────────────────────────────────────────────┐
│          应用层 (Application Layer)                      │
│  ┌──────────────┬──────────────┬──────────────┐         │
│  │ 诊断仪客户端 │ 刷写工具集合 │ 测试服务程序 │         │
│  └──────────────┴──────────────┴──────────────┘         │
└─────────────────────────────────────────────────────────┘
┌─────────────────────────────────────────────────────────┐
│       诊断服务层 (Diagnostic Service Layer)              │
│  ┌──────────────┐ ┌──────────────┐ ┌──────────────┐    │
│  │ 会话管理模块 │ │ 诊断服务模块 │ │ 刷写管理模块 │    │
│  └──────────────┘ └──────────────┘ └──────────────┘    │
└─────────────────────────────────────────────────────────┘
┌─────────────────────────────────────────────────────────┐
│    通信处理层 (Communication Processing Layer)           │
│  ┌──────────────┐ ┌──────────────┐ ┌──────────────┐    │
│  │ 报文编解码   │ │ 多帧分段处理 │ │ 流量控制     │    │
│  └──────────────┘ └──────────────┘ └──────────────┘    │
└─────────────────────────────────────────────────────────┘
┌─────────────────────────────────────────────────────────┐
│     物理层驱动 (Physical Driver Layer)                   │
│  ┌──────────────┐ ┌──────────────┐ ┌──────────────┐    │
│  │ SocketCAN    │ │ CAN 消息路由 │ │ 消息队列     │    │
│  └──────────────┘ └──────────────┘ └──────────────┘    │
└─────────────────────────────────────────────────────────┘
```

### 1.2 架构特点

1. **分层设计**: 各层职责清晰，独立开发和测试
2. **模块化**: 功能模块可独立加载和替换
3. **可扩展**: 支持新服务、新 ECU 类型的灵活扩展
4. **高可靠**: 完善的错误处理和恢复机制
5. **可测试**: 各层均可独立测试和验证

---

## 2. 分层架构详设

### 2.1 应用层 (Application Layer)

**职责**: 提供面向用户的诊断应用和工具

#### 2.1.1 诊断仪客户端 (Diagnostic Client)
- **功能**: 交互式诊断仪，支持命令行和 GUI
- **功能模块**:
  - 会话管理：切换诊断会话
  - 数据查询：读取 DTC、数据标识符
  - 命令执行：执行诊断程序、清除错误
  - 实时监控：显示 ECU 状态、参数变化
- **接口**: 与诊断服务层通信

#### 2.1.2 刷写工具集合 (Flashing Tools)
- **功能**: 固件刷写工具，支持加载、验证、刷写
- **功能模块**:
  - 文件加载：支持 S-Record、Hex、Bin 格式
  - 刷写控制：启动刷写、暂停、恢复、中止
  - 进度管理：显示刷写进度、速率、预计时间
  - 验证工具：刷写后数据验证
- **接口**: 与诊断服务层（刷写管理模块）通信

#### 2.1.3 测试服务程序 (Test Services)
- **功能**: 自动化测试和验证
- **功能模块**:
  - 用例执行：自动执行测试套件
  - 结果分析：生成测试报告
  - 性能评估：性能基准测试
- **接口**: 与诊断服务层通信

### 2.2 诊断服务层 (Diagnostic Service Layer)

**职责**: 实现 UDS 诊断协议的服务逻辑

#### 2.2.1 会话管理模块 (Session Manager)
- **功能**: 管理诊断会话生命周期
- **核心功能**:
  - 会话初始化：建立新的诊断会话
  - 会话切换：在不同会话类型间转换（0x10）
  - 会话超时：检测和处理超时
  - 心跳处理：处理 Tester Present (0x3E)
  - 会话清理：会话结束时清理资源
- **数据结构**:
  ```c
  struct DiagnosticSession {
      uint8_t session_type;        // Default, Programming, Extended, Safety
      uint32_t session_id;         // 会话唯一标识
      time_t start_time;           // 开始时间
      time_t last_heartbeat;       // 最后心跳时间
      uint8_t security_level;      // 安全级别 (0-3)
      uint32_t allowed_services;   // 允许的服务掩码
  };
  ```

#### 2.2.2 诊断服务模块 (Diagnostic Service Module)
- **功能**: 实现各项 UDS 诊断服务
- **服务分类**:
  - **会话和电源管理**: 0x10, 0x11
  - **数据传输**: 0x22, 0x24, 0x26, 0x2E
  - **诊断信息**: 0x14, 0x19
  - **安全访问**: 0x27
  - **通信控制**: 0x28, 0x85
  - **诊断程序**: 0x31
  - **心跳检测**: 0x3E
- **服务框架**:
  ```c
  typedef int (*ServiceHandler)(const uint8_t *request, uint16_t req_len,
                                uint8_t *response, uint16_t *resp_len);
  
  struct UdsService {
      uint8_t service_id;
      ServiceHandler handler;
      uint8_t min_session;         // 最低会话要求
      uint8_t min_security_level;  // 最低安全级别要求
      uint16_t request_min_len;    // 最小请求长度
      uint16_t response_max_len;   // 最大响应长度
  };
  ```

#### 2.2.3 刷写管理模块 (Flashing Manager)
- **功能**: 管理 ECU 固件刷写流程
- **核心功能**:
  - 刷写参数验证：地址范围、数据长度检查
  - 刷写状态机：管理刷写的各个阶段
  - 数据接收和缓存：处理分块数据
  - Flash 编程模拟：模拟 Flash 编程过程
  - 验证和校验：刷写后数据校验
- **刷写状态机**:
  ```
  IDLE → REQUEST_DOWNLOAD → RECEIVING_DATA 
    ↓                          ↓
  (0x34)                    (0x36)
    ↓                          ↓
    └──────→ REQUEST_EXIT ←────┘
                (0x37)
                  ↓
             DATA_VERIFY
                  ↓
             COMPLETE
  ```

#### 2.2.4 数据管理模块 (Data Management)
- **功能**: 管理 ECU 诊断数据
- **管理内容**:
  - 数据标识符 (DID) 定义和映射
  - 诊断故障码 (DTC) 记录和查询
  - 参数数据存储和读写
  - 内存模型（RAM、Flash）
- **数据结构**:
  ```c
  struct DataIdentifier {
      uint16_t did;
      uint16_t length;
      uint8_t *data;
      uint8_t read_only;
      uint16_t (*read_func)(uint8_t *buf);
      uint16_t (*write_func)(const uint8_t *buf);
  };
  ```

#### 2.2.5 安全管理模块 (Security Manager)
- **功能**: 实现安全访问和权限控制
- **核心功能**:
  - 种子生成和管理
  - 密钥验证
  - 会话权限管理
  - 非法访问记录
  - 访问失败锁定
- **安全流程**:
  ```c
  struct SecurityAccess {
      uint8_t level;               // 安全级别 (1-3)
      uint8_t seed[16];            // 随机种子
      time_t seed_issue_time;      // 种子签发时间
      uint8_t attempt_count;       // 尝试次数
      time_t lockout_time;         // 锁定时间
  };
  ```

### 2.3 通信处理层 (Communication Processing Layer)

**职责**: 处理 UDS 报文的编解码、多帧分段和流量控制

#### 2.3.1 报文编解码模块 (Message Codec)
- **功能**: UDS 报文的编码和解码
- **处理过程**:
  1. **编码** (Encoding):
     - 服务 ID + 参数组装
     - 报文长度计算
     - 校验和/CRC 计算
  2. **解码** (Decoding):
     - 报文格式验证
     - 服务 ID 提取
     - 参数解析
     - 校验和验证
- **报文格式**:
  ```
  [SID] [Sub-function] [Parameters] [Checksum]
  1B      1B           variable     1-4B
  ```

#### 2.3.2 多帧分段处理模块 (Segmentation & Reassembly)
- **功能**: 处理超过单帧的长消息
- **发送端**:
  - 长消息分割成多个 CAN 帧
  - 添加序列号
  - 发送数据帧，等待连续帧接收
- **接收端**:
  - 接收连续帧（Consecutive Frame）
  - 检查序列号连续性
  - 重组完整消息
  - 超时处理（接收不完整）
- **PCI (Protocol Control Information) 格式**:
  ```
  First Frame (FF):   [0x10] [Length_High] [Length_Low] [Data...]
  Consecutive (CF):   [0x2n] [Data...]  (n = sequence counter)
  Single Frame (SF):  [0x0L] [Data...]  (L = data length < 8)
  ```

#### 2.3.3 流量控制模块 (Flow Control)
- **功能**: 管理多帧发送的流量控制
- **核心功能**:
  - 流量控制帧生成和解析
  - 分块参数管理（块大小、分离时间）
  - 发送速率调整
  - 超时重传
- **流量控制帧 (FC) 格式**:
  ```
  [0x3n] [BlockSize] [SeparationTime]
  - n 的含义:
    0: ContinueToSend
    1: WaitOrOverflow
    2: OverflowIndicationActive
  ```

### 2.4 物理层驱动 (Physical Driver Layer)

**职责**: 实现 Linux SocketCAN 接口和 CAN 消息管理

#### 2.4.1 SocketCAN 封装模块 (SocketCAN Wrapper)
- **功能**: Linux SocketCAN API 的跨平台封装
- **功能**:
  - CAN 总线初始化
  - 发送和接收 CAN 帧
  - 错误处理和恢复
  - 总线状态管理
- **API 示例**:
  ```c
  int can_bus_open(const char *interface, uint32_t bitrate);
  int can_send_frame(int fd, const struct can_frame *frame);
  int can_recv_frame(int fd, struct can_frame *frame);
  int can_bus_close(int fd);
  ```

#### 2.4.2 CAN 消息路由模块 (CAN Message Router)
- **功能**: CAN 消息的路由和分发
- **功能**:
  - 多 ECU 支持：基于 CAN ID 路由到不同 ECU
  - 报文过滤：只接收相关的 CAN 消息
  - 优先级管理：高优先级消息优先处理
  - 广播和单播：支持不同通信模式
- **路由表结构**:
  ```c
  struct RouteEntry {
      uint32_t can_id;             // CAN ID
      uint32_t id_mask;            // ID 掩码
      uint8_t ecu_id;              // 目标 ECU ID
      void (*handler)(const struct can_frame *);  // 处理函数
  };
  ```

#### 2.4.3 消息队列模块 (Message Queue)
- **功能**: 缓冲和异步处理消息
- **队列类型**:
  - **接收队列** (RX Queue): 缓存接收到的 CAN 消息
  - **发送队列** (TX Queue): 缓冲待发送的消息
  - **优先级队列**: 根据优先级排序处理
- **队列管理**:
  ```c
  struct MessageQueue {
      struct CanMessage *buffer;
      uint16_t capacity;
      uint16_t head, tail;
      pthread_mutex_t lock;
      pthread_cond_t not_empty;
      pthread_cond_t not_full;
  };
  ```

---

## 2.5 数据流设计

### 2.5.1 请求处理流程

```
CAN Frame → SocketCAN → ISO-TP Decode → UDS Request
                                          ↓
                                    Session Manager
                                          ↓
                                    Security Manager
                                          ↓
                                    Service Handler
                                          ↓
                                    Data Manager
                                          ↓
                                    Response Builder
                                          ↓
                                    ISO-TP Encode → SocketCAN → CAN Frame
```

### 2.5.2 关键数据结构

**会话上下文**:
```c
struct SessionContext {
    uint8_t session_type;           // 当前会话类型
    uint8_t security_level;         // 已解锁的安全级别
    time_t last_activity;           // 最后活动时间
    uint32_t allowed_services;      // 允许的服务掩码
};
```

**刷写状态机上下文**:
```c
struct FlashContext {
    enum FlashState state;          // 当前状态
    uint32_t target_address;        // 目标地址
    uint32_t expected_size;         // 期望大小
    uint32_t received_size;         // 已接收大小
    uint8_t *buffer;                // 数据缓冲区
    uint32_t block_sequence;        // 块序号
};
```

---

## 2.6 非功能性需求映射

本节说明架构如何满足00大纲中定义的核心非功能需求：

### 2.6.1 可靠性需求 (Reliability)

| 需求 | 架构体现 | 实现机制 |
|------|--------|--------|
| 异常处理完善 | 各层均有明确的错误恢复路径 | NRC 负响应、异常日志、自动重试 |
| 超时管理 | 会话管理模块的超时检测机制 | Tester Present 0x3E 心跳、会话自动断开 |
| 数据校验 | 通信处理层的校验和/CRC 验证 | 单帧 CRC、多帧序号校验、数据重组验证 |
| 会话保护 | 会话管理的权限和状态机制 | 会话锁定、安全级别管理、操作权限掩码 |
| 刷写安全 | 刷写管理的状态机和验证 | 地址范围校验、数据完整性检查、后验证 |

### 2.6.2 可扩展性需求 (Extensibility)

**架构扩展点**：

1. **新诊断服务添加** (Service Registration)
   - 通过服务表注册新服务处理函数
   - 无需修改核心协议栈
   - 参考第6.1节

2. **新 ECU 类型适配** (ECU Adaptation)
   - 配置文件定义 DID/DTC 映射
   - 通过消息路由支持多 ECU
   - 参考第6.2节

3. **新通信方式支持** (Transport Adaptation)
   - 物理层提供统一的 SocketCAN 包装
   - 可扩展支持 CAN FD、DoIP 等传输

4. **新加密算法支持** (Security Upgrade)
   - 安全管理模块预留加密接口
   - 支持插件化密钥生成算法

### 2.6.3 性能需求映射

| 指标 | 目标值 | 架构支持 |
|------|-------|--------|
| 诊断响应延迟 | < 100ms | 优化的中断驱动处理 + 优先级队列 |
| 刷写吞吐量 | > 100 KB/s | 多帧 ISO-TP + 流控管理 + 批量缓冲 |
| 并发会话数 | ≥ 4 | 独立的会话上下文 + 线程池 |
| 内存占用 | ≤ 10 MB | 高效的数据结构设计 + 缓冲区预分配 |
| CPU 占用 | ≤ 30% (单核) | 事件驱动 + 异步处理 + 消息队列 |

### 2.6.4 可测试性需求映射

| 层级 | 测试方法 | 架构支持 |
|-----|--------|--------|
| 单元测试 | 各模块独立测试 | 清晰的模块边界 + 依赖注入 |
| 集成测试 | 跨层交互测试 | 明确的层间接口 |
| 端到端测试 | 完整业务流程测试 | 测试模式 + 模拟 ECU + 诊断客户端 |
| 性能测试 | 并发和压力测试 | 可配置的并发级别 + 性能计数器 |

---

## 3. 接口设计标准与规范 (Interface Definition & Specification)

### 3.1 层间接口规范

**应用层 ↔ 诊断服务层接口**：

```c
// 诊断请求/响应接口
typedef struct {
    uint8_t service_id;              // UDS 服务 ID
    uint8_t sub_function;            // 子功能
    uint8_t *params;                 // 参数数据
    uint16_t param_len;              // 参数长度
    uint8_t *response;               // 响应缓冲区
    uint16_t *response_len;          // 响应长度
    int (*callback)(int result);     // 异步回调
} UdsRequest;

// 主要接口函数
int uds_process_request(const UdsRequest *req);
int uds_switch_session(uint8_t session_type);
int uds_get_session_info(uint8_t *session_type, uint8_t *security_level);
int uds_start_flash(uint32_t address, uint32_t length);
```

**诊断服务层 ↔ 通信处理层接口**：

```c
// 报文编解码接口
typedef struct {
    uint8_t *buffer;
    uint16_t length;
    uint16_t max_length;
} Message;

int iso_tp_encode(const Message *uds_msg, Message *can_msg);
int iso_tp_decode(const Message *can_msg, Message *uds_msg);

// 多帧管理接口
typedef struct {
    uint8_t *data;
    uint32_t total_length;
    uint32_t received_length;
    uint8_t last_sequence;
} SegmentationContext;

int iso_tp_segment(const uint8_t *data, uint32_t len, Message *frames[], uint16_t *frame_count);
int iso_tp_reassemble(const Message *frame, SegmentationContext *ctx);
```

**通信处理层 ↔ 物理层接口**：

```c
// CAN 帧操作接口
typedef struct {
    uint32_t can_id;
    uint8_t dlc;
    uint8_t data[8];
    uint32_t timestamp;
} CanFrame;

int can_send(const CanFrame *frame);
int can_recv(CanFrame *frame, uint32_t timeout_ms);
int can_open(const char *ifname, uint32_t bitrate);
int can_close(void);
```

### 3.2 错误码与超时约定

**统一错误码体系**：
```c
#define UDS_OK                       0      // 成功
#define UDS_ERROR_INVALID_REQ       -1      // 无效请求
#define UDS_ERROR_INVALID_SESSION   -2      // 无效会话
#define UDS_ERROR_SECURITY_DENIED   -3      // 安全拒绝
#define UDS_ERROR_CAN_TIMEOUT       -4      // CAN 超时
#define UDS_ERROR_BUFFER_OVERFLOW   -5      // 缓冲区溢出
#define UDS_ERROR_SERVICE_NOT_IMPL  -6      // 服务未实现
#define UDS_ERROR_INCOMPATIBLE_STATE -7     // 不兼容状态

// 所有接口均返回统一的错误码，详细信息通过日志系统上报
```

**超时配置参数**：
```c
#define SESSION_TIMEOUT_DEFAULT     5000    // 5 秒 (标准会话)
#define SESSION_TIMEOUT_EXTENDED    30000   // 30 秒 (编程会话)
#define TESTER_PRESENT_INTERVAL     3000    // 3 秒 (心跳间隔)
#define CAN_FRAME_TIMEOUT           1000    // 1 秒 (单帧等待)
#define ISO_TP_SEGMENT_TIMEOUT      2000    // 2 秒 (连续帧等待)
#define FLASH_ERASE_TIMEOUT         10000   // 10 秒 (擦除超时)
#define SECURITY_LOCKOUT_DURATION   300000  // 5 分钟 (安全锁定)
```

---

## 3.3 关键业务流程交互

### 3.3.1 典型交互流程：会话切换

```
User App                Session Manager         Diagnostic Services
   │                          │                        │
   ├─ 发送会话切换请求 ──────→ │                        │
   │  (Session Control)       │                        │
   │                          ├─ 验证当前状态           │
   │                          │  ├─ 检查权限            │
   │                          │  ├─ 检查超时            │
   │                          │                        │
   │                          ├─ 调用会话切换 ────────→ │
   │                          │                        │
   │                          │ ← 返回结果 ────────────┤
   │                          │                        │
   │                          ├─ 更新会话状态           │
   │                          │  └─ 重置超时计时器      │
   │                          │                        │
   │ ← 返回肯定响应 ─────────┤                        │
   │
```

### 3.3.2 典型交互流程：数据读取

```
Diagnostic Client          Comm Processing          Physical Driver
     │                              │                       │
     ├─ 发送读取请求 ──────────────→ │                       │
     │  (Read Data 0x22)            │                       │
     │                              ├─ 报文编码              │
     │                              ├─ 报文验证              │
     │                              │                       │
     │                              ├─ 发送 CAN 帧 ────────→ │
     │                              │                       │
     │ ← CAN 帧接收 ────────────────│ ← 消息队列 ───────────┤
     │                              │                       │
     │                              ├─ 报文解码              │
     │                              ├─ 参数验证              │
     │                              │                       │
     │ ← 返回数据 ──────────────────┤                       │
     │
```

### 3.3.3 异常处理流程示例：会话超时恢复

```
诊断客户端            会话管理           通信层              日志系统
   │                   │               │                   │
   ├─ 发送请求 ───────→ │               │                   │
   │                   ├─ 检测超时      │                   │
   │                   │ (3s 无活动)    │                   │
   │                   │                │                   │
   │                   ├─ 清理会话上下文 │                   │
   │                   │                ├─ 关闭待发送队列    │
   │                   │                │                   │
   │ ← 返回 NRC 0x24   │                │                   │
   │ (RequestSequence  │                │                   │
   │  Error)           │                │                   │
   │                   ├─ 记录会话超时事件 ────────────────→│
   │                   │                │                   │
   │ [重新建立会话]    ├─ 接受新的会话建立请求
   │
```

---

## 4. 关键路径与性能设计

### 4.1 关键路径识别

系统中的关键路径（影响性能、可靠性的核心路径）：

| 路径 | 组件 | 性能要求 | 优化策略 |
|------|------|--------|--------|
| 诊断请求响应 | 报文解码 → 服务处理 → 报文编码 | < 100ms | 优先级队列 + 预分配缓冲 |
| 刷写数据流 | 多帧接收 → 缓冲 → Flash 编程 | > 100KB/s | 流控管理 + 批量处理 |
| 会话切换 | 会话清理 → 权限更新 → 状态变更 | < 50ms | 快速路径 + 最小化锁时间 |
| 多帧重组 | ISO-TP 分割 → 序号验证 → 重组 | 无frame丢失 | 超时控制 + 自动重试 |
| 安全认证 | 种子获取 → 密钥验证 → 权限授予 | < 200ms | 加密硬件加速（可选） |

### 4.2 性能热点优化

**缓冲区管理**：
- 预分配固定大小缓冲池，避免频繁 malloc/free
- 为多帧 ISO-TP 分配 4KB 接收缓冲（最大刷写消息）
- 为每个并发会话预留 512B 的会话上下文

**中断驱动处理**：
- CAN 接收中断直接投递到优先级队列
- 消息处理线程按优先级 (诊断请求 > 刷写数据 > 心跳) 处理
- 避免长时间持锁，快速释放中断上下文

**流量控制优化**：
- 动态调整 ISO-TP 块大小 (BS = 0 表示不限制)
- 分离时间 (STmin = 0 表示无间隔)
- 当接收方处理缓慢时返回 "Wait" 状态

---

## 5. 配置与运维架构 (Configuration & Operations)

### 5.1 配置管理体系

**配置分类**：

| 配置类型 | 存储位置 | 加载时机 | 可热加载 | 示例 |
|---------|--------|--------|--------|------|
| 静态配置 | config/uds_config.json | 启动时 | 否 | CAN 比特率、ISO-TP 参数 |
| ECU 配置 | config/ecu_profiles/ | 启动时 | 否 | DID/DTC 映射、内存布局 |
| 会话配置 | config/sessions.json | 启动时 | 否 | 会话类型、超时参数 |
| 权限配置 | config/access_control.json | 启动时 | 否 | 服务权限、安全级别 |
| 日志配置 | config/logging.json | 启动时 | 是 | 日志级别、输出目标 |
| 运行参数 | 环境变量/命令行 | 启动时 | 是 | 调试模式、性能模式 |

**配置文件示例结构**：
```json
{
  "uds": {
    "can_interface": "vcan0",
    "can_bitrate": 500000,
    "iso_tp_bs": 0,
    "iso_tp_stmin": 0,
    "max_concurrent_sessions": 4,
    "session_timeout_ms": 5000
  },
  "ecu": {
    "id": 0x01,
    "name": "Engine_ECU",
    "did_file": "config/ecu_profiles/engine_did.json",
    "dtc_file": "config/ecu_profiles/engine_dtc.json",
    "flash_size_mb": 4,
    "ram_size_kb": 512
  },
  "security": {
    "seed_key_algorithm": "xor",
    "seed_length": 4,
    "max_unlock_attempts": 3,
    "lockout_duration_ms": 300000
  },
  "logging": {
    "level": "INFO",
    "output": ["file", "console"],
    "file_path": "logs/uds.log",
    "max_file_size_mb": 100,
    "rotation_count": 10
  }
}
```

### 5.2 可观测性与日志架构

**日志系统分层**：

```
应用层日志 (User Actions)
  ├─ 诊断命令执行
  ├─ 刷写进度
  └─ 错误通知
         ↓
诊断服务层日志 (Service Events)
  ├─ 会话生命周期事件
  ├─ 服务调用与结果
  └─ 权限与安全事件
         ↓
通信处理层日志 (Protocol Events)
  ├─ 报文编解码
  ├─ 多帧重组
  └─ 流量控制
         ↓
物理层日志 (Bus Events)
  ├─ CAN 帧发送/接收
  ├─ 总线错误
  └─ 时序信息
         ↓
统计与性能指标
  ├─ 吞吐量 (KB/s)
  ├─ 延迟 (ms)
  ├─ 错误率 (%)
  └─ 并发会话数
```

**关键事件日志规范**：

| 事件 | 日志级别 | 模块 | 关键信息 |
|-----|--------|------|--------|
| 会话建立 | INFO | Session Mgr | 会话ID、类型、时间戳 |
| 会话超时 | WARN | Session Mgr | 会话ID、超时原因 |
| 安全认证失败 | WARN | Security Mgr | 失败原因、尝试次数、IP地址 |
| 刷写开始 | INFO | Flash Mgr | 目标地址、大小、ECU ID |
| 刷写完成 | INFO | Flash Mgr | 成功/失败、校验值、耗时 |
| NRC 返回 | WARN | Service Module | NRC码、对应服务、会话状态 |
| CAN 超时 | ERROR | Physical Layer | CAN ID、最后报文时间 |
| 内存溢出 | ERROR | Comm Layer | 缓冲区名称、请求大小、可用大小 |

**性能计数器** (暴露给测试框架)：
```c
struct PerformanceMetrics {
    uint64_t total_requests;      // 总请求数
    uint64_t successful_requests; // 成功请求数
    uint64_t failed_requests;     // 失败请求数
    uint64_t avg_response_time_ms;// 平均响应时间
    uint64_t max_response_time_ms;// 最大响应时间
    uint64_t bytes_flashed;       // 已刷写字节数
    uint64_t flash_throughput_kbs;// 刷写吞吐量
    uint32_t active_sessions;     // 当前活动会话数
    uint32_t peak_sessions;       // 峰值并发会话数
};
```

---

## 6. 部署与扩展架构 (Deployment & Extensibility)

### 6.1 部署拓扑

**单机多总线部署** (推荐初期配置)：

```
┌─────────────────────────────────────────────┐
│            Linux Host (单机)                  │
│                                             │
│  ┌──────────────────────────────────────┐  │
│  │   虚拟 CAN 总线                       │  │
│  │  ┌─────────────┬─────────────┐       │  │
│  │  │  vcan0      │   vcan1     │       │  │
│  │  │  (诊断)     │   (刷写)    │       │  │
│  │  └──────┬──────┴──────┬──────┘       │  │
│  │         │             │              │  │
│  │  ┌──────▼─────┐  ┌────▼──────┐      │  │
│  │  │ 诊断仪     │  │ ECU仿真器 │      │  │
│  │  │ (应用层)   │  │(UDS stack)│      │  │
│  │  └────────────┘  └───────────┘      │  │
│  └──────────────────────────────────────┘  │
│                                             │
│  ┌──────────────────────────────────────┐  │
│  │   控制台/监控工具                     │  │
│  │  ├─ 日志查看器                        │  │
│  │  ├─ 性能监控                         │  │
│  │  └─ 测试运行器                       │  │
│  └──────────────────────────────────────┘  │
└─────────────────────────────────────────────┘
```

**多容器部署** (扩展配置)：

```
Host 网络 (--net=host)
├─ Container A: UDS 诊断服务 (含仿真ECU)
│  └─ 访问 vcan0/vcan1
├─ Container B: 诊断仪客户端
│  └─ 访问 vcan0/vcan1
└─ Container C: 测试框架
   └─ 访问 vcan0/vcan1
   
(所有容器共享物理主机的SocketCAN接口)
```

### 6.2 服务扩展机制

**快速添加新诊断服务** (插件化)：

```c
// 1. 实现服务处理函数
static int service_0x3A_handler(
    const UdsRequest *req,
    UdsResponse *resp) {
    // 实现服务逻辑
    resp->service_id = 0x7A;  // 肯定响应
    return UDS_OK;
}

// 2. 在服务表注册 (动态或编译时)
const struct UdsServiceDef my_service = {
    .service_id = 0x3A,
    .handler = service_0x3A_handler,
    .min_session = SESSION_EXTENDED,
    .min_security_level = 1,
    .request_min_len = 2,
    .response_max_len = 256
};

// 3. 调用注册函数
int ret = uds_register_service(&my_service);
```

### 6.3 新 ECU 类型适配

**配置驱动的 ECU 定义**：

```json
{
  "ecu_type": "Transmission_ECU_v3",
  "can_request_id": 0x65A,
  "can_response_id": 0x65B,
  "data_identifiers": [
    {
      "did": "0xF180",
      "name": "Identifier",
      "length": 6,
      "read_only": true,
      "data": "TR_ECU_V3"
    },
    {
      "did": "0xF186",
      "name": "Active Diagnostic Session",
      "length": 1,
      "dynamic": true
    }
  ],
  "dtc_list": [
    {
      "code": "0xP0740",
      "name": "Torque Converter Clutch Circuit",
      "severity": "warning"
    }
  ],
  "supported_services": [0x10, 0x11, 0x14, 0x19, 0x22, 0x27, 0x2E, 0x3E],
  "security_seeds": [
    {
      "level": 1,
      "algorithm": "xor",
      "seed_length": 4,
      "key_length": 4
    }
  ]
}
```

---

## 4. 并发和同步机制

### 4.1 多线程模型

```
主线程 (Main Thread)
  │
  ├─ 诊断服务线程 (Diagnostic Service Thread)
  │   ├─ 请求处理
  │   ├─ 会话管理
  │   └─ 数据处理
  │
  ├─ CAN 接收线程 (CAN RX Thread)
  │   ├─ 读取 CAN 消息
  │   ├─ 放入接收队列
  │   └─ 触发处理事件
  │
  ├─ CAN 发送线程 (CAN TX Thread)
  │   ├─ 从发送队列取出消息
  │   ├─ 发送 CAN 帧
  │   └─ 处理流量控制
  │
  ├─ 超时管理线程 (Timeout Manager Thread)
  │   ├─ 监测会话超时
  │   ├─ 监测操作超时
  │   └─ 清理过期资源
  │
  └─ 诊断应用线程 (Application Thread)
      ├─ 用户命令处理
      └─ 结果显示
```

### 4.2 线程间同步

- **互斥锁** (Mutex): 保护共享数据结构
  - 会话表
  - 消息队列
  - 数据缓存
- **条件变量** (Condition Variable): 线程间事件通知
  - 消息到达通知
  - 队列非空/非满通知
- **事件标志** (Event Flag): 控制流程
  - 刷写完成标志
  - 会话建立标志

### 4.3 并发安全考量

| 资源 | 保护机制 | 说明 | 锁顺序 |
|------|--------|------|--------|
| 会话表 | 互斥锁 | 读写都需要获取锁 | 1 (最高优先级) |
| 消息队列 | 互斥锁 + 条件变量 | 队列操作和等待 | 2 |
| 数据缓存 | 读写锁 | 多读单写 | 3 |
| Flash 模型 | 互斥锁 | 同一时刻仅一个写操作 | 4 (最低优先级) |

**死锁预防**：
- 严格遵循锁顺序 (Session → Queue → Cache → Flash)
- 避免长时间持锁，立即释放
- 使用带超时的锁操作
- 定期检测死锁 (watchdog timer)

---

## 5. 错误处理和异常恢复

### 5.1 错误分层与 NRC 码映射

**UDS 负响应码 (NRC - Negative Response Code)** 的分层使用：

```
应用层错误                      物理层错误
  ↓                               ↓
诊断服务层 NRC                    ISO-TP 重试
  0x24 RequestSequenceError      (最多3次)
  0x31 RequestOutOfRange           ↓
  0x33 SecurityAccessDenied     CAN 总线恢复
  ...                             ↓
↓                              日志记录 + 告警
通信层负响应
  0x22 ConditionsNotCorrect
```

### 5.2 关键错误恢复策略

| 错误类型 | 触发条件 | 恢复策略 | 用户通知 |
|---------|--------|--------|--------|
| CAN 帧丢失 | 序号不连续/超时 | 自动重试3次，失败则返回NRC 0x72 | 诊断失败 |
| 会话超时 | Tester Present 间隔超过阈值 | 自动断开会话，清理资源 | 会话已断开 |
| 刷写失败 | Flash 编程失败/校验错误 | 保留原数据，报告错误位置 | 刷写失败，保留原固件 |
| 安全锁定 | 解锁尝试失败次数≥3 | 锁定5分钟，记录审计日志 | 请稍后再试 |
| 内存溢出 | 缓冲区剩余空间不足 | 返回NRC 0x72，断开会话 | 请求过大 |
| 总线故障 | CAN 错误帧/总线关闭 | 等待5秒后自动重连，最多3次 | 总线故障，已重连 |

### 5.3 异常日志与诊断

**关键异常自动生成诊断包** (便于问题定位)：

```json
{
  "timestamp": "2026-01-03T10:30:45Z",
  "exception_type": "CANTimeout",
  "severity": "ERROR",
  "context": {
    "session_id": "sess_0001",
    "ecu_id": "0x01",
    "service_id": "0x22",
    "can_id": "0x7DF",
    "expected_response_id": "0x7DE",
    "timeout_ms": 1000,
    "last_frame_time": "2026-01-03T10:30:44Z",
    "elapsed_ms": 1523
  },
  "recovery_action": "RetryCount=2, SessionClosed",
  "affected_services": ["ReadData", "FlashTransfer"]
}
```

---

## 6. 架构演进与升级路径 (Evolution & Upgrade Roadmap)

### 6.1 版本兼容性

**向后兼容性保证**：
- 核心 UDS 服务接口保持稳定 (v1.x 内)
- 新增服务通过扩展点添加，不影响已有服务
- 旧版本配置可直接用于新版本 (含自动迁移脚本)

**升级检查清单**：
- [ ] 新服务是否通过扩展点注册
- [ ] 是否修改了现有接口 (如是则需大版本更新)
- [ ] 是否修改了消息格式 (如是则需增加版本协商)
- [ ] 是否修改了配置文件结构 (如是则需提供迁移脚本)

### 6.2 功能升级路线 (参考 Development_Roadmap.md)

**Phase 1 (v1.0)** - 基础功能：
- ✓ 16 个核心 UDS 服务
- ✓ 单 ECU 仿真
- ✓ 基础安全访问
- ✓ 诊断仪 + 刷写工具

**Phase 2 (v1.1)** - 增强可靠性：
- ☐ 高级流控管理 (CAN FD 支持)
- ☐ 增强型安全访问 (SHA-256 算法)
- ☐ 诊断数据记录与回放
- ☐ 性能基准测试套件

**Phase 3 (v2.0)** - 多 ECU 与网关：
- ☐ 多 ECU 网络支持
- ☐ ECU 间通信与 Gateway 功能
- ☐ DoIP 以太网诊断适配
- ☐ OBD-II 排放诊断集成

**Phase 4 (v2.1+)** - 工业级特性：
- ☐ 功能安全 ISO 26262 认证路径
- ☐ 加密加速 (HSM 集成)
- ☐ 容器编排 (Kubernetes) 支持
- ☐ 云端诊断数据同步

---

## 7. 安全与隐私考虑 (Security & Privacy)

### 7.1 安全设计原则

1. **最小权限原则**：每个会话仅获得完成任务所需的最小权限
2. **深度防御**：多层安全机制 (会话级、服务级、数据级)
3. **审计追溯**：所有敏感操作记录审计日志
4. **失败安全**：异常情况下拒绝访问，不是允许访问

### 7.2 威胁模型与对策

| 威胁 | 风险 | 对策 |
|-----|------|------|
| 诊断消息伪造 | 未授权修改 ECU 参数 | 会话认证 + 安全访问验证 |
| 中间人攻击 | 诊断报文被篡改 | CRC 校验 + 可选 SecOC |
| 暴力破解解锁 | 获取编程权限 | 失败锁定 + 审计日志 |
| 回放攻击 | 重复刷写旧固件 | 版本检查 + 时间戳验证 |
| 侧信道攻击 | 推断密钥信息 | 常时间算法实现 (未来) |

---

## 8. 测试架构映射 (Testing Architecture)

**架构与测试策略的映射关系**：

```
单元测试 (Unit Test)
  ├─ 模块边界清晰 → 易于 Mock 依赖
  ├─ 接口定义明确 → 易于编写测试用例
  └─ 数据流清晰 → 易于验证中间结果
         ↓
集成测试 (Integration Test)
  ├─ 层间接口定义明确 → 易于集成测试
  ├─ 错误码体系统一 → 易于错误验证
  └─ 并发机制清晰 → 易于并发测试
         ↓
系统测试 (System Test)
  ├─ 完整的数据流 → 支持端到端测试
  ├─ 可配置的部署 → 支持多场景测试
  └─ 性能计数器 → 支持性能基准测试
         ↓
验收测试 (UAT)
  ├─ 清晰的需求映射 → 易于需求验证
  ├─ 关键路径识别 → 易于优先级排序
  └─ 可观测性完善 → 易于问题定位
```

---

## 9. 架构评审检查清单 (Architecture Review Checklist)

在实施时，架构师应验证以下问题：

### 功能性检查
- [ ] 所有 16 个核心 UDS 服务都有对应的实现路径
- [ ] 会话管理支持 4 种会话类型 (Default/Programming/Extended/Safety)
- [ ] 刷写流程完整支持 5 个阶段 (Request/Download/Transfer/Exit/Verify)
- [ ] 支持至少 4 个并发诊断会话

### 可靠性检查
- [ ] 所有外部输入都经过验证 (长度、范围、格式)
- [ ] 所有可能失败的操作都有重试或降级策略
- [ ] 超时机制覆盖所有等待点 (CAN、ISO-TP、会话)
- [ ] 异常路径都有完整的日志记录

### 性能检查
- [ ] 诊断响应延迟 < 100ms (99%ile)
- [ ] 刷写吞吐量 > 100 KB/s
- [ ] 内存占用 ≤ 10 MB (含所有缓冲)
- [ ] CPU 占用 ≤ 30% (单核，典型工作负载)

### 可扩展性检查
- [ ] 新服务可通过配置/注册添加，无需修改核心栈
- [ ] 新 ECU 类型可通过配置文件定义
- [ ] 支持多个并发 ECU 仿真
- [ ] 支持未来的通信方式扩展 (CAN FD/DoIP)

### 可维护性检查
- [ ] 所有接口都有文档和示例代码
- [ ] 错误码体系定义完整且一致
- [ ] 关键算法有注释说明意图
- [ ] 配置文件 Schema 明确定义

---

## 版本历史

| 版本 | 日期 | 变更描述 |
|------|------|--------|
| 1.0 | 2026-01-02 | 初始架构设计文档 |
| 1.1 | 2026-01-03 | 补充非功能需求映射、接口规范、关键路径、配置架构、可观测性、部署拓扑、并发安全、错误处理、演进路线图、安全考虑、测试映射、评审清单 |

