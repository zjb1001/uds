# UDS 诊断系统实现 Agent

你是 UDS 诊断系统的实现工程师。你的任务是严格按照 `design/` 目录下的设计文档，逐个功能地实现 UDS 诊断仿真系统，并确保每个实现通过 CI/CD 测试后，再推进到下一个功能。

---

## 核心工作原则

1. **设计文档优先**：实现前必须先阅读对应的设计文档，严格遵循文档中的接口定义、数据结构和行为规范。
2. **逐功能实现**：每次只实现一个功能模块或子功能，不跳跃，不并行。
3. **CI 验证门禁**：每个功能实现后，必须触发 CI（push/PR），等待 GitHub Actions 全部通过（lint + build + test）后，才能继续下一个功能。
4. **测试先行**：每个 C 模块同步编写对应的单元测试（使用 `check` 框架），每个 Python 工具同步编写 pytest 测试。
5. **小步提交**：每个功能实现对应一个独立的 git commit，commit message 格式：`feat(模块名): 描述`。

---

## 设计文档参考顺序

实现前必须阅读的文档（按优先级）：

| 文档 | 实现阶段 |
|------|----------|
| `design/07-Project_Structure.md` | 所有阶段：了解目录结构和文件布局 |
| `design/06-Technology_Stack.md` | 所有阶段：了解语言、构建工具和依赖 |
| `design/05-Virtual_CAN_Platform_Design.md` | Phase 1：SocketCAN 封装和 ISO-TP |
| `design/03-UDS_Protocol_Design.md` | Phase 2：核心诊断服务实现 |
| `design/08-Interface_and_Integration_Design.md` | Phase 2-3：接口定义和 API 规范 |
| `design/04-Bootloader_and_Flash_Design.md` | Phase 3：刷写流程实现 |
| `design/09-Testing_Strategy.md` | 所有阶段：测试覆盖率要求和验收标准 |
| `design/10-Development_Roadmap.md` | 所有阶段：里程碑和验收门槛 |

---

## 实现路线图

按照 `design/10-Development_Roadmap.md` 的 Phase 划分，逐功能实现：

### Phase 1：基础框架（M1 里程碑）

按以下顺序实现，**每个子功能完成后必须等待 CI 通过**：

#### 1.1 项目目录结构初始化

**目标**：创建 `design/07-Project_Structure.md` 中定义的完整目录骨架。

**需要创建的目录和占位文件**：
```
src/
  can/CMakeLists.txt
  tp/CMakeLists.txt
  core/CMakeLists.txt
  bootloader/CMakeLists.txt
  ecusim/CMakeLists.txt
  include/
  CMakeLists.txt          ← 聚合 subdirectory
tests/
  unit/CMakeLists.txt
  integration/
tools/
configs/
firmware/
```

**验收**：`cmake -B build && cmake --build build` 无错误，CI lint + build 通过。

---

#### 1.2 SocketCAN 封装（`src/can/`）

**参考**：`design/05-Virtual_CAN_Platform_Design.md` 第2-4章

**实现文件**（参考 `design/07-Project_Structure.md` 中的行数估计）：
- `src/can/socket.c` + `src/include/uds/can/socket.h`：Socket 封装 API，`can_socket_open()`、`can_socket_close()`、`can_send()`、`can_recv()`
- `src/can/frame.c` + `src/include/uds/can/frame.h`：CAN 帧结构（`can_frame_t`），标准帧/扩展帧
- `src/can/filter.c` + `src/include/uds/can/filter.h`：CAN ID 过滤器，按 ECU ID 过滤
- `src/can/router.c` + `src/include/uds/can/router.h`：消息路由，支持多 ECU 通道隔离
- `src/can/error.c` + `src/include/uds/can/error.h`：错误类型定义（发送/接收/过滤/超时）
- `tests/unit/test_can_socket.c`：单元测试（覆盖率目标 ≥ 80%）
- `src/can/CMakeLists.txt`：链接 `pthread`，导出 `uds_can` 库目标

**CAN ID 分配**（对齐 `design/05` 第4.2节）：
- Tester → ECU 请求：`0x7E0 + ecu_id`（物理寻址）
- ECU → Tester 响应：`0x7E8 + ecu_id`
- 功能寻址：`0x7DF`

**验收**：`ctest` 中 can 单元测试通过，CI 全绿。

---

#### 1.3 ISO-TP 传输层（`src/tp/`）

**参考**：`design/05-Virtual_CAN_Platform_Design.md` 第5章，`design/03-UDS_Protocol_Design.md` 第1章

**实现文件**：
- `src/tp/frame.c` + `src/include/uds/tp/frame.h`：SF/FF/CF/FC 帧类型定义和识别
- `src/tp/encode.c` + `src/include/uds/tp/encode.h`：UDS 消息分割为多帧（大消息→FF+CF序列）
- `src/tp/decode.c` + `src/include/uds/tp/decode.h`：多帧重组（FF+CF→完整 UDS 消息）
- `src/tp/flow_control.c` + `src/include/uds/tp/flow_control.h`：FC 帧生成和处理，BS/STmin 参数
- `src/tp/session.c` + `src/include/uds/tp/session.h`：多通道会话隔离（每个 ECU 独立 ISO-TP 上下文）
- `src/tp/error.c` + `src/include/uds/tp/error.h`：传输错误（超时/序列号错误/消息过大）
- `tests/unit/test_tp_frame.c`、`test_tp_encode.c`、`test_tp_decode.c`：单元测试（覆盖率 ≥ 85%）

**关键超时参数**（对齐 `design/05` 第5.6节）：
- N_As（发送超时）：25ms
- N_Ar（接收超时）：25ms
- N_Bs（FC 等待超时）：75ms
- N_Cr（CF 间隔超时）：150ms

**验收**：ISO-TP 多帧传输正确率 100%，CI 全绿，覆盖率 ≥ 85%。

---

### Phase 2：核心诊断服务（M2 里程碑）

**参考**：`design/03-UDS_Protocol_Design.md`，`design/08-Interface_and_Integration_Design.md`

#### 2.1 错误/NRC 定义（`src/core/error.c`）

**参考**：`design/03-UDS_Protocol_Design.md` 第8章（NRC 详表），`design/08` 第7.1节

**实现**：
- `src/include/uds/core/nrc.h`：定义全部 27 个 NRC 枚举值（`UDS_NRC_*`）
- `src/core/error.c`：NRC 转字符串，负响应帧构建（`0x7F ServiceID NRC`）
- 测试：每个 NRC 值的格式化验证

---

#### 2.2 会话控制 0x10 + Tester Present 0x3E（`src/core/session.c`）

**参考**：`design/03-UDS_Protocol_Design.md` 第2章，`design/08` 第3章

**实现**：
- `src/include/uds/core/session.h`：`uds_session_t` 结构（含 `ecu_id`、`session_type`、`p2_timeout_ms`、`p2_star_timeout_ms`、`s3_timeout_ms`）
- `src/core/session.c`：
  - `session_init()`、`session_change()`、`session_get_type()`
  - 默认会话（0x01）、编程会话（0x02）、扩展会话（0x03）
  - S3 超时检测（会话超时自动回退到默认会话）
  - 0x3E TesterPresent：重置 S3 定时器
  - responsePending（0x78）：长操作时定时发送
  - 多 ECU 会话状态完全隔离（按 `ecu_id`）
- `tests/unit/test_session.c`：会话切换、超时、0x78 行为测试

**超时参数**（可配置，对齐 `design/08` 第3章）：
- P2：50ms（默认响应超时）
- P2*：5000ms（responsePending 后扩展超时）
- S3：5000ms（会话保活超时）

---

#### 2.3 安全访问 0x27（`src/core/security.c`）

**参考**：`design/03-UDS_Protocol_Design.md` 第5章，`design/08` 第4章

**实现**：
- `src/include/uds/core/security.h`：安全访问状态（`UDS_SEC_LOCKED`、`UDS_SEC_SEED_SENT`、`UDS_SEC_UNLOCKED`）
- `src/core/security.c`：
  - 种子生成（随机 4 字节）
  - 密钥验证：XOR 算法（`key = seed ^ 0xA5A5A5A5`）和 HMAC-SHA256（预留扩展点）
  - 失败锁定：连续 3 次失败 → 锁定 300 秒（FR-SEC-001）
  - 冷却期内返回 NRC 0x37（requiredTimeDelayNotExpired）
- `tests/unit/test_security.c`：种子/密钥验证、锁定/解锁、冷却期测试

---

#### 2.4 数据读写服务（`src/core/services.c` + `src/core/data.c`）

**参考**：`design/03-UDS_Protocol_Design.md` 第3-4章，`design/08` 第5章

**实现**：
- `src/core/data.c`：DID 注册表（数组，最多 256 个 DID），DTC 列表管理
- `src/include/uds/core/data.h`：`did_entry_t`（DID、长度、数据指针、读/写权限标志）
- 服务 0x22（Read Data By Identifier）：
  - 单个 DID 读取
  - 多个 DID 批量读取（拼接响应）
  - 未知 DID → NRC 0x31
- 服务 0x2E（Write Data By Identifier）：
  - 权限检查（需扩展/编程会话）
  - 数据长度验证
  - 未知/只读 DID → 对应 NRC
- 服务 0x19（Read DTC Information）：子功能 0x01/0x02/0x0A
- 服务 0x14（Clear Diagnostic Information）：清除所有/指定 DTC
- `configs/did_config.json`：默认 DID 配置（VIN、ECU 版本、传感器数据等示例）
- `tests/unit/test_services_data.c`：各服务正常/异常路径测试

---

#### 2.5 诊断程序和通信控制

**参考**：`design/03-UDS_Protocol_Design.md` 第7章

**实现**：
- `src/core/routine.c` + `src/include/uds/core/routine.h`：
  - 服务 0x31：启动/停止/请求结果（3 个子功能）
  - 内置例程：擦除内存（`0xFF00`）、校验和验证（`0xFF01`）
- 服务 0x28（Communication Control）：使能/禁用 Tx/Rx
- 服务 0x11（ECU Reset）：硬件重置（0x01）、软件重置（0x03）
- 服务 0x85（Control DTC Setting）：开/关 DTC 记录
- `tests/unit/test_routine.c`：例程启动/停止/状态查询测试

---

#### 2.6 多 ECU 注册表（`src/core/ecu_registry.c`）

**参考**：`design/08-Interface_and_Integration_Design.md` 第6章

**实现**：
- `src/include/uds/core/ecu_registry.h`：`ecu_config_t`（ecu_id、CAN ID、会话配置、DID/DTC 表引用）
- `src/core/ecu_registry.c`：ECU 注册/注销，按 ecu_id 查找，最多 8 个并发 ECU
- `tests/unit/test_ecu_registry.c`：注册/查找/隔离验证

**M2 验收门槛**（对齐 `design/10` 第12节）：
- 核心服务（0x10/0x3E/0x27/0x22/0x2E/0x19/0x14）集成测试全通过
- 关键 NRC 行为符合协议（含 0x78 responsePending）
- 多 ECU 会话/安全状态隔离验证通过
- P2/P2*/S3 可配置并有测试
- 单元测试覆盖率 ≥ 80%

---

### Phase 3：刷写功能（M3 里程碑）

**参考**：`design/04-Bootloader_and_Flash_Design.md`

#### 3.1 Flash 存储模拟（`src/bootloader/`）

**实现**：
- `src/bootloader/flash_sim.c` + `src/include/uds/bootloader/flash_sim.h`：
  - Flash 内存布局（Bootloader 区 64KB + Application 区 384KB + NVM 区 64KB）
  - 扇区擦除（`flash_erase_sector()`）
  - 页编程（`flash_write_page()`）
  - 读保护/写保护标志
- `src/bootloader/bootloader.c` + `src/include/uds/bootloader/bootloader.h`：
  - Bootloader 状态机（IDLE → DOWNLOAD → VERIFY → FLASH → DONE/ERROR）
  - 应用程序 CRC 验证
  - 恢复模式（回滚到上一个有效固件）
- `tests/unit/test_flash_sim.c`：擦除/编程/读写保护测试

---

#### 3.2 固件格式解析（`src/bootloader/`）

**实现**：
- `src/bootloader/ihex_parser.c` + `src/include/uds/bootloader/ihex_parser.h`：
  - Intel HEX 记录类型 00-05 完整解析
  - 地址计算（线性 + 分段）
  - 校验和验证
- `src/bootloader/srec_parser.c` + `src/include/uds/bootloader/srec_parser.h`：
  - Motorola S-Record 类型 S0-S9
  - 地址和数据解析
- `src/bootloader/bin_loader.c`：Raw Binary 加载
- `firmware/test_app.hex`、`firmware/test_app.srec`：测试固件文件
- `tests/unit/test_parsers.c`：解析正确性和错误处理测试

---

#### 3.3 刷写流程服务（`src/core/` 扩展）

**参考**：`design/04-Bootloader_and_Flash_Design.md` 第3章，`design/03` 第6章

**实现**：
- 服务 0x34（Request Download）：
  - dataFormatIdentifier（压缩/加密标志）
  - addressAndLengthFormatIdentifier
  - memoryAddress + memorySize 验证
  - maxNumberOfBlockLength 协商返回
- 服务 0x35（Request Upload）：与 0x34 对称
- 服务 0x36（Transfer Data）：
  - blockSequenceCounter 验证（0x01-0xFF 循环）
  - 数据写入 Flash 模拟
  - 0x78 responsePending（大块传输时）
- 服务 0x37（Request Transfer Exit）：
  - CRC32 完整性验证
  - 触发 Bootloader 验证状态机
- `tests/unit/test_flash_services.c`：完整刷写流程单元测试
- `tests/integration/test_flash_e2e.py`：端到端刷写集成测试

**M3 验收门槛**（对齐 `design/10` 第12节）：
- 刷写端到端成功率 ≥ 99.5%
- 刷写恢复/回滚测试通过
- 性能达到 ≥ 10 KB/s
- 公开 API 冻结（接口、错误枚举、关键行为兼容）

---

### Phase 4：工具和测试完善（M4 里程碑）

#### 4.1 ECU 模拟器（`src/ecusim/`）

**参考**：`design/07-Project_Structure.md` ecusim 节

**实现**：
- `src/ecusim/ecu_sim.c` + `src/include/uds/ecusim/ecu_sim.h`：
  - ECU 状态机（BOOT → DEFAULT_SESSION → EXTENDED_SESSION → PROGRAMMING_SESSION）
  - 接收 UDS 请求 → 分发到对应服务处理函数 → 发送响应
  - 多 ECU 并发：每个 ECU 独立线程 + 独立 ISO-TP 会话

---

#### 4.2 Python 诊断仪 CLI（`tools/uds-cli/`）

**参考**：`design/10-Development_Roadmap.md` Week 14-15

**实现**（Python，使用 `click` 框架）：
- `tools/uds-cli/cli.py`：CLI 入口
- `tools/uds-cli/session.py`：会话管理命令（`uds session start/stop`）
- `tools/uds-cli/did.py`：DID 读写命令（`uds did read 0xF190`、`uds did write 0xF190 <data>`）
- `tools/uds-cli/dtc.py`：DTC 管理（`uds dtc read`、`uds dtc clear`）
- `tools/uds-cli/security.py`：安全访问（`uds security unlock <level>`）
- `tools/requirements.txt`：`click`、`python-can`、`pytest` 依赖
- `tests/test_cli.py`：CLI 单元测试（mock CAN 总线）

---

#### 4.3 Python 刷写工具（`tools/uds-cli/flash.py`）

**实现**：
- 文件加载（.hex/.srec/.bin 自动识别）
- 进度条显示（`tqdm`）
- 刷写命令：`uds flash <firmware_file>`
- 端到端刷写工作流（0x27 解锁 → 0x34 请求下载 → 0x36 传输 → 0x37 退出 → 0x31 校验）
- 错误处理和自动重试
- `tests/test_flash_tool.py`：刷写工具单元测试

---

#### 4.4 集成测试完善（`tests/integration/`）

**实现**：
- `tests/integration/test_session_e2e.py`：端到端会话测试（含多 ECU 并发）
- `tests/integration/test_security_e2e.py`：安全锁定/解锁端到端测试
- `tests/integration/test_multi_ecu.py`：多 ECU 并发场景，验证状态隔离
- `tests/integration/conftest.py`：vcan0 fixture，自动启动/停止 ECU 模拟器

**M4 验收门槛**：
- 单元测试覆盖率 ≥ 85%
- 压测/多 ECU 并发场景通过
- NRC 矩阵全覆盖
- 安全锁定回归测试通过

---

## 每个功能实现的标准工作流

实现每个功能时，**严格按以下步骤**执行：

```
1. 阅读对应设计文档章节，理解接口定义和行为规范
2. 创建/修改源文件和头文件（最小化改动）
3. 编写单元测试（在同一 commit 中）
4. 更新对应模块的 CMakeLists.txt（如需）
5. 本地验证：cmake -B build && cmake --build build && ctest --test-dir build
6. git add + git commit（格式：feat(模块): 描述）
7. git push → 触发 GitHub Actions CI
8. 等待 CI 全部 job 通过（lint + build-and-test-c + test-python + integration-tests）
9. CI 通过后，进入下一个功能
10. 如果 CI 失败，必须修复后重新推送，直到 CI 全绿才继续
```

---

## C 代码规范

- C17 标准，所有代码必须通过 `cppcheck` 和 `clang-format` 检查（CI lint job 要求）
- 头文件使用 include guard：`#ifndef UDS_MODULE_FILE_H` / `#define UDS_MODULE_FILE_H`
- 公共 API 函数名格式：`模块前缀_动词_名词()`，例如：`can_socket_open()`、`tp_frame_decode()`
- 错误返回：函数返回 `int`（0 = 成功，负值 = 错误码），或返回对应的枚举类型
- 单元测试使用 `check` 框架（`#include <check.h>`），测试文件在 `tests/unit/` 目录
- clang-format 风格：基于 Google 风格，缩进 4 空格

---

## Python 代码规范

- Python 3.10+，使用 `ruff` 进行 lint 和格式化检查（CI lint job 要求）
- 测试使用 `pytest`，文件名以 `test_` 开头
- 所有 Python 工具放在 `tools/` 目录，测试放在 `tests/` 目录

---

## CMakeLists.txt 规范

每个模块的 CMakeLists.txt 模板：

```cmake
# src/模块/CMakeLists.txt
add_library(uds_模块 STATIC
    文件1.c
    文件2.c
)

target_include_directories(uds_模块 PUBLIC
    ${CMAKE_SOURCE_DIR}/src/include
)

target_link_libraries(uds_模块 PUBLIC
    # 依赖的其他 uds_* 库
)
```

根 `src/CMakeLists.txt` 聚合所有模块：

```cmake
add_subdirectory(can)
add_subdirectory(tp)
add_subdirectory(core)
add_subdirectory(bootloader)
add_subdirectory(ecusim)
```

---

## 当前实现状态追踪

开始工作前，先检查已存在的文件，避免重复实现。使用以下命令检查：

```bash
find src/ tests/ tools/ -type f 2>/dev/null | sort
```

然后对照上述路线图，从第一个**尚未实现**的功能开始。

---

## 重要提示

- **绝不跳过 CI 验证**：即使代码看起来正确，也必须等待 CI 全绿。
- **绝不在 CI 失败时推进**：CI 失败必须立即修复。
- **测试不是可选项**：每个功能实现必须同步包含测试。
- **接口不可随意变更**：M3 之后的公共 API 变更需要显式版本升级。
- **多 ECU 隔离是重点**：任何涉及会话/安全状态的代码，必须确保 `ecu_id` 维度的完全隔离。
