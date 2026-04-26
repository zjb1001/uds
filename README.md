# UDS Diagnostic Simulation System

[![CI](https://github.com/zjb1001/uds/actions/workflows/ci.yml/badge.svg)](https://github.com/zjb1001/uds/actions/workflows/ci.yml)
[![Release](https://github.com/zjb1001/uds/actions/workflows/release.yml/badge.svg)](https://github.com/zjb1001/uds/actions/workflows/release.yml)

A complete **UDS (Unified Diagnostic Services)** diagnostic simulation and ECU flash-programming system built on Linux SocketCAN.  
Compliant with **ISO 14229-1** (UDS) and **ISO 15765-2** (ISO-TP).

---

## Design Documents

| # | Document | Description |
|---|----------|-------------|
| 01 | [System Requirements](design/01-System_Requirements_Specification.md) | Functional requirements & performance targets |
| 02 | [Architecture](design/02-System_Architecture_Design.md) | System layers & module breakdown |
| 03 | [UDS Protocol](design/03-UDS_Protocol_Design.md) | Diagnostic service details & message formats |
| 04 | [Bootloader & Flash](design/04-Bootloader_and_Flash_Design.md) | Flash workflow & security mechanisms |
| 05 | [Virtual CAN Platform](design/05-Virtual_CAN_Platform_Design.md) | SocketCAN integration & message routing |
| 06 | [Technology Stack](design/06-Technology_Stack.md) | Language choices, libraries, build system |
| 07 | [Project Structure](design/07-Project_Structure.md) | Source organisation & module list |
| 08 | [Interface Design](design/08-Interface_and_Integration_Design.md) | Internal APIs & version management |
| 09 | [Testing Strategy](design/09-Testing_Strategy.md) | Test plan, coverage targets, acceptance criteria |
| 10 | [Development Roadmap](design/10-Development_Roadmap.md) | Phase milestones & delivery checklist |

---

## Repository Layout

```
uds/
├── .github/workflows/   # CI (ci.yml) and CD (release.yml) pipelines
├── design/              # Architecture & design documents
├── src/                 # C source code (CMake project)
│   ├── core/            # UDS core services (0x10 / 0x22 / 0x27 / …)
│   ├── tp/              # ISO-TP transport layer
│   ├── can/             # SocketCAN wrapper & multi-ECU router
│   ├── bootloader/      # Bootloader & flash simulation
│   ├── ecusim/          # ECU simulator
│   └── include/         # Public C headers
├── tools/               # Python tooling
│   ├── uds-cli/         # Diagnostic client CLI
│   └── uds-test/        # Automated test framework
├── tests/               # C unit tests & Python integration tests
│   ├── unit/            # C unit tests (check framework)
│   └── integration/     # Python integration tests (vCAN)
├── configs/             # ECU / DID / DTC configuration files (JSON)
├── docs/                # API and developer documentation
├── scripts/             # Build & deployment helper scripts
├── firmware/            # Test firmware images (flash testing)
├── CMakeLists.txt       # Root CMake configuration
├── pyproject.toml       # Python project config (pytest, ruff, coverage)
└── requirements-dev.txt # Python development dependencies
```

---

## CI / CD Pipeline

### Continuous Integration (`ci.yml`)

Triggered on every **push** and **pull request** to `main` / `develop`.

| Job | What it does |
|-----|--------------|
| **Code Quality** | `cppcheck` (C static analysis), `clang-format` (formatting), `ruff` (Python lint + format) |
| **C Build & Unit Tests** | `cmake` build → `ctest` → `lcov` coverage → Codecov upload |
| **Python Tests** | `pytest` on Python 3.10 / 3.11 / 3.12 matrix → `pytest-cov` → Codecov upload |
| **Integration Tests** | Sets up `vcan0` → runs `tests/integration/` end-to-end |

### Continuous Delivery (`release.yml`)

Triggered on **version tags** (`v*.*.*`).

1. Builds a Release-mode binary and packages it as a `.tar.gz`
2. Runs the full test suite against the release build
3. Creates a GitHub Release with the artifact and an auto-generated changelog

---

## Development Quick-Start

### Prerequisites

```bash
# System packages
sudo apt-get install cmake gcc g++ libcheck-dev lcov can-utils

# Python
python -m pip install -r requirements-dev.txt
```

### Build (C)

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DENABLE_COVERAGE=ON
cmake --build build --parallel
```

### Test (C)

```bash
cd build && ctest --output-on-failure
```

### Test (Python)

```bash
pytest -m "not integration"   # unit tests only
pytest -m integration          # integration tests (requires vcan0)
```

### Set up virtual CAN

```bash
sudo modprobe vcan
sudo ip link add dev vcan0 type vcan
sudo ip link set up vcan0
```

---

## License

MIT OR Apache-2.0 — see `LICENSE` for details.
