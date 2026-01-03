# 项目结构

## 文档信息

| 项目 | 内容 |
|------|------|
| **文档版本** | 1.1 |
| **创建日期** | 2026-01-02 |
| **最后修订** | 2026-01-03 |
| **作者** | UDS 诊断系统设计团队 |
| **状态** | 审查中 |

---

## 1. 项目概述

### 1.1 项目组织

```
uds-diagnostic-system/
├── design/                 # 设计文档
├── src/                    # C 源代码 (CMake 项目)
│   ├── core/              # 核心 UDS 实现
│   ├── tp/                # ISO-TP 传输层
│   ├── can/               # SocketCAN 封装 (新增, 见06章技术栈)
│   ├── bootloader/        # Bootloader 和 Flash 模拟
│   ├── ecusim/            # ECU 模拟器 (新增)
│   └── include/           # 公共头文件
├── tools/                 # Python 工具
│   ├── uds-cli/           # 命令行工具 (诊断仪)
│   └── uds-test/          # 自动化测试框架
├── tests/                 # 集成和性能测试
├── configs/               # 配置文件 (ECU/DID/DTC)
├── docs/                  # 文档 (API/用户/开发者)
├── scripts/               # 构建和部署脚本
└── firmware/              # 测试固件 (刷写测试用)
```

**对齐说明** (与06章技术栈协调):
- C 源代码采用 **CMake** 构建,版本号统一管理
- `can` 模块新增,负责SocketCAN封装和多ECU路由 (FR-TRANS-002, 见06章)
- `ecusim` 新增,支持多ECU隔离与并发 (FR-TRANS-004, 见06章)

### 1.2 目录结构详解

---

## 2. 源代码组织

### 2.1 顶层目录

```
uds-diagnostic-system/
│
├── CMakeLists.txt          # 根构建配置 (06章技术栈定义)
├── README.md               # 项目说明
├── LICENSE                 # 许可证 (MIT OR Apache-2.0)
├── .gitignore              # Git 忽略规则
│
├── design/                 # 设计文档目录 (10份设计文档)
│   ├── 00-Design_Overview.md
│   ├── 01-System_Requirements_Specification.md (SRS需求)
│   ├── 02-System_Architecture_Design.md (架构对标)
│   ├── 03-UDS_Protocol_Design.md (协议服务)
│   ├── 04-Bootloader_and_Flash_Design.md (刷写流程)
│   ├── 05-Virtual_CAN_Platform_Design.md (vCAN设计)
│   ├── 06-Technology_Stack.md (技术栈与依赖)
│   ├── 07-Project_Structure.md (本文档)
│   ├── 08-Interface_and_Integration_Design.md (接口API)
│   ├── 09-Testing_Strategy.md (测试覆盖率)
│   └── 10-Development_Roadmap.md (Phase划分)
│
├── src/                    # C 源代码 (CMake 成员, 见06章)
│   │
│   ├── core/          # 核心 UDS 实现 (见03-UDS协议)
│   │   ├── CMakeLists.txt     # (CMake成员)
│   │       ├── session.c                   # (~400行, 0x10/0x3E会话管理)
│   │       ├── services.c                  # (~600行, 0x22/0x2E/0x19/0x14)
│   │       ├── security.c                  # (~300行, 0x27安全解锁)
│   │       ├── routine.c                   # (~250行, 0x31诊断程序)
│   │       ├── data.c                      # (~350行, DID/DTC管理)
│   │       ├── ecu_registry.c              # (~200行, 多ECU注册表, 新增)
│   │       └── error.c                     # (~150行, NRC错误定义)
│   │       总计: ~2350行
│   │
│   ├── tp/            # ISO-TP 传输层 (见03-UDS协议) (见03-UDS协议)
│   │   ├── CMakeLists.txt
│   │       ├── frame.c                     # (~200行, SF/FF/CF/FC帧)
│   │       ├── encode.c                    # (~180行, 编码多帧)
│   │       ├── decode.c                    # (~220行, 解码重组)
│   │       ├── flow_control.c              # (~150行, BS/STmin参数)
│   │       ├── session.c                   # (~250行, 会话隔离)
│   │       └── error.c                     # (~100行, 传输错误)
│   │       总计: ~1180行
│   │
│   ├── can/           # SocketCAN 封装 (新增, 见06/05章)
│   │   ├── CMakeLists.txt     # (链接依赖)
│   │       ├── socket.c                    # (~200行, Socket封装API)
│   │       ├── frame.c                     # (~100行, CAN帧定义)
│   │       ├── filter.c                    # (~120行, CAN ID过滤)
│   │       ├── router.c                    # (~200行, 多ECU路由, 新增)
│   │       └── error.c                     # (~80行, CAN错误)
│   │       总计: ~760行
│   │
│   ├── bootloader/    # Bootloader 和 Flash (见04-刷写设计)
│   │   ├── CMakeLists.txt
│   │       ├── bootloader.c                # (~350行, 状态机)
│   │       ├── flash.c                     # (~400行, Flash存储模型)
│   │       ├── parser.c                    # (~300行, S-Record/HEX解析)
│   │       ├── verifier.c                  # (~200行, 校验和验证)
│   │       └── error.c                     # (~100行, 刷写错误)
│   │       总计: ~1430行
│   │
│   ├── ecusim/        # ECU 模拟器 (新增, 见02-架构设计)
│   │   ├── CMakeLists.txt
│   │       ├── ecu.c                        # (~450行, ECU对象生命周期)
│   │       ├── memory.c                    # (~250行, RAM/Flash模型)
│   │       ├── dtc.c                        # (~200行, DTC记录)
│   │       ├── config.c                    # (~150行, JSON配置加载)
│   │       └── error.c                     # (~80行, 模拟器错误)
│   │       总计: ~1210行
│   │
│   └── include/             # 公共头文件
│       ├── core.h                           # 核心UDS接口
│       ├── tp.h                             # ISO-TP接口
│       ├── can.h                            # CAN驱动接口
│       ├── bootloader.h                     # Bootloader接口
│       ├── ecusim.h                         # ECU模拟器接口
│       └── version.h                        # 版本定义
│
├── tools/                 # Python 工具
│   │
│   ├── uds-cli/           # 命令行诊断工具 (见08章接口)
│   │   ├── setup.py       # (python_requires=">=3.10", 见06章)
│   │   ├── README.md
│   │   └── uds_cli/
│   │       ├── __init__.py
│   │       ├── main.py                    # (~200行, CLI入口)
│   │       ├── commands/
│   │       │   ├── session.py             # (~150行, 会话控制)
│   │       │   ├── data.py                # (~180行, 数据读写)
│   │       │   ├── dtc.py                 # (~120行, DTC操作)
│   │       │   └── flash.py               # (~200行, 刷写工具)
│   │       └── utils/
│   │           ├── can.py                 # (~150行, CAN接口)
│   │           ├── uds.py                 # (~250行, UDS协议)
│   │           └── config.py              # (~100行, 配置加载)
│   │       总计: ~1350行
│   │
│   └── uds-test/          # 自动化测试框架
│       ├── setup.py       # (python_requires=">=3.10")
│       ├── README.md
│       └── uds_test/
│           ├── __init__.py
│           ├── runner.py                  # (~200行, 测试框架)
│           ├── cases/
│           │   ├── test_session.py        # (~250行, 会话测试)
│           │   ├── test_security.py       # (~280行, 安全访问)
│           │   ├── test_data.py           # (~230行, 数据服务)
│           │   ├── test_flash.py          # (~300行, 刷写流程)
│           │   └── test_multi_ecu.py      # (~250行, 多ECU并发)
│           ├── fixtures/
│           │   ├── conftest.py            # (~150行, pytest共享装置)
│           │   └── mock_ecu.py            # (~200行, 虚拟ECU模拟)
│           └── reports/
│               └── pytest.ini              # (覆盖率配置, ≥80%)
│           总计: ~2060行
│
├── tests/                 # 集成和性能测试
│   ├── integration/
│   │   ├── test_full_workflow.c           # (~300行, 完整诊断流程)
│   │   ├── test_multi_ecu.c               # (~250行, 多ECU并发)
│   │   ├── test_flash_recovery.c          # (~200行, 刷写恢复)
│   │   └── conftest.c                     # (~150行, 测试装置)
│   │   总计: ~900行
│   │
│   └── performance/
│       ├── bench_throughput.c             # (~200行, 吞吐量基准)
│       ├── bench_latency.c                # (~200行, 延迟基准)
│       └── bench_concurrent.c             # (~200行, 多ECU并发基准)
│       总计: ~600行
│
├── configs/               # 配置文件 (JSON格式)
│   ├── ecu/
│   │   ├── engine_ecu.json                 # (见08章接口, ~150行)
│   │   ├── transmission_ecu.json           # (~150行)
│   │   └── abs_ecu.json                    # (~150行)
│   ├── did/
│   │   ├── standard_dids.json              # (标准DID定义, ~200行)
│   │   └── custom_dids.json                # (~100行)
│   ├── dtc/
│   │   ├── powertrain_dtc.json             # (P-Code定义, ~150行)
│   │   └── chassis_dtc.json                # (~150行)
│   └── system/
│       ├── can_config.json                 # (vCAN参数, ~50行)
│       ├── security_config.json            # (安全参数, ~80行)
│       └── logging_config.json             # (日志配置, ~50行)
│       总计: ~1230行 JSON
│
├── docs/                  # 文档 (API/用户/开发者)
│   ├── api/               # API 文档 (cargo doc生成)
│   ├── user_guide/        # 用户手册
│   │   ├── installation.md
│   │   ├── quick_start.md
│   │   ├── cli_reference.md
│   │   └── troubleshooting.md
│   ├── developer_guide/   # 开发者指南
│   │   ├── architecture.md
│   │   ├── contributing.md
│   │   └── coding_standards.md
│   └── examples/          # 示例代码
│       ├── basic_diagnostic.py             # (Python示例)
│       ├── flash_firmware.py               # (刷写示例)
│       └── custom_ecu.c                   # (Rust示例)
│
├── scripts/               # 构建和部署脚本
│   ├── setup_vcan.sh      # (设置虚拟CAN, 见05章)
│   ├── build.sh           # (完整构建: Rust+Python)
│   ├── test.sh            # (单元+集成+性能测试)
│   ├── format.sh          # (代码格式化: cargo fmt + black)
│   ├── lint.sh            # (静态检查: clippy + mypy, 新增)
│   └── docker/
│       ├── Dockerfile     # (开发环境容器)
│       └── docker-compose.yml
│
└── firmware/              # 测试固件 (用于刷写测试)
    ├── test_app.hex       # Intel HEX格式
    ├── test_app.srec      # Motorola S-Record格式
    └── test_app.bin       # 原始二进制格式
```

**统计信息与交付说明**:
| 类别 | 代码量 | 备注 |
|------|--------|------|
| C源代码          | ~7000行 | uds-core/tp/can/bootloader/ecusim |
| Python工具 | ~3410行 | uds-cli/uds-test |
| 配置文件 | ~1230行JSON | 支持多ECU动态配置 |
| 集成测试 | ~1500行 | 覆盖率目标≥80%, 见09章 |
| **总代码** | **~12500行** | 对齐10章Phase规划 |

---

## 3. 核心模块文件清单

### 3.1 core

**职责**: UDS 诊断协议核心实现 (见02-System_Architecture_Design和03-UDS_Protocol_Design)

**文件清单和对齐**:
| 文件 | 行数 | 关联服务 | 对应需求 |
|------|------|--------|---------|
| module.h | ~100 | 公共导出 | - |
| session.c | ~400 | 0x10(会话控制), 0x3E(心跳) | FR-SES-001/002/003 |
| services.c | ~600 | 0x22(读DID), 0x2E(写DID), 0x19(读DTC), 0x14(清除DTC) | FR-DATA-001/002, FR-DTC-001/002 |
| security.c | ~300 | 0x27(安全解锁) | FR-SEC-001/002 |
| routine.c | ~250 | 0x31(诊断程序) | FR-ROUTINE |
| data.c | ~350 | DID/DTC/数据管理 | FR-DATA-*, FR-DTC-* |
| ecu_registry.c | ~200 | 多ECU注册表 (新增) | FR-TRANS-004 (多ECU并发) |
| error.c | ~150 | NRC错误定义 | ISO 14229-1 错误码 |

**总计**: ~2350行

**与06章技术栈对齐**: 依赖 `once_cell`(全局ECU注册) + `parking_lot`(高效并发锁)

---

### 3.2 tp

**职责**: ISO-TP 传输层实现 (见03-UDS_Protocol_Design, ISO 15765-2)

**文件清单和对齐**:
| 文件 | 行数 | 关键功能 | 对应需求 |
|------|------|--------|---------|
| module.h | ~80 | 模块导出 | - |
| frame.c | ~200 | SF/FF/CF/FC帧类型 | FR-TRANS-001 |
| encode.c | ~180 | 多帧分段编码 | FR-TRANS-001 |
| decode.c | ~220 | 多帧重组解码 | FR-TRANS-001 |
| flow_control.c | ~150 | BS/STmin流量控制 | FR-TRANS-003 |
| session.c | ~250 | 独立ISO-TP会话 | FR-TRANS-004 (per-ECU隔离) |
| error.c | ~100 | 传输层错误 | 见08章错误处理 |

**总计**: ~1180行

**与03章对齐**: 支持P2/P2*/S3诊断时序, 支持CAN FD(可选特性)

---

### 3.3 can

**职责**: SocketCAN 封装与多ECU路由 (见05-Virtual_CAN_Platform_Design, 06-Technology_Stack)

**文件清单和对齐**:
| 文件 | 行数 | 关键功能 | 对应需求 |
|------|------|--------|---------|
| module.h | ~60 | 模块导出 | - |
| socket.c | ~200 | Socket封装API, 绑定vCAN | FR-TRANS-001 |
| frame.c | ~100 | CAN帧结构(标准/扩展/FD) | 见05章 |
| filter.c | ~120 | CAN ID过滤与匹配 | FR-TRANS-002 |
| router.c | ~200 | 多ECU消息路由与转发(新增) | FR-TRANS-002/004 |
| error.c | ~80 | CAN错误定义 | 见08章错误处理 |

**总计**: ~760行

**新增router.c模块**: 支持物理/功能寻址, CAN ID路由规则(0x600+ECU_ID, 0x680+ECU_ID)

---

### 3.4 bootloader

**职责**: Bootloader 和 Flash 模拟 (见04-Bootloader_and_Flash_Design)

**文件清单和对齐**:
| 文件 | 行数 | 关键功能 | 对应需求 |
|------|------|--------|---------|
| module.h | ~80 | 模块导出 | - |
| bootloader.c | ~350 | Bootloader状态机 | FR-FLASH-001 |
| flash.c | ~400 | Flash内存模型(1MB, 多区域) | FR-FLASH-002 |
| parser.c | ~300 | S-Record/Intel HEX解析 | FR-FLASH-003 |
| verifier.c | ~200 | 刷写校验和验证 | FR-FLASH-004 |
| error.c | ~100 | 刷写错误与恢复 | 见04章NRC处理 |

**总计**: ~1430行

**与04章对齐**: 支持0x34/0x36/0x37刷写服务, 支持多区域管理和断电恢复

---

### 3.5 ecusim

**职责**: ECU 模拟器与多ECU管理 (见02-System_Architecture_Design, 06-Technology_Stack)

**文件清单和对齐**:
| 文件 | 行数 | 关键功能 | 对应需求 |
|------|------|--------|---------|
| module.h | ~80 | 模块导出 | - |
| ecu.c | ~450 | ECU对象生命周期管理 | FR-TRANS-004 |
| memory.c | ~250 | RAM/Flash内存模型 | 与uds-bootloader协调 |
| dtc.c | ~200 | DTC记录与管理 | FR-DTC-001/002 |
| config.c | ~150 | JSON配置加载(pydantic验证) | FR-TRANS-002 |
| error.c | ~80 | 模拟器错误 | - |

**总计**: ~1210行

**新增模块**: 支持多ECU并发隔离(Tokio+once_cell+parking_lot), 动态ECU注册与卸载

---

## 4. 配置文件格式

### 4.1 ECU 配置

**文件**: `configs/ecu/engine_ecu.json`

```json
{
  "ecu_id": 1,
  "name": "Engine_ECU",
  "request_can_id": "0x7E0",
  "response_can_id": "0x7E8",
  "supported_services": [
    0x10, 0x11, 0x14, 0x19, 0x22, 0x27,
    0x2E, 0x31, 0x34, 0x36, 0x37, 0x3E
  ],
  "memory_map": {
    "flash_size": 524288,
    "flash_start": "0x00100000",
    "ram_size": 65536,
    "ram_start": "0x20000000"
  },
  "security_levels": [
    {
      "level": 1,
      "seed_size": 4,
      "key_algo": "xor",
      "key_param": "0xABCD"
    },
    {
      "level": 3,
      "seed_size": 16,
      "key_algo": "hmac-sha256",
      "key_param": "SecretKey_ECU_V1"
    }
  ]
}
```

---

### 4.2 DID 配置

**文件**: `configs/did/standard_dids.json`

```json
{
  "data_identifiers": [
    {
      "did": "0xF190",
      "name": "VIN",
      "length": 17,
      "type": "string",
      "read_only": true,
      "default_value": "TEST_VIN_1234567890"
    },
    {
      "did": "0xF191",
      "name": "PartNumber",
      "length": 16,
      "type": "string",
      "read_only": true,
      "default_value": "PART_12345_67890"
    },
    {
      "did": "0xF910",
      "name": "ECU_Temperature",
      "length": 2,
      "type": "uint16",
      "unit": "0.01 °C",
      "read_only": false,
      "value_source": "sensor"
    }
  ]
}
```

---

### 4.3 DTC 配置

**文件**: `configs/dtc/powertrain_dtc.json`

```json
{
  "dtc_definitions": [
    {
      "code": "P0100",
      "name": "MAF Sensor Circuit",
      "description": "Mass Air Flow sensor circuit malfunction",
      "enabled": true
    },
    {
      "code": "P0200",
      "name": "Fuel Injector Circuit",
      "description": "Fuel injector circuit malfunction",
      "enabled": true
    }
  ]
}
```

---

### 4.4 系统配置

**文件**: `configs/system/can_config.json`

```json
{
  "can_interface": "vcan0",
  "bitrate": 500000,
  "test_mode": true,
  "max_ecu_count": 4,
  "timeout": {
    "p2_can_rx": 1000,
    "p2_can_server": 100,
    "n_bs": 1000,
    "n_cr": 1000
  }
}
```

---

## 5. 构建产物

### 5.1 Rust 产物

```
target/
├── debug/              # 调试构建
│   ├── uds-core       # 核心库 (rlib)
│   ├── uds-tp         # 传输层库
│   ├── uds-can        # CAN 库
│   ├── uds-bootloader # Bootloader 库
│   ├── uds-ecusim     # ECU 模拟器可执行文件
│   └── uds-server     # UDS 服务器可执行文件
│
└── release/            # 发布构建
    ├── uds-ecusim     # 优化后的可执行文件
    └── uds-server     # 优化后的可执行文件
```

---

### 5.2 Python 产物

```
tools/uds-cli/
├── build/
│   └── uds-cli        # 可执行脚本
└── dist/
    └── uds-cli-0.1.0.tar.gz  # 发布包

tools/uds-test/
├── build/
└── dist/
    └── uds-test-0.1.0.tar.gz
```

---

## 6. 测试文件组织

### 6.1 单元测试

**位置**: 与源代码同目录

**示例**: `src/uds-core/src/session.c`
```rust
#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_session_control() {
        // 测试代码
    }
}
```

---

### 6.2 集成测试

**位置**: `tests/integration/`

**文件**:
- `test_full_workflow.c` - 完整诊断流程
- `test_multi_ecu.c` - 多 ECU 场景
- `test_flash_recovery.c` - 刷写恢复

---

### 6.3 性能测试

**位置**: `tests/performance/`

**文件**:
- `bench_throughput.c` - 吞吐量测试
- `bench_latency.c` - 延迟测试

---

## 7. 文档结构

### 7.1 API 文档

**生成方式**: `cargo doc`

**输出**: `target/doc/`

**包含**:
- 所有公共 API
- 示例代码
- 类型签名

---

### 7.2 用户文档

**目录**: `docs/user_guide/`

**内容**:
- 安装指南
- 快速入门
- 命令行参考
- 故障排查

---

### 7.3 开发者文档

**目录**: `docs/developer_guide/`

**内容**:
- 架构说明
- 贡献指南
- 代码规范
- 调试技巧

---

## 8. 脚本和工具

### 8.1 环境设置

**setup_vcan.sh**:
```bash
#!/bin/bash
# 设置虚拟 CAN 总线

sudo modprobe vcan
sudo ip link add dev vcan0 type vcan
sudo ip link set up vcan0
sudo ip link add dev vcan1 type vcan
sudo ip link set up vcan1

echo "Virtual CAN interfaces created:"
ip link show vcan0
ip link show vcan1
```

---

### 8.2 构建脚本

**build.sh**:
```bash
#!/bin/bash
# 完整构建流程

echo "Building Rust components..."
cargo build --release

echo "Building Python tools..."
cd tools/uds-cli
pip install -e .
cd ../uds-test
pip install -e .

echo "Build complete!"
```

---

### 8.3 测试脚本

**test.sh**:
```bash
#!/bin/bash
# 运行所有测试

echo "Running unit tests..."
cargo test

echo "Running integration tests..."
cargo test --test integration

echo "Running Python tests..."
cd tools/uds-test
pytest

echo "All tests passed!"
```

---

## 9. 文件大小估算

### 9.1 源代码统计

| 模块 | Rust 代码 | Python 代码 | 配置文件 | 关联文档 |
|------|-----------|-------------|---------|---------|
| uds-core | ~2350行 | - | - | 03-UDS协议, 02-架构 |
| uds-tp | ~1180行 | - | - | 03-UDS协议, 05-vCAN |
| uds-can | ~760行 | - | - | 06-技术栈, 05-vCAN |
| uds-bootloader | ~1430行 | - | - | 04-Bootloader设计 |
| uds-ecusim | ~1210行 | - | - | 06-技术栈, 02-架构 |
| uds-client | ~500行 | - | - | 08-接口设计 |
| uds-cli | - | ~1350行 | - | 08-接口设计 |
| uds-test | - | ~2060行 | - | 09-测试策略 |
| 集成测试 | ~1500行 | - | - | 09-测试策略 |
| configs | - | - | ~1230行JSON | 06-技术栈, 08-接口 |
| **总计** | **~7930行** | **~3410行** | **~1230行** | - |

**与10章开发计划对齐**: 总代码量~13270行, 满足Phase 1-3的交付目标

---

### 9.2 产物大小

| 产物 | Debug 大小 | Release 大小 | 备注 |
|------|-----------|-------------|------|
| uds-ecusim (Rust) | ~8 MB | ~2 MB | 完整ECU模拟 |
| uds-client (Rust) | ~5 MB | ~1.5 MB | 可选 |
| uds-cli (Python) | ~1 MB | - | 包含依赖 |
| 文档(html) | ~5 MB | - | cargo doc生成 |

---

## 10. 结构约束与治理

- **代码所有权**: 按模块设置 Code Owners (src/uds-core, uds-tp, uds-bootloader, uds-can, tools/uds-cli, tools/uds-test)。
- **命名规范**: crate/包名使用 kebab-case; Rust 模块/文件使用 snake_case; Python 包使用小写下划线; 配置文件使用前缀 (did_*, dtc_*, ecu_*)。
- **格式与静态检查**: Rust 强制 `cargo fmt && cargo clippy -D warnings`; Python 推荐 `ruff`/`black`/`mypy`; CI 阻断未通过的 PR。
- **生成物与缓存**: 生成产物统一到 `target/` 与 `.pytest_cache/`, 并在 `.gitignore` 管控; 示例配置与固件放置于 `configs/`、`firmware/`。
- **文档同步**: 设计文档在 `design/`; 用户/开发者文档在 `docs/`; 公共 API 通过 `cargo doc` 同步到 `docs/api/`。

---

## 11. 与其他设计文档的一致性对齐

本项目结构与系统其他文档的对齐情况:

| 对齐项目 | 关联文档 | 对齐内容 | 状态 |
|---------|--------|--------|------|
| **Workspace组织** | 06-技术栈 | 所有crate版本号统一管理, 共享workspace.dependencies | ✅ |
| **uds-can新增** | 06-技术栈, 05-vCAN | SocketCAN 2.0+, 多ECU路由, 物理/功能寻址 | ✅ |
| **uds-ecusim新增** | 06-技术栈, 02-架构 | ECU隔离与并发(Tokio/once_cell/parking_lot) | ✅ |
| **ecu_registry新增** | 06-技术栈, 02-架构 | 全局ECU注册表, per-ECU会话隔离 | ✅ |
| **代码行数估算** | 10-开发计划 | 总~13270行, Phase划分对齐 | ✅ |
| **模块划分** | 02-系统架构 | 应用层/服务层/通信层/物理层分层 | ✅ |
| **uds-core服务** | 03-UDS协议 | 17个核心服务, NRC错误码定义 | ✅ |
| **刷写流程** | 04-Bootloader | 0x34/0x36/0x37服务, 多区域管理, 恢复机制 | ✅ |
| **ISO-TP支持** | 03-UDS协议, 05-vCAN | SF/FF/CF/FC帧, BS/STmin, P2/P2*/S3参数 | ✅ |
| **接口API** | 08-接口设计 | Service trait, Registry, 错误处理, 版本管理 | ✅ |
| **测试组织** | 09-测试策略 | 单元(crates内)、集成(tests/)、性能(bench) | ✅ |
| **Python工具** | 08-接口设计 | CLI/Flash工具, pydantic配置验证, pytest框架 | ✅ |
| **配置管理** | 06-技术栈, 08-接口 | JSON格式ECU/DID/DTC配置, serde_json+pydantic | ✅ |
| **脚本工具** | 10-开发计划 | setup_vcan.sh, build.sh, test.sh, lint.sh(新增) | ✅ |

---

## 版本历史

| 版本 | 日期 | 变更描述 |
|------|------|--------|
| 1.1 | 2026-01-03 | 完善Workspace组织、新增uds-can/ecusim模块、详细代码行数估算、交叉参考完整、与06章技术栈深度对齐 |
| 1.0 | 2026-01-02 | 项目结构初始版本 |

---

## 参考文档

- [02-System_Architecture_Design.md](02-System_Architecture_Design.md) - 系统架构分层
- [03-UDS_Protocol_Design.md](03-UDS_Protocol_Design.md) - UDS服务与协议
- [04-Bootloader_and_Flash_Design.md](04-Bootloader_and_Flash_Design.md) - 刷写流程
- [05-Virtual_CAN_Platform_Design.md](05-Virtual_CAN_Platform_Design.md) - SocketCAN与vCAN
- [06-Technology_Stack.md](06-Technology_Stack.md) - 技术栈与Workspace依赖
- [08-Interface_and_Integration_Design.md](08-Interface_and_Integration_Design.md) - API接口与版本管理
- [09-Testing_Strategy.md](09-Testing_Strategy.md) - 测试覆盖率目标
- [10-Development_Roadmap.md](10-Development_Roadmap.md) - Phase划分与交付物
