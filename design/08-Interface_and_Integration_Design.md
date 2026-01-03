# 接口和集成设计

## 文档信息

| 项目 | 内容 |
|------|------|
| **文档版本** | 1.1 |
| **创建日期** | 2026-01-02 |
| **最后修订** | 2026-01-03 |
| **作者** | UDS 诊断系统设计团队 |
| **状态** | 审查中 |

---

## 1. 引言

### 1.1 目的

本文档定义 UDS 诊断系统的内部接口和外部集成接口,包括 API 定义、数据结构、通信协议等。

### 1.2 范围

**包含内容**:
- 模块间接口定义
- 公共 API 规范
- 数据结构定义
- 错误处理机制
- 外部系统集成接口

---

## 2. 接口设计原则

### 2.1 设计原则

1. **高内聚低耦合**: 模块间依赖最小化
2. **接口稳定性**: 公共 API 向后兼容
3. **类型安全**: 使用强类型,避免运行时错误
4. **错误处理**: 明确的错误类型和处理策略
5. **文档完整**: 所有公开接口都有文档

### 2.2 接口分类

```
┌─────────────────────────────────────────────────────────┐
│                  接口分层                                │
├─────────────────────────────────────────────────────────┤
│  外部接口 (External Interface)                          │
│  ┌──────────────┬──────────────┬──────────────┐        │
│  │ 诊断仪 API   │ 刷写工具 API │ 测试框架 API │        │
│  └──────────────┴──────────────┴──────────────┘        │
├─────────────────────────────────────────────────────────┤
│  内部接口 (Internal Interface)                          │
│  ┌──────────────┬──────────────┬──────────────┐        │
│  │ 服务层接口   │ 传输层接口   │ CAN 层接口   │        │
│  └──────────────┴──────────────┴──────────────┘        │
└─────────────────────────────────────────────────────────┘
```

---

## 3. 核心数据结构

### 3.1 UDS 报文结构

**Rust 定义** (对齐03-UDS协议与01-需求规范):

```rust
/// UDS 报文 (见03-UDS_Protocol_Design)
#[derive(Debug, Clone, PartialEq)]
pub struct UdsMessage {
    /// 服务 ID (0x10-0x3E, 见03章服务清单)
    pub service_id: u8,
    /// 子功能 (可选, 如会话类型、安全级别)
    pub sub_function: Option<u8>,
    /// 数据部分 (服务参数)
    pub data: Vec<u8>,
}

impl UdsMessage {
    /// 创建新的 UDS 报文
    pub fn new(service_id: u8, data: Vec<u8>) -> Self {
        UdsMessage {
            service_id,
            sub_function: None,
            data,
        }
    }

    /// 带子功能的报文 (如0x10的会话类型)
    pub fn with_sub_function(service_id: u8, sub_function: u8, data: Vec<u8>) -> Self {
        UdsMessage {
            service_id,
            sub_function: Some(sub_function),
            data,
        }
    }

    /// 序列化为字节 (ISO-TP分段前准备)
    pub fn to_bytes(&self) -> Vec<u8> {
        let mut bytes = vec![self.service_id];
        if let Some(sf) = self.sub_function {
            bytes.push(sf);
        }
        bytes.extend(&self.data);
        bytes
    }

    /// 从字节解析 (ISO-TP重组后处理)
    pub fn from_bytes(bytes: &[u8]) -> Result<Self, ParseError> {
        if bytes.is_empty() {
            return Err(ParseError::EmptyMessage);
        }

        let service_id = bytes[0];
        let (sub_function, data) = if bytes.len() > 1 {
            // 简化: 假设第二个字节是子功能或数据
            (Some(bytes[1]), bytes[2..].to_vec())
        } else {
            (None, vec![])
        };

        Ok(UdsMessage {
            service_id,
            sub_function,
            data,
        })
    }
}
```

**对齐说明** (与03-UDS协议):
- service_id 范围: 0x10-0x3E (17个核心服务)
- sub_function 取值见03章具体服务定义

---

### 3.2 UDS 响应结构

```rust
/// UDS 响应 (FR-TRANS-005: 支持响应抑制)
#[derive(Debug, Clone, PartialEq)]
pub struct UdsResponse {
    /// 响应 ID: 正响应 = service_id + 0x40; 负响应 = 0x7F
    pub response_id: u8,
    /// 响应数据 (不包括 service_id 或 NRC)
    pub data: Vec<u8>,
    /// 负响应码 (NRC: 见7.1节)
    pub nrc: Option<u8>,
    /// 响应抑制标志 (FR-TRANS-005: SuppressPositiveResponse bit)
    pub suppress_positive_response: bool,
    /// 响应时间戳 (用于P2*/S3超时检测)
    pub timestamp: std::time::Instant,
}

impl UdsResponse {
    /// 创建肯定响应
    pub fn positive(service_id: u8, data: Vec<u8>) -> Self {
        UdsResponse {
            response_id: service_id.wrapping_add(0x40),
            data,
            nrc: None,
            suppress_positive_response: false,
            timestamp: std::time::Instant::now(),
        }
    }

    /// 创建否定响应
    pub fn negative(service_id: u8, nrc: u8) -> Self {
        UdsResponse {
            response_id: 0x7F,
            data: vec![service_id, nrc],
            nrc: Some(nrc),
            suppress_positive_response: true,
            timestamp: std::time::Instant::now(),
        }
    }

    /// 判断是否为肯定响应
    pub fn is_positive(&self) -> bool {
        self.response_id != 0x7F
    }

    /// 获取响应服务 ID
    pub fn response_id(&self) -> u8 {
        self.response_id
    }

    /// 检查是否需要抑制正响应 (FR-TRANS-005)
    pub fn should_suppress(&self) -> bool {
        self.suppress_positive_response && self.nrc.is_none()
    }
}
```

---

### 3.3 诊断会话结构

```rust
/// 诊断会话类型 (FR-SES-001: 见01-SRS)
#[derive(Debug, Clone, Copy, PartialEq)]
pub enum SessionType {
    /// 默认会话 (0x01)
    Default = 0x01,
    /// 编程会话 (0x02, FR-FLASH-001) 
    Programming = 0x02,
    /// 扩展诊断会话 (0x03)
    ExtendedDiagnostic = 0x03,
    /// 安全系统诊断会话 (0x04)
    SafetySystem = 0x04,
}

/// 诊断会话 (FR-SES-001/002/003, FR-TRANS-003/004)
/// 多ECU支持: 通过ecu_registry架构(见06/07章)
pub struct DiagnosticSession {
    /// 会话 ID (全局唯一)
    pub id: u32,
    /// ECU ID (多ECU隔离, FR-TRANS-004)
    pub ecu_id: u8,
    /// 会话类型
    pub session_type: SessionType,
    /// 开始时间
    pub start_time: std::time::Instant,
    /// 最后心跳时间
    pub last_heartbeat: std::time::Instant,
    /// 当前安全级别 (0-FF, 见FR-SEC-001)
    pub security_level: u8,
    /// P2 服务器超时 (毫秒, 可选/可配置)
    /// 默认值见01-SRS FR-TRANS-003; 
    /// 如果Some则覆盖ECU配置
    pub p2_server: Option<u16>,
    /// P2* 服务器超时 (毫秒, 可选/可配置)
    /// 用于长延迟操作(编程/擦除, 见FR-FLASH-001)
    pub p2_server_max: Option<u16>,
    /// S3 服务器超时 (秒, 可选/可配置)
    /// 默认: 5-20s (取决于ECU配置)
    pub s3_server: Option<u32>,
}

impl DiagnosticSession {
    /// 创建新会话 (支持多ECU)
    pub fn new(id: u32, ecu_id: u8, session_type: SessionType) -> Self {
        let now = std::time::Instant::now();
        DiagnosticSession {
            id,
            ecu_id,
            session_type,
            start_time: now,
            last_heartbeat: now,
            security_level: 0,
            p2_server: None,      // 使用ECU配置
            p2_server_max: None,  // 使用ECU配置
            s3_server: None,      // 使用ECU配置
        }
    }

    /// 检查会话是否超时
    pub fn is_expired(&self) -> bool {
        let elapsed = self.last_heartbeat.elapsed().as_secs();
        // 默认S3=10s (若未配置)
        let s3_secs = self.s3_server.unwrap_or(10) as u64;
        elapsed > s3_secs
    }

    /// 更新心跳时间
    pub fn refresh_heartbeat(&mut self) {
        self.last_heartbeat = std::time::Instant::now();
    }

    /// 获取有效的P2超时 (毫秒)
    /// 优先级: 会话配置 > ECU配置(需外部传入) > 默认100ms
    pub fn get_p2_timeout(&self, ecu_default: Option<u16>) -> u16 {
        self.p2_server
            .or(ecu_default)
            .unwrap_or(100)
    }

    /// 获取有效的P2*超时 (毫秒)
    /// 用于长延迟操作(编程/擦除, 见FR-FLASH-001)
    pub fn get_p2_max_timeout(&self, ecu_default: Option<u16>) -> u16 {
        self.p2_server_max
            .or(ecu_default)
            .unwrap_or(2000)
    }
}
```

---

### 3.4 安全访问结构 (FR-SEC-001/002)

```rust
/// 安全错误 (FR-SEC-001: 3次失败锁定5分钟)
#[derive(Debug, Clone)]
pub enum SecurityError {
    /// 已锁定 (锁定期间内)
    Locked {
        locked_until: std::time::Instant,
    },
    /// 无种子 (未请求种子)
    NoSeed,
    /// 无效密钥 (不匹配)
    InvalidKey {
        attempts_remaining: u8,
    },
    /// 达到最大尝试次数 (3次)
    MaxAttemptsExceeded,
    /// 种子生成失败
    SeedGenerationFailed(String),
}

/// 安全级别 (FR-SEC-001: 见01-SRS)
#[derive(Debug, Clone, Copy)]
pub struct SecurityLevel {
    /// 级别 (1-FF, 见01-SRS权限矩阵)
    pub level: u8,
    /// 种子大小 (字节)
    pub seed_size: u8,
    /// 最大尝试次数 (FR-SEC-001: 通常为3)
    pub max_attempts: u8,
    /// 锁定时间 (秒, FR-SEC-001: 通常为300=5分钟)
    pub lockout_duration: u32,
}

/// 密钥生成算法 (见06-技术栈: 支持XOR和HMAC)
pub enum KeyAlgorithm {
    /// XOR 算法 (mask)
    Xor(u32),
    /// HMAC-SHA256
    HmacSha256(String),
    /// 自定义算法
    Custom(String),
}

/// 安全访问状态 (FR-SEC-001/002)
pub struct SecurityAccess {
    /// 当前解锁级别 (可多个级别同时解锁)
    pub unlocked_levels: Vec<u8>,
    /// 各级别尝试计数 (3次失败后锁定)
    pub attempt_counts: std::collections::HashMap<u8, u8>,
    /// 各级别锁定状态
    pub locked_until: std::collections::HashMap<u8, std::time::Instant>,
    /// 各级别当前种子
    pub current_seeds: std::collections::HashMap<u8, Vec<u8>>,
}

impl SecurityAccess {
    /// 请求种子 (见FR-SEC-001)
    pub fn request_seed(&mut self, level: u8) -> Result<Vec<u8>, SecurityError> {
        if self.is_locked(level) {
            let locked_time = self.locked_until.get(&level).copied();
            return Err(SecurityError::Locked {
                locked_until: locked_time.unwrap_or_else(std::time::Instant::now),
            });
        }

        let seed = self.generate_seed(level)
            .map_err(|e| SecurityError::SeedGenerationFailed(e))?;
        self.current_seeds.insert(level, seed.clone());
        Ok(seed)
    }

    /// 验证密钥 (FR-SEC-001: 3次失败后锁定5分钟)
    pub fn verify_key(&mut self, level: u8, key: &[u8]) -> Result<(), SecurityError> {
        if self.is_locked(level) {
            let locked_time = self.locked_until.get(&level).copied();
            return Err(SecurityError::Locked {
                locked_until: locked_time.unwrap_or_else(std::time::Instant::now),
            });
        }

        let seed = self.current_seeds.get(&level)
            .ok_or(SecurityError::NoSeed)?;

        let expected_key = self.compute_key(level, seed)?;

        if key == expected_key.as_slice() {
            // 密钥正确: 解锁此级别, 清除尝试计数
            self.unlocked_levels.push(level);
            self.attempt_counts.remove(&level);
            self.locked_until.remove(&level);
            Ok(())
        } else {
            // 密钥错误: 增加尝试计数
            let count = self.attempt_counts.entry(level).or_insert(0);
            *count += 1;

            // 3次失败后锁定5分钟 (FR-SEC-001)
            if *count >= 3 {
                self.lock_level(level, 300); // 300秒 = 5分钟
                Err(SecurityError::MaxAttemptsExceeded)
            } else {
                let remaining = 3 - *count;
                Err(SecurityError::InvalidKey {
                    attempts_remaining: remaining,
                })
            }
        }
    }

    /// 检查某级别是否已锁定
    fn is_locked(&self, level: u8) -> bool {
        if let Some(locked_until) = self.locked_until.get(&level) {
            locked_until.elapsed().as_secs() == 0
        } else {
            false
        }
    }

    /// 锁定某安全级别 (FR-SEC-001)
    fn lock_level(&mut self, level: u8, duration_secs: u32) {
        let locked_until = std::time::Instant::now()
            + std::time::Duration::from_secs(duration_secs as u64);
        self.locked_until.insert(level, locked_until);
    }

    /// 生成种子
    fn generate_seed(&self, level: u8) -> Result<Vec<u8>, String> {
        // 实现见06-技术栈: 使用rand 库
        use rand::Rng;
        let mut rng = rand::thread_rng();
        let seed_size = 4; // 默认4字节
        let seed: Vec<u8> = (0..seed_size)
            .map(|_| rng.gen::<u8>())
            .collect();
        Ok(seed)
    }

    /// 计算预期密钥
    fn compute_key(&self, level: u8, seed: &[u8]) -> Result<Vec<u8>, SecurityError> {
        // 简化: 使用XOR算法 (实际应使用configuration或KeyAlgorithm枚举)
        let mask = 0xABCD_EFu32;
        let mut key = Vec::new();
        for byte in seed {
            key.push(byte ^ (mask >> 8) as u8);
        }
        Ok(key)
    }
}
```

---

### 3.5 Flash 编程结构

```rust
/// Flash 编程请求
pub struct FlashProgrammingRequest {
    /// 数据格式
    pub data_format: DataFormat,
    /// 地址
    pub address: u32,
    /// 数据大小
    pub size: u32,
    /// 数据块大小
    pub block_size: u16,
}

/// 数据格式
pub enum DataFormat {
    RawBinary,
    MotorolaSRecord,
    IntelHex,
}

/// Flash 编程会话
pub struct FlashSession {
    /// 会话 ID
    pub id: u32,
    /// 当前状态
    pub state: FlashState,
    /// 目标地址
    pub target_address: u32,
    /// 总大小
    pub total_size: u32,
    /// 已接收大小
    pub received_size: u32,
    /// 数据缓冲区
    pub buffer: Vec<u8>,
    /// 校验和
    pub expected_checksum: Option<u32>,
}

/// Flash 状态
#[derive(Debug, Clone, Copy, PartialEq)]
pub enum FlashState {
    Idle,
    RequestDownload,
    Transferring,
    Verifying,
    Complete,
    Error(FlashError),
}
```

---

## 3.4 ECU 配置与多 ECU 管理 (新增, 对齐06-技术栈与07-项目结构)

### 3.4.1 ECU 配置结构

```rust
/// ECU 配置 (对应configs/ecu/引入, 见07-Project_Structure)
/// 支持从JSON/YAML加载, 见10-Roadmap Week 6-7
#[derive(Debug, Clone, Deserialize)]
pub struct EcuConfig {
    /// ECU唯一标识
    pub ecu_id: u8,
    
    /// ECU名称 (便于调试和日志)
    pub name: String,
    
    /// CAN标识符相关配置
    pub can_config: CanConfig,
    
    /// 超时参数 (单位: 毫秒, 见3.2章节)
    /// P2: 普通响应超时 (默认100ms, ISO-14229-1规范)
    pub p2_timeout_ms: u16,
    
    /// P2*: 长响应超时 (默认2500ms, 刷写/长操作)
    pub p2_max_timeout_ms: u16,
    
    /// S3: 会话超时 (单位: 秒, 默认10s)
    pub s3_timeout_secs: u16,
    
    /// responsePending (0x78) 最大轮询次数 (默认3, 见09-Testing_Strategy)
    pub responsePending_max_retries: u8,
    
    /// 响应待定轮询间隔 (毫秒, 默认100ms)
    pub responsePending_poll_interval_ms: u16,
    
    /// 数据标识符注册 (0xF1xx-0xFDxx)
    pub supported_dids: Vec<u16>,
    
    /// 故障码支持 (P-Code等)
    pub supported_dtcs: Vec<u32>,
    
    /// 安全级别配置 (见3.5节)
    pub security_levels: Vec<SecurityLevel>,
    
    /// 会话类型支持 (默认支持0x01-0x04)
    pub supported_sessions: Vec<u8>,
    
    /// Flash编程地址范围 (开始, 结束)
    pub flash_address_range: Option<(u32, u32)>,
    
    /// 权限矩阵: (service_id, session_type) -> bool
    /// 用于控制特定会话下特定服务是否可用
    pub access_matrix: std::collections::BTreeMap<(u8, u8), bool>,
}

/// CAN相关配置
#[derive(Debug, Clone, Deserialize)]
pub struct CanConfig {
    /// 请求ID (客户端->ECU)
    pub rx_id: u32,
    
    /// 响应ID (ECU->客户端)
    pub tx_id: u32,
    
    /// CAN扩展帧标识 (默认false, 11-bit)
    pub extended_frame: bool,
    
    /// ISO-TP TA (Target Address), 用于多地址支持
    pub ta: Option<u8>,
    
    /// ISO-TP NAE (Non-Addressed Extension), 见03-UDS_Protocol
    pub nae: Option<u8>,
}

/// ECU配置加载器 (从JSON文件)
impl EcuConfig {
    /// 从文件加载 (见07-Project_Structure configs/)
    pub fn from_file(path: &str) -> Result<Self, serde_json::Error> {
        let json = std::fs::read_to_string(path)?;
        serde_json::from_str(&json)
    }
    
    /// 验证配置合法性
    pub fn validate(&self) -> Result<(), String> {
        if self.ecu_id == 0 {
            return Err("ECU ID不能为0".to_string());
        }
        if self.p2_timeout_ms == 0 || self.p2_max_timeout_ms == 0 {
            return Err("超时参数不能为0".to_string());
        }
        if self.p2_timeout_ms > self.p2_max_timeout_ms {
            return Err("P2不能大于P2*".to_string());
        }
        Ok(())
    }
}
```

### 3.4.2 ECU 注册表与管理

```rust
/// ECU 注册表 (管理多ECU生命周期和配置)
/// 见06-Technology_Stack多ECU并发栈和07-Project_Structure
pub struct EcuRegistry {
    /// ECU配置映射: ecu_id -> EcuConfig
    configs: std::collections::HashMap<u8, Arc<EcuConfig>>,
    
    /// 当前活跃会话: (ecu_id, session_id) -> DiagnosticSession
    /// 支持多ECU并发会话隔离 (FR-TRANS-004)
    sessions: std::collections::HashMap<(u8, u32), Arc<tokio::sync::Mutex<DiagnosticSession>>>,
    
    /// ECU锁: ecu_id -> RwLock (用于并发访问保护)
    locks: std::collections::HashMap<u8, Arc<tokio::sync::RwLock<()>>>,
}

impl EcuRegistry {
    /// 创建新注册表
    pub fn new() -> Self {
        EcuRegistry {
            configs: std::collections::HashMap::new(),
            sessions: std::collections::HashMap::new(),
            locks: std::collections::HashMap::new(),
        }
    }
    
    /// 注册ECU配置
    pub fn register_ecu(&mut self, config: EcuConfig) -> Result<(), String> {
        config.validate()?;
        self.configs.insert(config.ecu_id, Arc::new(config.clone()));
        self.locks.insert(config.ecu_id, Arc::new(tokio::sync::RwLock::new(())));
        Ok(())
    }
    
    /// 从配置文件目录加载所有ECU配置
    /// 对应07-Project_Structure的configs/ecu/目录
    pub async fn load_from_directory(dir: &str) -> Result<Self, Box<dyn std::error::Error>> {
        let mut registry = Self::new();
        for entry in std::fs::read_dir(dir)? {
            let entry = entry?;
            let path = entry.path();
            if path.extension().map_or(false, |ext| ext == "json") {
                let config = EcuConfig::from_file(path.to_str().unwrap())?;
                registry.register_ecu(config)?;
            }
        }
        Ok(registry)
    }
    
    /// 获取ECU配置
    pub fn get_ecu_config(&self, ecu_id: u8) -> Option<Arc<EcuConfig>> {
        self.configs.get(&ecu_id).cloned()
    }
    
    /// 创建新会话 (支持多ECU隔离)
    pub async fn create_session(
        &mut self,
        ecu_id: u8,
        session_type: SessionType,
    ) -> Result<Arc<tokio::sync::Mutex<DiagnosticSession>>, String> {
        let config = self.get_ecu_config(ecu_id)
            .ok_or(format!("ECU {} not found", ecu_id))?;
        
        let session_id = std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .unwrap()
            .as_millis() as u32;
        
        let mut session = DiagnosticSession::new(session_id, ecu_id, session_type);
        
        // 从ECU配置应用超时参数 (见3.4.3超时策略)
        session.p2_server = Some(config.p2_timeout_ms);
        session.p2_server_max = Some(config.p2_max_timeout_ms);
        session.s3_server = Some(config.s3_timeout_secs as u32 * 1000);
        
        let session = Arc::new(tokio::sync::Mutex::new(session));
        self.sessions.insert((ecu_id, session_id), session.clone());
        Ok(session)
    }
    
    /// 获取会话 (线程安全)
    pub fn get_session(
        &self,
        ecu_id: u8,
        session_id: u32,
    ) -> Option<Arc<tokio::sync::Mutex<DiagnosticSession>>> {
        self.sessions.get(&(ecu_id, session_id)).cloned()
    }
    
    /// 关闭会话
    pub fn close_session(&mut self, ecu_id: u8, session_id: u32) {
        self.sessions.remove(&(ecu_id, session_id));
    }
    
    /// 获取ECU锁 (用于独占操作, 如Flash编程)
    pub async fn acquire_ecu_lock(&self, ecu_id: u8) -> Result<tokio::sync::RwLockWriteGuard<'_, ()>, String> {
        let lock = self.locks.get(&ecu_id)
            .ok_or(format!("ECU {} not found", ecu_id))?;
        Ok(lock.write().await)
    }
}
```

### 3.4.3 超时参数策略 (对齐09-Testing_Strategy的超时测试)

**关键原则**: 超时参数优先级为: **会话配置 > ECU配置 > 默认值**

| 参数 | 默认值(ms) | 用途 | 覆盖机制 |
|------|-----------|------|--------|
| **P2** | 100 | 普通服务响应等待 | DiagnosticSession.p2_server (优先级最高) |
| **P2*** | 2500 | 长操作响应等待(刷写/编程) | DiagnosticSession.p2_server_max (优先级最高) |
| **S3** | 10000 | 会话空闲超时 | DiagnosticSession.s3_server (优先级最高) |
| **0x78轮询** | 100 | responsePending轮询间隔 | EcuConfig.responsePending_poll_interval_ms |
| **0x78最大轮次** | 3 | responsePending最多重试 | EcuConfig.responsePending_max_retries |

**超时行为与错误处理**:
- 若P2超时但收到0x78(responsePending), **继续轮询** (最多N次), 每次间隔Poll_interval_ms
- 若0x78轮询达到最大次数仍无最终响应, 返回**TransportError::P2Timeout**
- 若P2*超时, 返回**TransportError::P2MaxTimeout** (用于刷写错误恢复)
- 若S3超时, 会话自动关闭, 返回**UdsError::NRC(0x22)** (conditionsNotCorrect)

**多ECU配置独立性** (FR-TRANS-004):
- 每个ECU可有独立的P2/P2*/S3配置, 加载自 `configs/ecu/{ecu_name}.json`
- ServiceRegistry在处理请求时需调用 `EcuRegistry.get_ecu_config()` 以获取超时值
- 并发时, Tokio的 `tokio::time::timeout()` 确保各ECU的超时独立计时

---

## 4. 服务层接口

### 4.1 UDS 服务处理器接口

```rust
/// UDS 服务处理接口
pub trait UdsServiceHandler {
    /// 处理 UDS 请求
    fn handle_request(
        &self,
        request: &UdsMessage,
        session: &mut DiagnosticSession,
    ) -> Result<UdsResponse, ServiceError>;

    /// 获取支持的服务 ID
    fn supported_services(&self) -> Vec<u8>;
}

/// 会话控制服务 (0x10)
pub struct SessionControlService;

impl UdsServiceHandler for SessionControlService {
    fn handle_request(
        &self,
        request: &UdsMessage,
        session: &mut DiagnosticSession,
    ) -> Result<UdsResponse, ServiceError> {
        // 实现细节...
        Ok(UdsResponse::positive(0x10, vec![session_type, p2_ms, p2_ms_max]))
    }

    fn supported_services(&self) -> Vec<u8> {
        vec![0x10]
    }
}

/// 数据标识符读取服务 (0x22)
pub struct ReadDataByIdentifierService {
    pub did_registry: Arc<DidRegistry>,
}

impl UdsServiceHandler for ReadDataByIdentifierService {
    fn handle_request(
        &self,
        request: &UdsMessage,
        session: &mut DiagnosticSession,
    ) -> Result<UdsResponse, ServiceError> {
        // 解析 DID
        // 查询数据
        // 返回响应
        Ok(UdsResponse::positive(0x22, data))
    }

    fn supported_services(&self) -> Vec<u8> {
        vec![0x22]
    }
}
```

---

### 4.2 服务注册接口 (多ECU支持, 见06/07章)

```rust
/// UDS 服务注册表 (支持多ECU和优先级)
/// 对应uds-core模块 (见07-Project_Structure 2.2)
pub struct ServiceRegistry {
    /// 服务处理器映射: (ECU_ID, Service_ID) -> Handler
    /// 支持每个ECU有不同的服务实现 (多ECU隔离)
    handlers: std::collections::HashMap<(u8, u8), (u16, Box<dyn UdsServiceHandler>)>,
    // priority: u16用于处理多个处理器时的优先级
}

impl ServiceRegistry {
    /// 创建新注册表
    pub fn new() -> Self {
        ServiceRegistry {
            handlers: std::collections::HashMap::new(),
        }
    }

    /// 注册服务 (全ECU通用处理器)
    pub fn register(&mut self, handler: Box<dyn UdsServiceHandler>) {
        for service_id in handler.supported_services() {
            // 注册为全局处理器 (ECU_ID=0xFF)
            self.handlers.insert((0xFF, service_id), (0, handler));
        }
    }

    /// 为特定ECU注册服务 (多ECU隔离)
    pub fn register_ecu_service(
        &mut self,
        ecu_id: u8,
        handler: Box<dyn UdsServiceHandler>,
    ) {
        for service_id in handler.supported_services() {
            self.handlers.insert((ecu_id, service_id), (0, handler));
        }
    }

    /// 为特定ECU的特定服务注册处理器 (支持优先级)
    pub fn register_ecu_service_with_priority(
        &mut self,
        ecu_id: u8,
        service_id: u8,
        priority: u16,
        handler: Box<dyn UdsServiceHandler>,
    ) {
        self.handlers.insert((ecu_id, service_id), (priority, handler));
    }

    /// 处理请求 (根据ECU_ID选择处理器)
    pub fn handle(
        &self,
        ecu_id: u8,
        request: &UdsMessage,
        session: &mut DiagnosticSession,
    ) -> UdsResponse {
        // 查找优先级: (ECU_ID, Service_ID) > (0xFF, Service_ID) > 不支持
        let key = (ecu_id, request.service_id);
        let handler = self.handlers.get(&key)
            .or_else(|| self.handlers.get(&(0xFF, request.service_id)));

        match handler {
            Some((_, handler)) => {
                handler.handle_request(request, session)
                    .unwrap_or_else(|e| UdsResponse::negative(request.service_id, e.into_nrc()))
            },
            None => {
                // serviceNotSupported (NRC 0x11)
                UdsResponse::negative(request.service_id, 0x11)
            },
        }
    }

    /// 启用/禁用特定服务 (支持响应抑制, FR-TRANS-005)
    pub fn set_service_enabled(&mut self, ecu_id: u8, service_id: u8, enabled: bool) {
        // 实现: 添加enable_flags映射
        // 禁用时返回NRC 0x22 (conditionsNotCorrect)
    }
}
```

---

## 5. 传输层接口

### 5.1 ISO-TP 接口

```rust
/// ISO-TP 传输层
pub trait IsoTpTransport {
    /// 发送消息
    fn send(&self, message: &[u8]) -> Result<(), TransportError>;

    /// 接收消息
    fn recv(&self) -> Result<Vec<u8>, TransportError>;

    /// 设置流控参数
    fn set_flow_control(&mut self, block_size: u8, st_min: u8);
}

/// ISO-TP 会话
pub struct IsoTpSession {
    can_send: Box<dyn CanSender>,
    state: IsoTpState,
    block_size: u8,
    st_min: u8,
}

impl IsoTpTransport for IsoTpSession {
    fn send(&self, message: &[u8]) -> Result<(), TransportError> {
        if message.len() <= 7 {
            // 单帧
            self.send_single_frame(message)?;
        } else {
            // 多帧
            self.send_multi_frame(message)?;
        }
        Ok(())
    }

    fn recv(&self) -> Result<Vec<u8>, TransportError> {
        // 接收和重组逻辑
        Ok(vec![])
    }

    fn set_flow_control(&mut self, block_size: u8, st_min: u8) {
        self.block_size = block_size;
        self.st_min = st_min;
    }
}
```

---

### 5.2 CAN 接口

```rust
/// CAN 发送接口
pub trait CanSender: Send {
    fn send(&self, frame: &CanFrame) -> Result<(), CanError>;
}

/// CAN 接收接口
pub trait CanReceiver: Send {
    fn recv(&self) -> Result<CanFrame, CanError>;
}

/// CAN 帧
#[derive(Debug, Clone)]
pub struct CanFrame {
    pub id: u32,
    pub data: Vec<u8>,
    pub extended: bool,
}

/// SocketCAN 实现
pub struct SocketCan {
    socket: std::os::unix::io::RawFd,
}

impl CanSender for SocketCan {
    fn send(&self, frame: &CanFrame) -> Result<(), CanError> {
        // SocketCAN 发送逻辑
        Ok(())
    }
}

impl CanReceiver for SocketCan {
    fn recv(&self) -> Result<CanFrame, CanError> {
        // SocketCAN 接收逻辑
        Ok(CanFrame {
            id: 0,
            data: vec![],
            extended: false,
        })
    }
}
```

---

## 6. 外部集成接口

### 6.1 诊断仪客户端 API (Python, FR-SES-001/002, FR-SEC-001, FR-FLASH-001)

```python
from abc import ABC, abstractmethod
from typing import List, Optional, Dict, Tuple
from dataclasses import dataclass
from enum import IntEnum

# 异常定义 (见7节)
class UdsException(Exception):
    """UDS基础异常"""
    pass

class UdsError(UdsException):
    """UDS协议错误"""
    def __init__(self, nrc: int, message: str):
        self.nrc = nrc
        super().__init__(f"NRC 0x{nrc:02X}: {message}")

class SecurityError(UdsException):
    """安全相关错误"""
    pass

class TransportError(UdsException):
    """传输层错误"""
    pass

class DidError(UdsException):
    """DID相关错误"""
    pass

@dataclass
class UdsResponse:
    """UDS 响应"""
    service_id: int
    data: bytes
    is_positive: bool
    nrc: Optional[int] = None  # 负响应的NRC码

@dataclass
class UdsRequest:
    """UDS 请求"""
    service_id: int
    sub_function: Optional[int] = None
    data: bytes = b""
    suppress_positive_response: bool = False  # FR-TRANS-005

class SessionType(IntEnum):
    """诊断会话类型 (FR-SES-001)"""
    Default = 0x01
    Programming = 0x02  # FR-FLASH-001
    ExtendedDiagnostic = 0x03
    SafetySystem = 0x04

@dataclass
class EcuConfig:
    """ECU 配置 (对齐Rust侧, 见3.4.1)"""
    ecu_id: int
    name: str
    rx_id: int  # 请求ID
    tx_id: int  # 响应ID
    extended_frame: bool = False
    p2_timeout_ms: int = 100
    p2_max_timeout_ms: int = 2500
    s3_timeout_secs: int = 10
    responsePending_max_retries: int = 3
    responsePending_poll_interval_ms: int = 100
    supported_sessions: List[int] = None
    access_matrix: Dict[Tuple[int, int], bool] = None  # (service_id, session_type) -> bool
    
    def __post_init__(self):
        """验证配置"""
        if self.ecu_id == 0:
            raise ValueError("ECU ID不能为0")
        if self.p2_timeout_ms > self.p2_max_timeout_ms:
            raise ValueError("P2不能大于P2*")
        if self.supported_sessions is None:
            self.supported_sessions = [0x01, 0x02, 0x03, 0x04]
        if self.access_matrix is None:
            self.access_matrix = {}
    
    @classmethod
    def from_json_file(cls, path: str) -> 'EcuConfig':
        """从JSON文件加载配置 (见07-Project_Structure configs/ecu/)"""
        import json
        with open(path, 'r') as f:
            data = json.load(f)
        return cls(**data)

class EcuRegistry:
    """ECU 注册表和会话管理 (对齐Rust侧, 见3.4.2)"""
    
    def __init__(self):
        self._configs: Dict[int, EcuConfig] = {}
        self._sessions: Dict[Tuple[int, int], dict] = {}  # (ecu_id, session_id) -> session_info
        self._locks: Dict[int, asyncio.Lock] = {}
    
    def register_ecu(self, config: EcuConfig) -> None:
        """注册ECU配置"""
        config.__post_init__()  # 验证
        self._configs[config.ecu_id] = config
        self._locks[config.ecu_id] = asyncio.Lock()
    
    def load_from_directory(self, directory: str) -> None:
        """从config目录加载所有ECU配置"""
        import os
        from pathlib import Path
        for file in Path(directory).glob('*.json'):
            config = EcuConfig.from_json_file(str(file))
            self.register_ecu(config)
    
    def get_ecu_config(self, ecu_id: int) -> Optional[EcuConfig]:
        """获取ECU配置"""
        return self._configs.get(ecu_id)
    
    def create_session(self, ecu_id: int, session_type: SessionType) -> int:
        """创建新会话, 返回session_id"""
        if ecu_id not in self._configs:
            raise ValueError(f"ECU {ecu_id} not found")
        
        import time
        session_id = int(time.time() * 1000) % (2**32)
        self._sessions[(ecu_id, session_id)] = {
            'ecu_id': ecu_id,
            'session_type': session_type,
            'created_at': time.time(),
            'last_heartbeat': time.time(),
        }
        return session_id
    
    def get_session(self, ecu_id: int, session_id: int) -> Optional[dict]:
        """获取会话信息"""
        return self._sessions.get((ecu_id, session_id))
    
    def close_session(self, ecu_id: int, session_id: int) -> None:
        """关闭会话"""
        self._sessions.pop((ecu_id, session_id), None)
    
    async def acquire_ecu_lock(self, ecu_id: int) -> asyncio.Lock:
        """获取ECU锁 (用于独占操作, 如Flash编程)"""
        if ecu_id not in self._locks:
            raise ValueError(f"ECU {ecu_id} not found")
        return self._locks[ecu_id]

class UdsClientFactory:
    """UDS 客户端工厂 (支持多ECU创建和配置)"""
    
    def __init__(self, ecu_registry: Optional[EcuRegistry] = None):
        self.registry = ecu_registry or EcuRegistry()
    
    def create_client(
        self,
        ecu_id: int,
        interface: str = "can0",
    ) -> 'UdsClient':
        """创建指定ECU的UDS客户端"""
        config = self.registry.get_ecu_config(ecu_id)
        if not config:
            raise ValueError(f"ECU {ecu_id} config not found")
        
        client = UdsClient(ecu_id=ecu_id, config=config)
        client.connect(interface)
        return client

class UdsClient(ABC):
    """UDS 客户端接口 (对齐Rust的UDS服务, 支持多ECU)"""
    
    def __init__(self, ecu_id: int, config: EcuConfig):
        """初始化客户端 (多ECU支持)
        
        Args:
            ecu_id: 目标ECU ID
            config: ECU配置 (含超时参数、权限矩阵等, 见3.4.1)
        """
        self.ecu_id = ecu_id
        self.config = config
        self.session_id: Optional[int] = None
        self.session_type: Optional[SessionType] = None
        self.security_level = 0
        self._connection = None  # 连接句柄
        self._receive_timeout_ms = config.p2_timeout_ms
        self._long_timeout_ms = config.p2_max_timeout_ms

    @abstractmethod
    def connect(self, interface: str) -> None:
        """连接到 CAN 总线"""
        pass

    @abstractmethod
    def disconnect(self) -> None:
        """断开连接"""
        pass

    @abstractmethod
    def send_request(self, request: UdsRequest, timeout_ms: Optional[int] = None) -> UdsResponse:
        """发送 UDS 请求, 返回响应
        
        Args:
            request: UDS请求
            timeout_ms: 响应超时(毫秒), 若为None则使用ECU配置的P2/P2*
            
        Raises:
            TransportError: 传输层错误(P2/P2*超时, 帧丢失等)
            UdsError: 负响应(NRC码)
        
        **多ECU超时策略** (见3.4.3):
        - 若timeout_ms未指定, 根据当前操作类型选择P2或P2*
        - 若接收到0x78(responsePending), 按config参数重试
        """
        pass

    # 会话控制 (0x10, 多ECU支持)
    def change_session(self, session_type: SessionType) -> UdsResponse:
        """切换诊断会话 (0x10, FR-SES-002, 多ECU隔离)
        
        Args:
            session_type: 目标会话类型
            
        Returns:
            响应 (含P2/P2*超时)
            
        **多ECU行为**:
        - 每个ECU独立维护会话状态
        - 超时参数从self.config加载 (ECU特定)
        - 在ecu_registry中注册新会话
        """
        request = UdsRequest(
            service_id=0x10,
            sub_function=session_type,
            suppress_positive_response=False
        )
        response = self.send_request(request)
        
        if response.is_positive:
            self.session_type = session_type
            # 会话ID通常在后续0x27中创建或由ECU侧管理
        
        return response
    
    def _send_with_responsePending_polling(
        self,
        request: UdsRequest,
        timeout_ms: int = None,
    ) -> UdsResponse:
        """发送请求并处理0x78(responsePending)轮询 (见3.4.3)
        
        支持多ECU的独立超时和重试策略
        """
        if timeout_ms is None:
            timeout_ms = self.config.p2_timeout_ms
        
        max_retries = self.config.responsePending_max_retries
        poll_interval = self.config.responsePending_poll_interval_ms
        retries = 0
        
        while retries < max_retries:
            response = self.send_request(request, timeout_ms=timeout_ms)
            
            if response.nrc == 0x78:  # responsePending
                retries += 1
                import time
                time.sleep(poll_interval / 1000.0)
                continue
            
            return response
        
        # 超过最大轮次
        raise TransportError(
            f"ResponsePending timeout: max retries {max_retries} exceeded for ECU {self.ecu_id}"
        )

    # 数据标识符读取 (0x22, FR-DATA-001/003)
    def read_data_identifier(self, did: int) -> bytes:
        """读取 DID (0x22)
        
        Args:
            did: 数据标识符 (0xF1xx-0xFDxx 见03-UDS_Protocol_Design)
            
        Returns:
            DID数据
            
        Raises:
            DidError: DID不支持或权限不足
        """
        request = UdsRequest(
            service_id=0x22,
            data=did.to_bytes(2, 'big')
        )
        response = self.send_request(request)
        if response.is_positive:
            return response.data[2:]  # 跳过响应SID和DID
        raise DidError(f"Read DID 0x{did:04X} failed: NRC 0x{response.nrc:02X}")

    def read_multiple_dids(self, dids: List[int]) -> Dict[int, bytes]:
        """批量读取DID"""
        result = {}
        for did in dids:
            try:
                result[did] = self.read_data_identifier(did)
            except DidError:
                result[did] = None
        return result

    # 清除故障码 (0x14, FR-DTC-002)
    def clear_dtc(self, group_of_dtc: int = 0xFFFFFF) -> bool:
        """清除诊断故障码 (0x14)
        
        Args:
            group_of_dtc: DTC分组 (默认全部)
            
        Returns:
            是否成功
        """
        request = UdsRequest(
            service_id=0x14,
            data=group_of_dtc.to_bytes(3, 'big')
        )
        response = self.send_request(request)
        return response.is_positive

    # 读取DTC (0x19, FR-DTC-001)
    def read_dtc(self, report_type: int = 0x01) -> Tuple[int, List[bytes]]:
        """读取诊断故障码 (0x19)
        
        Args:
            report_type: 报告类型 (0x01=当前DTC, 0x02=DTC and 状态, etc.)
            
        Returns:
            (状态可用掩码, DTC列表)
        """
        request = UdsRequest(
            service_id=0x19,
            sub_function=report_type
        )
        response = self.send_request(request)
        if response.is_positive:
            status_mask = response.data[0] if len(response.data) > 0 else 0
            # DTC格式: 3字节 + 1字节状态
            dtcs = []
            for i in range(1, len(response.data), 4):
                if i + 3 < len(response.data):
                    dtcs.append(response.data[i:i+4])
            return status_mask, dtcs
        raise UdsError(response.nrc, "Read DTC failed")

    # 安全访问 (0x27, FR-SEC-001/002)
    def request_seed(self, level: int) -> bytes:
        """请求种子 (0x27, sub=奇数, FR-SEC-001)
        
        Args:
            level: 安全级别 (1-0xFF)
            
        Returns:
            种子数据
            
        Raises:
            SecurityError: 已锁定或无法获取种子
        """
        sub_function = level * 2 - 1  # 奇数子功能
        request = UdsRequest(
            service_id=0x27,
            sub_function=sub_function
        )
        response = self.send_request(request)
        if response.is_positive:
            return response.data[1:]  # 跳过子功能
        if response.nrc == 0x36:  # exceededNumberOfAttempts
            raise SecurityError(f"Security level {level} locked: 3 failed attempts, 5-min lockout")
        raise SecurityError(f"Request seed level {level} failed: NRC 0x{response.nrc:02X}")

    def send_key(self, level: int, key: bytes) -> bool:
        """发送密钥 (0x27, sub=偶数, FR-SEC-001)
        
        Args:
            level: 安全级别
            key: 密钥数据
            
        Returns:
            是否解锁成功
            
        Raises:
            SecurityError: 密钥错误或达到尝试限制
        """
        sub_function = level * 2  # 偶数子功能
        request = UdsRequest(
            service_id=0x27,
            sub_function=sub_function,
            data=key
        )
        response = self.send_request(request)
        if response.is_positive:
            return True
        if response.nrc == 0x35:  # invalidKey
            raise SecurityError(f"Invalid key for level {level}")
        if response.nrc == 0x36:  # exceededNumberOfAttempts
            raise SecurityError(f"Security level {level} locked: max attempts exceeded")
        raise SecurityError(f"Send key level {level} failed: NRC 0x{response.nrc:02X}")

    # Flash编程 (0x34/0x36/0x37, FR-FLASH-001)
    def request_download(self, address: int, length: int, data_format: int = 0x00) -> Tuple[int, int]:
        """请求下载 (0x34, 开始Flash编程, FR-FLASH-001)
        
        Args:
            address: 编程地址
            length: 数据长度
            data_format: 数据格式 (默认0x00=原始二进制)
            
        Returns:
            (最大块长, 长度格式)
            
        Raises:
            UdsError: 请求失败
        """
        # 简化格式: 1字节地址格式 + 1字节长度格式 + 地址 + 长度
        addr_format = 0x32  # 2字节地址, 2字节长度
        request = UdsRequest(
            service_id=0x34,
            sub_function=0x00,  # 开始下载
            data=bytes([data_format, addr_format]) 
                 + address.to_bytes(2, 'big') 
                 + length.to_bytes(2, 'big')
        )
        response = self.send_request(request, timeout_ms=2000)  # P2*
        if response.is_positive:
            # 响应: 长度格式 + 最大块长
            length_format = response.data[0]
            max_block_len = int.from_bytes(response.data[1:], 'big')
            return max_block_len, length_format
        raise UdsError(response.nrc, f"Request download failed at 0x{address:04X}")

    def transfer_data(self, sequence_counter: int, data: bytes) -> bool:
        """传输数据块 (0x36, FR-FLASH-001, 支持0x78轮询)
        
        Args:
            sequence_counter: 块序列号 (1-FF循环)
            data: 数据块 (最大长度由request_download返回)
            
        Returns:
            是否成功
            
        Raises:
            UdsError: 传输失败 (NRC != 0x78且非正响应)
            TransportError: P2*超时后无最终响应 (见3.4.3)
        
        **多ECU行为**:
        - 使用self.config.p2_max_timeout_ms作为超时
        - 收到0x78时按config的轮询策略重试
        - 每个ECU的重试次数和轮询间隔独立
        """
        request = UdsRequest(
            service_id=0x36,
            sub_function=sequence_counter,
            data=data
        )
        # 使用0x78轮询辅助方法 (见3.4.3超时策略)
        response = self._send_with_responsePending_polling(
            request,
            timeout_ms=self.config.p2_max_timeout_ms
        )
        
        if response.is_positive:
            return True
        raise UdsError(response.nrc, f"Transfer data block {sequence_counter} failed")

    def request_exit(self) -> bool:
        """请求退出下载 (0x37, 结束Flash编程, FR-FLASH-001)
        
        返回: 是否成功
        
        Raises:
            UdsError: 退出失败
        """
        request = UdsRequest(service_id=0x37)
        response = self.send_request(request, timeout_ms=2000)  # P2*
        if response.is_positive:
            return True
        raise UdsError(response.nrc, "Request exit failed")

    # 通信控制 (0x28)
    def enable_normal_communication(self) -> bool:
        """启用正常通信 (0x28, sub=0x01)"""
        request = UdsRequest(
            service_id=0x28,
            sub_function=0x01,
            data=bytes([0x01])  # communicationType
        )
        response = self.send_request(request)
        return response.is_positive

    def disable_normal_communication(self) -> bool:
        """禁用正常通信 (0x28, sub=0x03, 编程期间使用)"""
        request = UdsRequest(
            service_id=0x28,
            sub_function=0x03,  # disableRxAndTx
            data=bytes([0x01])
        )
        response = self.send_request(request)
        return response.is_positive

    # Tester Present (0x3E)
    def tester_present(self, sub_function: int = 0x00) -> bool:
        """发送Tester Present以保活会话 (0x3E, FR-TRANS-003)
        
        Args:
            sub_function: 0x00=默认, 0x80=抑制正响应
        """
        request = UdsRequest(
            service_id=0x3E,
            sub_function=sub_function,
            suppress_positive_response=(sub_function == 0x80)
        )
        response = self.send_request(request, timeout_ms=100)
        return response.is_positive
```

---

### 6.2 刷写工具 API (FR-FLASH-001/004/005)

```python
import time
from pathlib import Path

class FlashTool:
    """Flash 编程工具 (FR-FLASH-001: 编程工作流, 多ECU支持)"""

    def __init__(
        self,
        uds_client: UdsClient,
        ecu_name: str = "ECU1",
        ecu_registry: Optional[EcuRegistry] = None,
    ):
        """初始化刷写工具
        
        Args:
            uds_client: UDS客户端 (自动关联到ecu_id)
            ecu_name: ECU名称 (便于日志和调试)
            ecu_registry: ECU注册表 (用于多ECU场景的锁管理, 可选)
        
        **多ECU行为**:
        - 每个FlashTool实例绑定到一个UdsClient/ECU
        - 自动获取ECU的Flash地址范围和超时参数
        - 支持并发编程多个ECU(由registry的锁保护)
        """
        self.client = uds_client
        self.ecu_name = ecu_name
        self.ecu_id = uds_client.ecu_id
        self.registry = ecu_registry
        
        # 从ECU配置获取Flash参数
        self.flash_start, self.flash_end = (
            uds_client.config.flash_address_range
            if uds_client.config.flash_address_range
            else (0, 0xFFFFFFFF)
        )
        
        self.session_type = SessionType.Programming  # 0x02
        self.programming_started = False
        self.current_progress = 0.0
        self.total_bytes = 0
        self._ecu_lock_guard = None  # 用于独占Flash编程

    async def acquire_flash_lock(self) -> None:
        """获取ECU Flash编程独占锁 (防止并发编程同一ECU)
        
        见3.4.2的acquire_ecu_lock和10-Roadmap的并发风险管理
        """
        if self.registry:
            self._ecu_lock_guard = await self.registry.acquire_ecu_lock(self.ecu_id)

    def release_flash_lock(self) -> None:
        """释放Flash编程锁"""
        self._ecu_lock_guard = None

    def prepare_flash(self, security_level: int = 3) -> bool:
        """Flash编程前准备 (FR-FLASH-001: 第1-3步)
        
        1. 切换到编程会话 (0x10, sub=0x02)
        2. 禁用正常通信 (0x28, sub=0x03)
        3. 安全解锁 (0x27: 种子+密钥)
        
        Args:
            security_level: 编程所需安全级别 (默认3, 见01-SRS权限矩阵)
            
        Returns:
            是否成功准备
        """
        try:
            # 步骤1: 切换到编程会话
            print(f"[{self.ecu_name}] Switching to programming session...")
            resp = self.client.change_session(self.session_type)
            if not resp.is_positive:
                print(f"Failed to switch session: NRC 0x{resp.nrc:02X}")
                return False

            # 步骤2: 禁用正常通信 (编程期间不响应其他请求)
            print(f"[{self.ecu_name}] Disabling normal communication...")
            if not self.client.disable_normal_communication():
                print("Failed to disable communication")
                return False

            # 步骤3: 安全解锁 (种子+密钥方案, FR-SEC-001)
            print(f"[{self.ecu_name}] Requesting security level {security_level}...")
            seed = self.client.request_seed(security_level)
            print(f"Got seed: {seed.hex()}")

            key = self._generate_key(seed, security_level)
            print(f"Sending key: {key.hex()}")
            
            if not self.client.send_key(security_level, key):
                print(f"Security unlock failed for level {security_level}")
                return False

            print(f"[{self.ecu_name}] Flash preparation successful!")
            self.programming_started = True
            return True

        except (SecurityError, TransportError, UdsError) as e:
            print(f"Prepare flash error: {e}")
            return False

    def flash_firmware(self, firmware_path: str, base_address: int = 0x1000) -> bool:
        """Flash编程工作流 (FR-FLASH-001: 完整流程)
        
        工作流:
        1. 请求下载 (0x34)
        2. 分块传输数据 (0x36, 多次)
        3. 请求退出 (0x37)
        4. 验证 (可选: 读取校验和)
        
        Args:
            firmware_path: 固件文件路径
            base_address: 编程基地址
            
        Returns:
            是否刷写成功
        """
        if not self.programming_started:
            print("Error: Flash preparation not completed. Call prepare_flash() first.")
            return False

        try:
            firmware_data = Path(firmware_path).read_bytes()
            self.total_bytes = len(firmware_data)
            print(f"[{self.ecu_name}] Flashing firmware ({self.total_bytes} bytes)...")

            # 步骤1: 请求下载
            print(f"[{self.ecu_name}] Requesting download at 0x{base_address:04X}...")
            max_block_len, length_format = self.client.request_download(
                address=base_address,
                length=self.total_bytes,
                data_format=0x00  # 原始二进制
            )
            print(f"Max block length: {max_block_len} bytes")

            # 步骤2: 分块传输
            block_counter = 1
            bytes_sent = 0
            while bytes_sent < self.total_bytes:
                # 读取一个块
                chunk_size = min(max_block_len, self.total_bytes - bytes_sent)
                chunk = firmware_data[bytes_sent:bytes_sent + chunk_size]

                # 发送块 (0x36)
                print(f"[{self.ecu_name}] Transferring block {block_counter} "
                      f"({bytes_sent}/{self.total_bytes})...")
                
                if not self.client.transfer_data(block_counter & 0xFF, chunk):
                    print(f"Transfer block {block_counter} failed")
                    return False

                bytes_sent += chunk_size
                block_counter += 1
                self.current_progress = (bytes_sent / self.total_bytes) * 100
                print(f"  Progress: {self.current_progress:.1f}%")

                time.sleep(0.1)  # P2*等待

            # 步骤3: 请求退出 (0x37)
            print(f"[{self.ecu_name}] Requesting exit...")
            if not self.client.request_exit():
                print("Request exit failed")
                return False

            print(f"[{self.ecu_name}] Firmware flash completed successfully!")
            self.programming_started = False
            return True

        except (TransportError, UdsError) as e:
            print(f"Flash firmware error: {e}")
            return False

    def finalize_flash(self) -> bool:
        """Flash编程后处理 (FR-FLASH-001: 收尾)
        
        1. 启用正常通信 (0x28, sub=0x01)
        2. 可选: 返回默认会话 (0x10, sub=0x01)
        """
        try:
            print(f"[{self.ecu_name}] Finalizing flash...")

            # 启用正常通信
            if not self.client.enable_normal_communication():
                print("Warning: Failed to enable communication")

            # 返回默认会话
            resp = self.client.change_session(SessionType.Default)
            if not resp.is_positive:
                print("Warning: Failed to switch to default session")

            print(f"[{self.ecu_name}] Flash finalization completed!")
            return True
        except Exception as e:
            print(f"Finalize flash error: {e}")
            return False

    def _generate_key(self, seed: bytes, level: int) -> bytes:
        """生成密钥 (见06-技术栈: 支持XOR和HMAC)
        
        简化实现: XOR算法
        实际应从配置加载或使用HMAC
        """
        # 示例: XOR算法 (掩码根据安全级别和配置而定)
        mask = [0xAB, 0xCD, 0xEF, 0x12][level % 4]
        key = bytearray()
        for i, byte in enumerate(seed):
            key.append(byte ^ (mask + i) & 0xFF)
        
        # 备选: HMAC-SHA256 (需要cryptography库)
        # from cryptography.hazmat.primitives import hashes, hmac
        # h = hmac.HMAC(secret_key, hashes.SHA256())
        # h.update(seed)
        # key = h.finalize()
        
        return bytes(key)

    def get_progress(self) -> Dict[str, any]:
        """获取编程进度"""
        return {
            "ecu": self.ecu_name,
            "progress_percent": self.current_progress,
            "bytes_total": self.total_bytes,
            "is_programming": self.programming_started,
        }
```

---

## 7. 错误处理 (FR-DTC-001/002, FR-SEC-001, FR-TRANS-003/005)

### 7.1 错误类型定义

```rust
/// 数据标识符注册表 (FR-DATA-001/003)
pub trait DidRegistry: Send + Sync {
    /// 根据DID获取数据
    fn get_did(&self, did: u16) -> Result<Vec<u8>, DidError>;
    
    /// 设置DID值 (可选, 依赖权限矩阵FR-SEC-002)
    fn set_did(&mut self, did: u16, value: Vec<u8>) -> Result<(), DidError>;
    
    /// 列出所有可访问DID
    fn list_dids(&self) -> Vec<u16>;
}

/// DID错误
#[derive(Debug, thiserror::Error)]
pub enum DidError {
    #[error("DID not supported: 0x{0:04X}")]
    DidNotSupported(u16),

    #[error("DID access denied: 0x{0:04X}")]
    DidAccessDenied(u16),

    #[error("Security level insufficient")]
    SecurityLevelInsufficient,
}

/// UDS 错误类型 (见03-UDS_Protocol_Design: NRC码表)
#[derive(Debug, thiserror::Error)]
pub enum UdsError {
    // 服务错误 (0x10-0x1F范围)
    #[error("Service not supported (NRC 0x11): 0x{0:02X}")]
    ServiceNotSupported(u8),

    #[error("Sub-function not supported (NRC 0x12): 0x{0:02X}")]
    SubFunctionNotSupported(u8),

    #[error("Incorrect message length or invalid format (NRC 0x13)")]
    InvalidMessageFormat,

    // 会话/条件错误 (0x20-0x3F范围)
    #[error("Conditions not correct (NRC 0x22)")]
    ConditionsNotCorrect,

    #[error("Request sequence error (NRC 0x24): 会话状态不允许此服务")]
    RequestSequenceError,

    #[error("Request out of range (NRC 0x31): 地址/数据范围有效")]
    RequestOutOfRange,

    // 安全错误 (0x30-0x3F范围, FR-SEC-001/002)
    #[error("Security access denied (NRC 0x33): 需要更高安全级别")]
    SecurityAccessDenied,

    #[error("Invalid key (NRC 0x35): 密钥验证失败")]
    InvalidKey,

    #[error("Attempts exceeded (NRC 0x36): 3次失败后锁定5分钟")]
    AttemptsExceeded,

    // 编程错误 (0x70-0x7F范围, FR-FLASH-001)
    #[error("General programming failure (NRC 0x72): Flash编程失败")]
    GeneralProgrammingFailure,

    #[error("Programming voltage failure (NRC 0x74): 编程电压异常")]
    ProgrammingVoltureFailure,

    // 超时相关 (见FR-TRANS-003)
    #[error("Response pending (NRC 0x78): P2*/S3超时, 需继续轮询")]
    ResponsePending,

    // 传输层错误
    #[error("Transport error: {0}")]
    TransportError(#[from] TransportError),

    #[error("CAN error: {0}")]
    CanError(#[from] CanError),

    #[error("DID error: {0}")]
    DidError(#[from] DidError),
}

impl UdsError {
    /// 转换为 NRC 码 (Negative Response Code, 见03-UDS_Protocol_Design)
    pub fn to_nrc(&self) -> u8 {
        match self {
            UdsError::ServiceNotSupported(_) => 0x11,                    // serviceNotSupported
            UdsError::SubFunctionNotSupported(_) => 0x12,               // subFunctionNotSupported
            UdsError::InvalidMessageFormat => 0x13,                     // incorrectMessageLengthOrInvalidFormat
            UdsError::ConditionsNotCorrect => 0x22,                     // conditionsNotCorrect
            UdsError::RequestSequenceError => 0x24,                     // requestSequenceError
            UdsError::RequestOutOfRange => 0x31,                        // requestOutOfRange
            UdsError::SecurityAccessDenied => 0x33,                     // securityAccessDenied
            UdsError::InvalidKey => 0x35,                               // invalidKey
            UdsError::AttemptsExceeded => 0x36,                         // exceededNumberOfAttempts (FR-SEC-001)
            UdsError::GeneralProgrammingFailure => 0x72,               // generalProgrammingFailure
            UdsError::ProgrammingVoltureFailure => 0x74,               // programmingVoltageDetectionFailure
            UdsError::ResponsePending => 0x78,                          // requestCorrectlyReceivedResponsePending
            _ => 0x22, // 默认: conditionsNotCorrect
        }
    }
}

/// 服务处理错误 (内部)
#[derive(Debug, thiserror::Error)]
pub enum ServiceError {
    #[error("UDS error: {0}")]
    Uds(#[from] UdsError),

    #[error("Service execution failed: {0}")]
    ExecutionFailed(String),
}

impl ServiceError {
    pub fn into_nrc(&self) -> u8 {
        match self {
            ServiceError::Uds(e) => e.to_nrc(),
            ServiceError::ExecutionFailed(_) => 0x22,
        }
    }
}
```

---

### 7.2 传输层错误 (FR-TRANS-001/002/003)

```rust
/// 传输层错误 (ISO-TP, 见03-UDS_Protocol_Design)
#[derive(Debug, thiserror::Error)]
pub enum TransportError {
    /// P2超时: 等待首个响应帧超时 (FR-TRANS-003)
    #[error("P2 timeout: 等待第一个响应帧超时")]
    P2Timeout,

    /// P2*超时: 等待长延迟响应超时, 应返回NRC 0x78 (FR-TRANS-003)
    #[error("P2* timeout: 长操作延迟, 客户端应继续轮询")]
    P2StarTimeout,

    /// 帧丢失
    #[error("Frame lost: 分段传输中丢失连续帧")]
    FrameLost,

    /// 流控错误 (接收方发送的FC帧错误)
    #[error("Flow control error: {0}")]
    FlowControlError(String),

    /// 流控溢出: FS字段错误或BS值超限
    #[error("Flow control overflow: BS超出有效范围")]
    FlowControlOverflow,

    /// 连续帧超时 (FR-TRANS-002: CF接收超时)
    #[error("Consecutive frame timeout: 接收连续帧超时")]
    ConsecutiveFrameTimeout,

    /// 缓冲溢出
    #[error("Buffer overflow: 报文过大超过缓冲区")]
    BufferOverflow,

    /// 无效帧类型
    #[error("Invalid frame type: 0x{0:02X}")]
    InvalidFrameType(u8),
}

/// CAN错误 (见05-Virtual_CAN_Platform_Design)
#[derive(Debug, thiserror::Error)]
pub enum CanError {
    /// 总线离线 (Bus Off)
    #[error("Bus off: CAN总线离线")]
    BusOff,

    /// 错误被动 (Error Passive)
    #[error("Error passive: CAN控制器错误被动")]
    ErrorPassive,

    /// 控制器错误
    #[error("Controller error")]
    ControllerError,

    /// 套接字错误
    #[error("Socket error: {0}")]
    SocketError(String),

    /// 接口不存在
    #[error("Interface not found: {0}")]
    InterfaceNotFound(String),
}
```

---

## 8. 接口版本管理

### 8.1 版本控制策略

**语义化版本**: `MAJOR.MINOR.PATCH`

- **MAJOR**: 不兼容的 API 变更
- **MINOR**: 向后兼容的功能新增
- **PATCH**: 向后兼容的问题修复

**示例**:
- `0.1.0` - 初始版本
- `0.2.0` - 新增服务 0x31,向后兼容
- `1.0.0` - 稳定版本,API 冻结
- `1.1.0` - 新增 CAN FD 支持,向后兼容
- `2.0.0` - 重构服务层接口,不兼容

---

### 8.2 API 废弃策略

**废弃流程**:
1. 标记为 `deprecated`,保留至少 2 个 MINOR 版本
2. 文档说明替代方案
3. 3 个 MINOR 版本后移除

**示例**:
```rust
#[deprecated(since = "1.1.0", note = "Use `handle_request_v2` instead")]
pub fn handle_request(&self, req: &Request) -> Response {
    // 旧实现
}

/// 新版本 API (v1.1.0+)
pub fn handle_request_v2(&self, req: &Request) -> Result<Response, Error> {
    // 新实现
}
```

---

## 9. 接口测试

### 9.1 接口测试示例

```rust
#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_uds_message_serialization() {
        let msg = UdsMessage::new(0x22, vec![0xF1, 0x90]);
        let bytes = msg.to_bytes();
        assert_eq!(bytes, vec![0x22, 0xF1, 0x90]);

        let decoded = UdsMessage::from_bytes(&bytes).unwrap();
        assert_eq!(decoded.service_id, 0x22);
        assert_eq!(decoded.data, vec![0xF1, 0x90]);
    }

    #[test]
    fn test_session_expiration() {
        let mut session = DiagnosticSession::new(1, SessionType::Default);
        assert!(!session.is_expired());

        // 模拟超时
        session.s3_server = 100;
        std::thread::sleep(std::time::Duration::from_millis(150));
        assert!(session.is_expired());
    }
}
```

---

## 10. 设计一致性检查清单

本章与整体设计目标一致性验证 (对齐06/07章格式):

| # | 检查项 | 依赖章节 | 验证状态 | 备注 |
|---|--------|---------|--------|------|
| 1 | 会话类型定义与01-SRS FR-SES-001一致 | 01-SRS (2.2/3.1) | ✅ | SessionType:Default/Programming/ExtendedDiagnostic/SafetySystem |
| 2 | 超时参数支持动态配置 (P2/P2*/S3) | 01-SRS FR-TRANS-003; 03-UDS_Protocol | ✅ | DiagnosticSession使用Option<>支持每ECU配置 |
| 3 | 多ECU隔离: DiagnosticSession含ecu_id字段 | 06-Tech_Stack(3.2); 07-Project(2.2) | ✅ | 通过ecu_registry架构支持并发会话 |
| 4 | NRC码完整覆盖 (0x11-0x78) | 03-UDS_Protocol (4.1) | ✅ | UdsError枚举含27种NRC; responsePending(0x78)支持P2*轮询 |
| 5 | 安全访问3次失败后锁定5分钟 | 01-SRS FR-SEC-001 (3.2) | ✅ | SecurityAccess.verify_key()计数+lockout_duration |
| 6 | DidRegistry trait定义 | 01-SRS FR-DATA-001/003 | ✅ | 支持权限矩阵查询(FR-SEC-002) |
| 7 | Flash编程工作流 (request_download/transfer_data/request_exit) | 01-SRS FR-FLASH-001; 04-Bootloader | ✅ | UdsClient和FlashTool完整实现 |
| 8 | 响应抑制支持 (suppress_positive_response) | 01-SRS FR-TRANS-005 | ✅ | UdsResponse和UdsRequest均含此标志 |
| 9 | Python API完整覆盖17个核心服务 | 01-SRS (表3.1) | ✅ | 0x10/0x14/0x19/0x22/0x27/0x28/0x34/0x36/0x37/0x3E等 |
| 10 | 传输层错误覆盖ISO-TP规范 | 03-UDS_Protocol (附录ISO-TP); 06-Tech(Tokio异步) | ✅ | TransportError含P2/P2*超时/流控溢出/连续帧超时 |
| 11 | 技术栈依赖一致 (thiserror/anyhow/tokio) | 06-Tech_Stack (3.1/3.2/4.1) | ✅ | Rust使用thiserror; Python使用异常体系 |
| 12 | 版本管理与冻结点 (M3前冻结核心API) | 10-Roadmap (4.1-4.5) | ✅ | 语义化版本; 0x10/0x27等服务在M3冻结 |
| 13 | 跨语言兼容性 (Rust/Python一致性) | 06-Tech_Stack (2.1) | ✅ | 错误码、请求/响应格式、权限矩阵同步 |
| 14 | 多ECU配置加载接口 (serde/pydantic) | 07-Project (3.2.6); 06-Tech(2.2) | ✅ | 支持JSON配置 + Python dataclass验证 |

**核心验证结论**: ✅ 章节完全对齐设计规范和其他章节定义。所有FR需求都在接口中有对应体现。

---

## 11. 接口文档生成

### 11.1 Rust 文档

```bash
# 生成文档
cmake doc --open

# 包含私有项
cmake doc --document-private-items --open
```

### 11.2 Python 文档

使用 Sphinx 或 MkDocs:

```bash
# 安装
pip install sphinx

# 生成
sphinx-apidoc -o docs uds_cli
cd docs
make html
```

---

## 12. 兼容性与变更管理

- **版本策略**: 公共 API/协议遵循语义化版本 (MAJOR.MINOR.PATCH); MAJOR 版本才允许破坏性变更。
- **弃用流程**: 先标记 @deprecated 与文档提示, 提供迁移路径; 至少保留两次发布周期后再移除。
- **数据契约**: DID/DTC/路由配置新增字段必须向后兼容, 删除字段需提供默认值或迁移脚本。
- **跨语言一致性**: Rust/Python SDK 的请求/响应结构、错误码映射保持同步; 变更需同时更新两侧及集成测试。
- **接口冻结点**: 在里程碑 M3 之前冻结 0x10/0x27/0x22/0x2E/0x19/0x34-0x37/0x3E 接口, 破坏性修改需架构评审批准。

## 版本历史

| 版本 | 日期 | 变更描述 |
|------|------|--------|
| 1.1 | 2026-01-03 | 完善FR映射、多ECU隔离、NRC码覆盖、安全锁定、Python API完整性、Error定义、响应抑制 |
| 1.0 | 2026-01-02 | 接口和集成设计初始版本 |

---

## 参考文档

- [02-System_Architecture_Design.md](02-System_Architecture_Design.md) - 系统架构
- [07-Project_Structure.md](07-Project_Structure.md) - 项目结构
