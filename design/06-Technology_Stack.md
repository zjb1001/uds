# 技术栈

## 文档信息

| 项目 | 内容 |
|------|------|
| **文档版本** | 1.0 |
| **创建日期** | 2026-01-02 |
| **最后修订** | 2026-01-02 |
| **作者** | UDS 诊断系统设计团队 |
| **状态** | 草案 |

---

## 1. 技术栈概述

### 1.1 技术选型原则

1. **标准兼容**: 符合 ISO 14229-1 和 ISO 15765-2 标准
2. **跨平台**: 优先支持 Linux,后续扩展至 Windows
3. **高性能**: 满足实时性和吞吐量需求
4. **可维护**: 代码清晰,易于扩展
5. **开源友好**: 使用开源协议,避免商业依赖

### 1.2 技术栈分层

```
┌─────────────────────────────────────────────────────────┐
│          应用层技术栈                                     │
│  C/C++ | Python                                          │
└─────────────────────────────────────────────────────────┘
┌─────────────────────────────────────────────────────────┐
│          中间层技术栈                                     │
│  ISO-TP | UDS Protocol | Serialization                  │
└─────────────────────────────────────────────────────────┘
┌─────────────────────────────────────────────────────────┐
│          系统层技术栈                                     │
│  SocketCAN | Threads | Memory Management                │
└─────────────────────────────────────────────────────────┘
┌─────────────────────────────────────────────────────────┐
│          构建和测试工具                                   │
│  CMake | GCC/Clang | pytest | valgrind                  │
└─────────────────────────────────────────────────────────┘
```

---

## 2. 编程语言

### 2.1 核心语言: C/C++

**选择理由**:
- **直接控制**: 低阶内存管理,零成本抽象
- **高性能**: 编译为本地代码,性能优秀
- **标准化**: ISO C11/C17/C++17 标准库广泛支持
- **跨平台**: Linux, Windows, 嵌入式平台广泛支持
- **兼容性**: 与现有诊断工具和测试框架集成度高
- **生态成熟**: CMake、gcc/clang、valgrind等工具链成熟

**适用模块**:
- UDS 诊断服务核心
- ISO-TP 传输层
- Bootloader 和 Flash 模拟
- SocketCAN 封装
- 多线程并发处理

**代码示例**:
```c
#include <stdint.h>
#include <string.h>

// UDS 服务处理函数
typedef struct {
    uint8_t service_id;
    uint16_t data_len;
    uint8_t data[4096];
} UdsRequest;

typedef struct {
    uint8_t service_id;
    uint16_t data_len;
    uint8_t data[4096];
} UdsResponse;

UdsResponse handle_uds_request(const UdsRequest *request) {
    UdsResponse response = {0};
    
    switch (request->service_id) {
        case 0x10:  // DiagnosticSessionControl
            handle_session_control(request, &response);
            break;
        case 0x22:  // ReadDataByIdentifier
            handle_read_data_identifier(request, &response);
            break;
        case 0x27:  // SecurityAccess
            handle_security_access(request, &response);
            break;
        default:
            response.service_id = 0x7F;  // NRC
            response.data[0] = 0x11;      // Service Not Supported
            response.data_len = 2;
            break;
    }
    
    return response;
}

// ISO-TP 多帧处理
typedef enum {
    SINGLE_FRAME = 0,
    FIRST_FRAME = 1,
    CONSECUTIVE_FRAME = 2,
    FLOW_CONTROL = 3
} FrameType;

typedef struct {
    FrameType type;
    uint16_t data_len;
    uint8_t data[64];
} CanFrame;

typedef struct {
    uint8_t state;
    uint8_t buffer[4096];
    uint16_t buffer_len;
    uint8_t block_size;
    uint8_t st_min;
    uint8_t seq_counter;
} IsoTpSession;

int iso_tp_process_frame(IsoTpSession *session, const CanFrame *frame) {
    switch (frame->type) {
        case SINGLE_FRAME:
            return handle_single_frame(session, frame);
        case FIRST_FRAME:
            return handle_first_frame(session, frame);
        case CONSECUTIVE_FRAME:
            return handle_consecutive_frame(session, frame);
        case FLOW_CONTROL:
            return handle_flow_control(session, frame);
        default:
            return -1;
    }
}
```

**编译工具链**:
```cmake
# CMakeLists.txt
cmake_minimum_required(VERSION 3.10)
project(uds-diagnostic-system C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -fstack-protector-all")

# 核心库
add_library(uds-core
    src/core/session.c
    src/core/services.c
    src/core/security.c
    src/core/data.c
    src/core/error.c
)

# ISO-TP库
add_library(uds-tp
    src/tp/frame.c
    src/tp/encode.c
    src/tp/decode.c
    src/tp/flow_control.c
)

# CAN库
add_library(uds-can
    src/can/socket.c
    src/can/filter.c
    src/can/router.c
)

# Bootloader库
add_library(uds-bootloader
    src/bootloader/bootloader.c
    src/bootloader/flash.c
    src/bootloader/parser.c
    src/bootloader/verifier.c
)

# ECU模拟器库
add_library(uds-ecusim
    src/ecusim/ecu.c
    src/ecusim/memory.c
    src/ecusim/dtc.c
    src/ecusim/config.c
)

# 链接选项
target_link_libraries(uds-core PRIVATE pthread)
target_link_libraries(uds-tp PRIVATE uds-core)
target_link_libraries(uds-can PRIVATE uds-tp)

# 诊断仪可执行文件
add_executable(uds-client tools/cli/main.c)
target_link_libraries(uds-client PRIVATE uds-can uds-bootloader)

# 测试可执行文件
enable_testing()
add_executable(test-core tests/test_core.c)
target_link_libraries(test-core PRIVATE uds-core)
add_test(NAME CoreTest COMMAND test-core)
```

**关键头文件和库**:
- `socketcan.h` / `linux/can.h` - SocketCAN接口
- `pthread.h` - 多线程支持
- `json-c` - JSON配置解析 (可选)
- `openssl/evp.h` - 加密算法 (安全访问)
- `check.h` - 单元测试框架

**选择理由**:
- **快速开发**: 语法简洁,开发效率高
- **生态丰富**: 大量测试和脚本库
- **易于测试**: pytest 框架强大
- **跨平台**: 良好的 Windows/Linux 支持

**适用模块**:
- 诊断仪客户端工具
- 测试脚本和自动化
- 数据分析和可视化
- 配置文件生成

**代码示例**:
```python
#!/usr/bin/env python3

import can
from uds.uds_client import UdsClient

# 连接 CAN 总线
bus = can.interface.Bus(channel='vcan0', bustype='socketcan')

# 创建 UDS 客户端
client = UdsClient(bus, arb_id=0x7E0, resp_id=0x7E8)

# 读取 DID
vin = client.read_data_identifier(0xF190)
print(f"VIN: {vin}")

# 切换会话
client.change_session(0x02)  # 进入编程会话

# 安全解锁
seed = client.request_seed(0x03)
key = generate_key(seed)
client.send_key(0x03, key)
```

**关键依赖库**:
```python
# requirements.txt
python-can==4.0.0      # CAN 接口 (FR-TRANS-001, 虚拟CAN支持)
pytest==7.4.0          # 测试框架 (09-Testing_Strategy)
pytest-cov==4.1.0      # 代码覆盖率 (目标 ≥80%, 见09章)
click==8.1.0           # CLI 框架 (诊断仪客户端)
rich==13.5.0           # 终端美化 (用户体验)
pyserial==3.5          # 串口通信 (可选硬件接入)
cryptography==41.0.0   # 加密库 (FR-SEC-001: 密钥生成算法)
pydantic==2.0.0        # 配置验证 (ECU配置模式检查)
```

**Python依赖与需求的映射**:
| 依赖 | 版本 | 关联需求 | 功能 |
|------|------|---------|------|
| python-can | 4.0+ | FR-TRANS-001/002 | SocketCAN接口,虚拟CAN (vcan0) 支持 |
| pytest | 7.4+ | 09-Testing_Strategy | 单元/集成/E2E测试框架 |
| cryptography | 41+ | FR-SEC-001 | HMAC-SHA256等安全访问密钥算法 |
| pydantic | 2.0+ | FR-TRANS-002 | ECU配置JSON模式验证,防止配置错误 |

---

## 3. 通信技术

### 3.1 SocketCAN

**版本**: Linux Kernel 2.6.25+

**功能**:
- CAN 2.0A/B 支持
- CAN FD 支持 (可选)
- 虚拟 CAN 接口 (vCAN)
- 消息过滤

**配置工具**:
- `can-utils`: canconfig, candump, cansend, cangen
- `iproute2`: ip link set

---

### 3.2 ISO-TP 实现

**方案**: 自行实现 (C) + 参考标准库支持

**原因**:
- 现有库功能有限
- 需要深度集成 UDS 协议
- 便于调试和优化
- 支持多ECU隔离的传输上下文管理

**关键特性**:
- 支持单帧、首帧、连续帧、流控帧 (ISO 15765-2, 见03-UDS_Protocol_Design)
- 可配置 BS (BlockSize) 和 STmin (SeparationTime) (FR-TRANS-003)
- 多帧分割和重组,支持 CAN 2.0A/B 标准帧
- **CAN FD 支持 (可选)**: 通过编译选项启用,支持长帧 (64字节) 提升刷写速率
  ```cmake
  option(ENABLE_CAN_FD "Enable CAN FD support" OFF)
  if(ENABLE_CAN_FD)
      add_definitions(-DCAN_FD_ENABLED)
  endif()
  ```
- 超时和错误处理,支持 P2/P2* 诊断时序 (FR-TRANS-003)
- 多ECU会话隔离: 每个ECU维护独立的ISO-TP发送/接收窗口

---

## 4. 数据序列化

### 4.1 JSON

**用途**:
- 配置文件 (ECU 配置, DID 定义, DTC 列表)
- 测试用例定义
- 日志格式

**库**:
- Rust: serde_json
- Python: json (标准库)

**示例**:
```json
{
  "ecu": {
    "id": 1,
    "name": "Engine_ECU",
    "request_can_id": "0x7E0",
    "response_can_id": "0x7E8",
    "data_identifiers": [
      {
        "did": "0xF190",
        "name": "VIN",
        "length": 17,
        "read_only": true
      }
    ]
  }
}
```

---

### 4.2 二进制格式

**用途**:
- Flash 固件数据
- 高性能数据存储

**支持格式**:
- Intel HEX (.hex)
- Motorola S-Record (.srec)
- Raw Binary (.bin)

---

## 5. 并发和异步

### 5.1 并发模型

**POSIX 线程**:
```c
#include <pthread.h>
#include <unistd.h>

// 接收线程
void* can_receive_thread(void *arg) {
    CanQueue *queue = (CanQueue *)arg;
    while (1) {
        CanFrame frame;
        if (can_recv(&frame) == 0) {
            queue_push(queue, &frame);
        }
        usleep(1000);  // 1ms 采样周期
    }
    return NULL;
}

// 处理线程
void* process_thread(void *arg) {
    CanQueue *queue = (CanQueue *)arg;
    while (1) {
        CanFrame frame;
        if (queue_pop(queue, &frame) == 0) {
            process_frame(&frame);
        }
    }
    return NULL;
}

// 线程启动
int main() {
    pthread_t rx_tid, proc_tid;
    CanQueue queue;
    queue_init(&queue);
    
    pthread_create(&rx_tid, NULL, can_receive_thread, &queue);
    pthread_create(&proc_tid, NULL, process_thread, &queue);
    
    pthread_join(rx_tid, NULL);
    pthread_join(proc_tid, NULL);
}
```

### 5.2 线程间通信

**消息队列**:
```c
#include <pthread.h>
#include <string.h>

typedef struct {
    uint8_t data[256];
    uint16_t len;
} Message;

typedef struct {
    pthread_mutex_t lock;
    pthread_cond_t cond;
    Message messages[100];
    int head, tail;
} MessageQueue;

int msg_queue_send(MessageQueue *q, const Message *msg) {
    pthread_mutex_lock(&q->lock);
    if ((q->head + 1) % 100 == q->tail) {
        pthread_mutex_unlock(&q->lock);
        return -1;  // 队列满
    }
    memcpy(&q->messages[q->head], msg, sizeof(Message));
    q->head = (q->head + 1) % 100;
    pthread_cond_signal(&q->cond);
    pthread_mutex_unlock(&q->lock);
    return 0;
}

int msg_queue_recv(MessageQueue *q, Message *msg) {
    pthread_mutex_lock(&q->lock);
    while (q->head == q->tail) {
        pthread_cond_wait(&q->cond, &q->lock);
    }
    memcpy(msg, &q->messages[q->tail], sizeof(Message));
    q->tail = (q->tail + 1) % 100;
    pthread_mutex_unlock(&q->lock);
    return 0;
}
```

**共享状态 (互斥锁)**:
```c
#include <pthread.h>

typedef struct {
    pthread_mutex_t lock;
    uint8_t session_type;
    uint8_t security_level;
} SessionState;

int session_state_init(SessionState *state) {
    pthread_mutex_init(&state->lock, NULL);
    state->session_type = 0x01;  // Default
    state->security_level = 0;
    return 0;
}

void session_state_set_security(SessionState *state, uint8_t level) {
    pthread_mutex_lock(&state->lock);
    state->security_level = level;
    pthread_mutex_unlock(&state->lock);
}
```

---

## 6. 构建系统

### 6.1 C/C++ 构建: CMake

**CMakeLists.txt (根构建文件)**:
```cmake
cmake_minimum_required(VERSION 3.10)
project(uds-diagnostic-system C CXX)

set(PROJECT_VERSION "0.1.0")
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 编译选项
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Werror=format-security")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fstack-protector-strong -D_FORTIFY_SOURCE=2")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra")

# 可选特性开关
option(ENABLE_CAN_FD "Enable CAN FD support" OFF)
option(BUILD_TESTS "Build test executables" ON)
option(ENABLE_SHARED_LIBS "Build shared libraries" OFF)

if(ENABLE_CAN_FD)
    add_definitions(-DCAN_FD_ENABLED)
endif()

# 包查找
find_package(Threads REQUIRED)

# 可选库
find_package(OpenSSL)  # 加密库
find_package(json-c)   # JSON解析

# 子目录
add_subdirectory(src/core)
add_subdirectory(src/tp)
add_subdirectory(src/can)
add_subdirectory(src/bootloader)
add_subdirectory(src/ecusim)
add_subdirectory(tools/cli)

# 测试
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# 安装配置
install(DIRECTORY include/ DESTINATION include)
install(FILES LICENSE README.md DESTINATION .)
```

**版本管理策略**:
- **主版本 (Major)**: 不兼容API变更
- **次版本 (Minor)**: 新功能兼容性
- **补丁版本 (Patch)**: 修复错误
- **VERSION 文件**: 在顶级 CMakeLists.txt 中定义统一版本号

**构建命令**:
```bash
# 创建构建目录
mkdir -p build && cd build

# 开发构建 (调试信息)
cmake -DCMAKE_BUILD_TYPE=Debug ..
make -j$(nproc)

# 发布构建 (优化)
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)

# 运行测试
ctest --verbose

# 启用CAN FD
cmake -DENABLE_CAN_FD=ON ..

# 检查代码格式 (使用外部工具)
clang-format -i src/**/*.c src/**/*.h
```

---

### 6.2 Python 构建: setuptools

**setup.py**:
```python
from setuptools import setup, find_packages

setup(
    name="uds-client",
    version="0.1.0",
    packages=find_packages(),
    python_requires=">=3.10",  # 对齐09-Testing_Strategy
    install_requires=[
        "python-can>=4.0.0",
        "click>=8.1.0",
        "pytest>=7.4.0",
        "cryptography>=41.0.0",
        "pydantic>=2.0.0",
    ],
    extras_require={
        "dev": ["pytest-cov>=4.1.0", "mypy>=1.0"],
        "perf": ["py-spy>=0.3.14"],  # 性能分析工具
    },
    entry_points={
        "console_scripts": [
            "uds-client=uds.client:main",
        ],
    },
)
```

**构建命令**:
```bash
# 安装
pip install -e .

# 运行测试
pytest

# 检查代码风格
flake8 uds/
```

---

## 7. 测试框架

### 7.1 C 测试框架 (Check Framework)

**单元测试** (使用Check库):
```c
#include <check.h>
#include <stdlib.h>
#include "../src/core/session.h"

START_TEST(test_session_control)
{
    SessionState session;
    session_state_init(&session);
    
    ck_assert_int_eq(session.session_type, 0x01);  // Default
}
END_TEST

START_TEST(test_did_read)
{
    uint8_t did_value[17];
    int ret = did_read(0xF190, did_value, sizeof(did_value));
    
    ck_assert_int_eq(ret, 17);
    ck_assert_mem_ne(did_value, NULL, 17);
}
END_TEST
```

**集成测试** (使用CMake):
```cmake
# tests/CMakeLists.txt
find_package(Check REQUIRED)
add_executable(test_core test_core.c)
target_link_libraries(test_core PRIVATE uds_core Check::check pthread m)
add_test(NAME CoreTest COMMAND test_core)
```

---

### 7.2 Python 测试### 7.2 Python 测试

**pytest**:
```python
# test_uds_client.py
import pytest
from uds.uds_client import UdsClient

def test_read_vin():
    client = UdsClient(arb_id=0x7E0)
    vin = client.read_data_identifier(0xF190)
    assert len(vin) == 17

def test_session_switch():
    client = UdsClient(arb_id=0x7E0)
    resp = client.change_session(0x02)
    assert resp.service_id == 0x50

@pytest.fixture
def can_bus():
    import can
    bus = can.interface.Bus(channel='vcan0', bustype='socketcan')
    yield bus
    bus.shutdown()
```

---

## 8. 调试和分析工具

### 8.1 CAN 总线调试

**can-utils**:
```bash
# 监听 CAN 报文
candump vcan0

# 发送 CAN 报文
cansend vcan0 123#DEADBEEF

# 生成 CAN 报文
cangen vcan0 -v -e

# 绘制 CAN 流量
canplayer -I candump.log
```

**图形化工具**:
- **Cantact**: CAN 总线分析工具
- **BusMaster**: Windows CAN 工具 (WSL2)

---

### 8.2 性能分析

**Valgrind** (C/C++):
```bash
valgrind --leak-check=full ./uds-core
valgrind --tool=callgrind ./uds-core
```

**Flamegraph** (Rust):
```bash
cargo install flamegraph
cargo flamegraph
```

**Perf** (Linux):
```bash
perf record -g ./uds-core
perf report
```

---

### 8.3 日志和调试

**Rust 日志**:
```rust
use log::{info, warn, error};

info!("Starting UDS diagnostic system");
warn!("Security access attempt from {}", addr);
error!("Flash verification failed: {}", err);
```

**Python 日志**:
```python
import logging

logging.basicConfig(level=logging.INFO)
logger = logging.getLogger(__name__)

logger.info("Starting UDS client")
logger.error("Connection failed: %s", err)
```

---

## 8.1 Rust FFI 和 C 集成工具

**可选工具链** (仅在使用C/C++优化时需要):

```bash
# 依赖安装
cargo install bindgen-cli  # C头文件自动绑定
apt-get install libclang-dev  # bindgen 依赖

# 编译和验证
cargo build --features c-flash  # 启用C Flash模拟
cargo test --features c-flash   # C代码单元测试

# 内存检查
valgrind --leak-check=full ./target/debug/uds-core
```

**FFI最佳实践** (在08-Interface_and_Integration_Design中详述):
- 使用 `bindgen` 自动生成类型安全的Rust绑定
- 将C代码隔离在独立crate (uds-flash-c)
- 通过特性开关(feature flag)控制C编译,默认禁用(纯Rust)
- 对所有FFI调用进行内存安全检查

---

## 9. 文档生成

### 9.1 C API 文档

**Doxygen**:
```bash
doxygen Doxyfile
# HTML 输出在 docs/html/index.html
```

### 9.2### 9.2 Markdown 文档

**工具**: MkDocs, Docusaurus, Hugo

**内容**:
- API 文档
- 用户手册
- 开发指南
- 故障排查

---

## 10. 版本控制和 CI/CD

### 10.1 Git 工作流

**分支策略**:
```
main (生产)
  ↑
develop (开发)
  ↑
feature/* (功能分支)
hotfix/* (热修复)
```

**提交规范**:
```
feat: 添加 ISO-TP 多帧处理
fix: 修复安全访问密钥验证问题
docs: 更新 API 文档
test: 添加 Flash 编程单元测试
```

---

### 10.2 持续集成

**GitHub Actions**:
```yaml
name: CI

on: [push, pull_request]

jobs:
  test:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v2
      - name: Install Rust
        uses: actions-rs/toolchain@v1
        with:
          toolchain: stable
      - name: Run tests
        run: cargo test --verbose
      - name: Run clippy
        run: cargo clippy -- -D warnings
```

---

## 10.3 兼容性与安全基线

- **运行时矩阵**: Rust 1.75+; Python 3.10-3.12; Linux Kernel ≥ 5.4 (含 SocketCAN/vCAN) (对齐10-Development_Roadmap的M1基础框架阶段)。
- **多ECU支持**: 通过Tokio异步运行时支持并发ECU会话 (FR-TRANS-004),每个ECU隔离会话/安全状态/刷写上下文。
- **可选容器化**: 提供基于 Debian/Ubuntu 的 Dockerfile，预装 `can-utils`、Rust 工具链、Python 依赖，用于 CI 与演示环境统一。支持 WSL2 (Windows 开发环境)。
- **依赖安全**: 
  - Rust: 启用 `cargo audit` 检查已知漏洞,禁用不安全代码块 (unsafe 需文档说明)
  - Python: 使用 `pip-audit` 与 `--require-hashes` (生产环境),禁用MD5用于安全访问 (使用SHA-256, 见FR-SEC-001)
  - C/C++: 启用编译器安全选项 (`-fstack-protector-all -Wl,-z,relro,-z,now`)
- **性能工具**: 
  - Rust: `criterion` 基准测试 (目标 <100ms 响应延迟,见01-SRS),`tokio-console` 并发任务监控
  - C/C++: 可选 `valgrind` 内存检查,`perf` 性能分析
  - Python: `py-spy` 采样分析
- **特性开关** (Cargo features):
  ```toml
  [features]
  default = []              # 默认配置 (纯Rust,无C依赖)
  can-fd = []              # 可选: CAN FD支持 (64字节帧,刷写优化)
  async-tokio = []         # 可选: Tokio异步运行时 (默认std::thread)
  crypto-hmac = []         # 可选: HMAC-SHA256加密算法
  c-flash = []             # 可选: C实现Flash模拟 (性能优化,仅Phase 4+)
  ```
  通过减少特性组合,降低编译时间和二进制膨胀。

---

## 11. 多ECU架构支持 (与02-System_Architecture_Design和07-Project_Structure对齐)

### 11.1 并发ECU管理

为支持多ECU隔离与并发 (FR-TRANS-004), 技术栈提供以下构件:

**全局ECU注册表**:
```rust
// uds-core/src/ecu_registry.rs
use once_cell::sync::Lazy;
use parking_lot::RwLock;
use std::collections::HashMap;

pub static ECU_REGISTRY: Lazy<RwLock<HashMap<u8, Arc<EcuContext>>>> = 
    Lazy::new(|| RwLock::new(HashMap::new()));

pub struct EcuContext {
    pub id: u8,
    pub session: Mutex<DiagnosticSession>,      // 会话隔离
    pub security: Mutex<SecurityContext>,       // 安全状态隔离
    pub flash_context: Mutex<FlashingContext>,  // 刷写上下文隔离
    pub iso_tp_state: Mutex<IsoTpState>,        // ISO-TP窗口隔离
}
```

**Tokio异步并发处理**:
```rust
// 处理多个ECU的诊断请求 (无阻塞)
#[tokio::main]
async fn main() {
    let (tx, mut rx) = tokio::sync::mpsc::channel(100);

    // 为每个ECU创建独立任务
    for ecu_id in 0..4 {
        let tx = tx.clone();
        tokio::spawn(async move {
            handle_ecu_session(ecu_id, tx).await
        });
    }

    // 处理消息队列
    while let Some(msg) = rx.recv().await {
        process_can_message(msg).await;
    }
}
```

### 11.2 项目结构与Workspace组织

与07-Project_Structure对齐, Cargo Workspace 组织如下:

```
crates/
├── uds-core/          # 核心诊断服务 (通用)
│   └── src/
│       ├── lib.rs
│       ├── ecu_registry.rs      # ECU注册表 (新增)
│       ├── services/
│       │   ├── session.rs       # 会话管理 (支持多ECU隔离)
│       │   ├── security.rs      # 安全访问 (per-ECU)
│       │   └── data.rs          # 数据读写 (per-ECU)
│       └── error.rs
│
├── uds-tp/            # ISO-TP传输层 (通用)
│   └── src/
│       ├── lib.rs
│       ├── session.rs           # 独立传输会话 (支持per-ECU隔离)
│       └── flow_control.rs
│
├── uds-can/           # CAN驱动 (通用)
│   └── src/
│       ├── lib.rs
│       ├── router.rs            # 多ECU路由 (新增, FR-TRANS-002)
│       └── filter.rs
│
├── uds-bootloader/    # Bootloader (通用)
│   └── src/
│       ├── lib.rs
│       └── flash.rs             # per-ECU Flash隔离
│
└── uds-client/        # 诊断仪客户端
    └── src/
        ├── lib.rs
        └── cli.rs               # CLI工具
```

**Workspace 依赖统一管理**:
- 所有crate共享同一组依赖版本 (通过 `[workspace.dependencies]`)
- 统一的Rust版本要求 (1.75+)
- 所有crate同步升版本号

### 11.3 ECU配置与路由 (与08-Interface_and_Integration_Design对齐)

支持JSON配置文件定义多ECU,示例:

```json
{
  "ecus": [
    {
      "id": 0,
      "name": "Engine_ECU",
      "request_can_id": "0x600",
      "response_can_id": "0x680",
      "session_timeout_s": 600
    },
    {
      "id": 1,
      "name": "Transmission_ECU",
      "request_can_id": "0x601",
      "response_can_id": "0x681",
      "session_timeout_s": 600
    }
  ],
  "iso_tp": {
    "bs": 0,       # BlockSize (0 = 无限制)
    "stmin": 5     # Separation Time (ms)
  }
}
```

**配置加载与验证** (使用pydantic, Python侧; serde_json + validator, Rust侧):
```python
# Python客户端
from pydantic import BaseModel

class EcuConfig(BaseModel):
    id: int
    name: str
    request_can_id: str
    response_can_id: str
    session_timeout_s: int

class SystemConfig(BaseModel):
    ecus: List[EcuConfig]
    iso_tp: dict

# 加载并验证
config = SystemConfig.parse_file("ecu_config.json")
```

---

## 12. 许可证

**推荐许可证**:
- **主项目**: MIT License 或 Apache 2.0
- **文档**: CC BY 4.0

**理由**:
- 宽松,允许商业使用
- 保护作者权益
- 社区友好

## 版本历史

| 版本 | 日期 | 变更描述 |
|------|------|--------|
| 1.1 | 2026-01-03 | 完善多ECU支持、FFI工具链、版本管理、需求对齐 |
| 1.0 | 2026-01-02 | 技术栈初始版本 |

---

## 一致性对齐检查清单

本章与系统其他文档的对齐情况:

| 对齐项目 | 关联文档 | 一致性状态 | 备注 |
|---------|--------|----------|------|
| 功能需求映射 | 01-SRS | ✅ 完善 | 所有关键依赖库标记对应的FR需求 |
| 架构分层 | 02-SAD | ✅ 完善 | 添加多ECU隔离方案,支持04章刷写和05章CAN路由 |
| ISO-TP参数 | 03-UDS协议 | ✅ 完善 | 明确支持CAN FD、流控参数可配置 |
| 刷写流程 | 04-Bootloader | ✅ 完善 | C优化作为Phase 4+ 可选项 |
| 虚拟CAN平台 | 05-vCAN设计 | ✅ 完善 | SocketCAN 2.0+支持,vCAN虚拟接口 |
| 项目结构 | 07-项目结构 | ✅ 完善 | Workspace组织、crate清单与07章同步 |
| 接口定义 | 08-接口设计 | ✅ 完善 | FFI工具链、版本管理、配置模式 |
| 测试工具 | 09-测试策略 | ✅ 完善 | pytest、criterion、性能基准工具 |
| 开发路线图 | 10-开发计划 | ✅ 完善 | Phase划分、Rust 1.75+、纯Rust初期 |

---

- [The Rust Programming Language](https://www.rust-lang.org/)
- [Python Official Documentation](https://docs.python.org/)
- [SocketCAN - Linux Kernel](https://www.kernel.org/doc/Documentation/networking/can.txt)
- [ISO 14229-1:2020](https://www.iso.org/standard/72439.html)
