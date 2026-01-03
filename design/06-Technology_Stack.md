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
│  Rust | Python | C/C++                                   │
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
│  Cargo | CMake | pytest | valgrind                      │
└─────────────────────────────────────────────────────────┘
```

---

## 2. 编程语言

### 2.1 核心语言: Rust

**选择理由**:
- **内存安全**: 编译时保证内存安全,避免缓冲区溢出、空指针等
- **高性能**: 零成本抽象,性能接近 C/C++
- **并发支持**: 内置所有权系统,安全并发
- **类型系统**: 强类型,枚举、模式匹配等减少错误
- **包管理**: Cargo 提供完善的依赖管理和构建系统

**适用模块**:
- UDS 诊断服务核心
- ISO-TP 传输层
- Bootloader 和 Flash 模拟
- SocketCAN 封装
- 多线程并发处理

**代码示例**:
```rust
use std::net::UdpSocket;

// UDS 服务处理函数
fn handle_uds_request(request: &UdsRequest) -> UdsResponse {
    match request.service_id {
        0x10 => handle_session_control(request),
        0x22 => handle_read_data_identifier(request),
        0x27 => handle_security_access(request),
        _ => UdsResponse::negative_response(0x11),
    }
}

// ISO-TP 多帧处理
struct IsoTpSession {
    state: SessionState,
    buffer: Vec<u8>,
    block_size: u8,
    st_min: u8,
}

impl IsoTpSession {
    fn process_frame(&mut self, frame: &CanFrame) -> Result<Vec<u8>, IsoTpError> {
        match frame.pci() {
            FrameType::Single => self.handle_single_frame(frame),
            FrameType::First => self.handle_first_frame(frame),
            FrameType::Consecutive => self.handle_consecutive_frame(frame),
            FrameType::FlowControl => self.handle_flow_control(frame),
        }
    }
}
```

**关键依赖库**:
```toml
[dependencies]
socketcan = "2.0"           # SocketCAN 绑定 (FR-TRANS-001: ISO-TP)
tokio = { version = "1.0", features = ["full"] }  # 异步运行时 (多ECU并发, FR-TRANS-004)
serde = { version = "1.0", features = ["derive"] }  # 序列化
serde_json = "1.0"         # JSON 配置加载 (ECU配置路由, FR-TRANS-002)
hex = "0.4"                # 十六进制编码 (数据处理)
log = "0.4"                # 日志接口 (诊断可观测性)
env_logger = "0.10"        # 日志实现
thiserror = "1.0"          # 错误处理 (ISO 14229-1 NRC支持)
anyhow = "1.0"             # 错误上下文
once_cell = "1.19"         # 全局状态管理 (ECU隔离, FR-TRANS-004)
parking_lot = "0.12"       # 高效互斥锁 (多ECU并发性能)
```

**依赖库与需求规范的映射**:
| 依赖库 | 版本 | 关联需求 | 功能说明 |
|--------|------|---------|----------|
| socketcan | 2.0+ | FR-TRANS-001/002 | Linux SocketCAN 原生支持,支持vCAN虚拟接口 |
| tokio | 1.75+ | FR-TRANS-004 | 异步运行时,支持多ECU并发会话隔离 |
| serde_json | 1.0+ | FR-TRANS-002 | JSON格式ECU配置、DID定义、DTC列表 (见01-SRS) |
| once_cell | 1.19+ | FR-TRANS-004 | 全局ECU注册表,实现ECU会话状态隔离 |
| parking_lot | 0.12+ | FR-TRANS-004 | 替代std::sync::Mutex,提升多ECU高并发性能 |

---

### 2.2 辅助语言: Python

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

### 2.3 性能优化: C/C++ (可选)

**使用场景**:
- 关键路径性能优化 (Flash编程算法)
- 与现有 C 库集成
- 底层驱动开发 (可选,未来扩展)

**集成方式**:
- 使用 Rust FFI 调用 C 函数
- 使用 bindgen 自动生成绑定

**工具链配置**:
```toml
# Cargo.toml (uds-bootloader crate)
[build-dependencies]
bindgen = "0.69"  # C/C++ 绑定生成
cc = "1.0"        # C编译集成

[features]
C-flash = []      # 可选特性,启用C实现的Flash模拟
```

**build.rs 示例**:
```rust
// 可选: 编译C库
#[cfg(feature = "C-flash")]
fn compile_c_library() {
    cc::Build::new()
        .file("src/c/flash_sim.c")
        .compile("flash_sim");
}
```

**限制**: 初期(Phase 1-3)使用纯Rust实现,C优化作为Phase 4+ 的可选优化,降低初期复杂度

**示例**:
```c
// flash_sim.c - 高性能 Flash 模拟
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define FLASH_SIZE (512 * 1024)

struct FlashSimulator {
    uint8_t *memory;
    uint32_t size;
};

int flash_init(struct FlashSimulator *flash, uint32_t size) {
    flash->memory = calloc(size, sizeof(uint8_t));
    flash->size = size;
    return (flash->memory != NULL) ? 0 : -1;
}

int flash_write(struct FlashSimulator *flash, uint32_t addr,
                const uint8_t *data, uint32_t len) {
    if (addr + len > flash->size) {
        return -1;
    }
    memcpy(flash->memory + addr, data, len);
    return 0;
}
```

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

**方案**: 自行实现 (Rust) + 参考标准库支持

**原因**:
- 现有库功能有限
- 需要深度集成 UDS 协议
- 便于调试和优化
- 支持多ECU隔离的传输上下文管理

**关键特性**:
- 支持单帧、首帧、连续帧、流控帧 (ISO 15765-2, 见03-UDS_Protocol_Design)
- 可配置 BS (BlockSize) 和 STmin (SeparationTime) (FR-TRANS-003)
- 多帧分割和重组,支持 CAN 2.0A/B 标准帧
- **CAN FD 支持 (可选)**: 通过特性开关启用,支持长帧 (64字节) 提升刷写速率
  ```toml
  [features]
  can-fd = ["socketcan/can-fd"]  # 可选特性,减少依赖大小
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

**Rust 线程**:
```rust
use std::thread;

// 接收线程
let rx_thread = thread::spawn(move || {
    loop {
        let frame = can_recv(&sockfd);
        tx_queue.send(frame).unwrap();
    }
});

// 处理线程
let process_thread = thread::spawn(move || {
    loop {
        let frame = rx_queue.recv().unwrap();
        process_frame(frame);
    }
});
```

**Tokio 异步运行时** (可选):
```rust
use tokio::net::UdpSocket;

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    let socket = UdpSocket::bind("0.0.0.0:1234").await?;

    loop {
        let mut buf = [0; 1024];
        let (len, addr) = socket.recv_from(&mut buf).await?;
        process_packet(&buf[..len]).await;
    }
}
```

---

### 5.2 线程间通信

**通道 (Channel)**:
```rust
use std::sync::mpsc;

let (tx, rx) = mpsc::channel();

// 发送
tx.send(frame)?;

// 接收
let frame = rx.recv()?;
```

**共享状态**:
```rust
use std::sync::{Arc, Mutex};

let shared_state = Arc::new(Mutex::new(SessionState::new()));

// 多个线程共享
let state1 = Arc::clone(&shared_state);
let state2 = Arc::clone(&shared_state);
```

---

## 6. 构建系统

### 6.1 Rust 构建: Cargo

**Cargo.toml (根Workspace)**:
```toml
[package]
name = "uds-diagnostic-system"
version = "0.1.0"
edition = "2021"
authors = ["UDS Team"]
repository = "https://github.com/uds-team/uds-diagnostic-system"
license = "MIT OR Apache-2.0"

[workspace]
members = [
    "crates/uds-core",
    "crates/uds-tp",
    "crates/uds-can",
    "crates/uds-bootloader",
    "crates/uds-client",
]
resolver = "2"

# 工作空间级依赖管理,统一版本号
[workspace.dependencies]
tokio = { version = "1.35", features = ["full"] }  # 最小1.35支持异步yield
serde = { version = "1.0", features = ["derive"] }
serde_json = "1.0"
log = "0.4"
env_logger = "0.10"

[dependencies]
# (继承自workspace.dependencies,保持一致性)
tokio = { workspace = true }
serde = { workspace = true }
socketcan = "2.0"
once_cell = "1.19"  # 全局ECU注册表
parking_lot = "0.12"  # 高效互斥锁

[dev-dependencies]
criterion = "0.5"  # 性能基准测试 (见09-Testing_Strategy)

[features]
default = []
can-fd = []  # 可选: CAN FD支持
crypto-custom = []  # 可选: 自定义加密算法
```

**版本管理策略**:
- **主版本 (Major)**: 不兼容API变更
- **次版本 (Minor)**: 新功能兼容性
- **补丁版本 (Patch)**: 修复错误
- **Workspace 协调**: 所有crate版本号同步,发布时更新顶层版本号

**构建命令**:
```bash
# 开发构建
cargo build

# 发布构建
cargo build --release

# 运行测试
cargo test

# 运行基准测试
cargo bench

# 检查代码
cargo clippy

# 格式化代码
cargo fmt
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

### 7.1 Rust 测试

**单元测试**:
```rust
#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_session_control() {
        let session = Session::new();
        assert_eq!(session.session_type(), SessionType::Default);
    }

    #[test]
    fn test_did_read() {
        let result = read_did(0xF190);
        assert_eq!(result.len(), 17);
    }
}
```

**集成测试**:
```rust
// tests/integration_test.rs
#[test]
fn test_full_flashing_workflow() {
    // 创建虚拟 CAN 总线
    // 启动 ECU 模拟
    // 执行完整刷写流程
    // 验证结果
}
```

**性能测试**:
```rust
use criterion::{black_box, criterion_group, criterion_main, Criterion};

fn criterion_benchmark(c: &mut Criterion) {
    c.bench_function("iso_tp_encode", |b| {
        b.iter(|| {
            let data = vec![0u8; 1024];
            encode_iso_tp(black_box(&data))
        })
    });
}

criterion_group!(benches, criterion_benchmark);
criterion_main!(benches);
```

---

### 7.2 Python 测试

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

### 9.1 Rust 文档

**rustdoc**:
```bash
# 生成文档
cargo doc --open

# 包含私有项
cargo doc --document-private-items
```

**代码注释**:
```rust
/// UDS 诊断服务处理函数
///
/// # 参数
///
/// * `request` - UDS 请求报文
///
/// # 返回
///
/// UDS 响应报文
///
/// # 示例
///
/// ```
/// let resp = handle_uds_request(&req);
/// assert_eq!(resp.service_id(), 0x62);
/// ```
fn handle_uds_request(request: &UdsRequest) -> UdsResponse {
    // ...
}
```

---

### 9.2 Markdown 文档

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
