# Design and Development of a Battery Monitoring and Warning System on an Embedded Platform Using CAN Bus
### Thiết kế và xây dựng mô hình hệ thống giám sát và cảnh báo pin trên nền tảng hệ thống nhúng sử dụng CAN Bus

## 1. Title & Executive Summary

This repository contains the completed design, firmware, tooling, and experimental data for a
distributed **Battery Management System (BMS)** for an electric vehicle, developed as a student
research project. The system implements a **distributed master-slave topology**: one **STM32F103C8T6
Master node** coordinates two **STM32F103C8T6 Slave nodes** over a **CAN bus**, with each Slave
performing local cell acquisition, temperature measurement, and passive balancing on a five-cell
segment of a **5S2P 18650 lithium-ion pack** (10 cells deployed). The Master aggregates telemetry,
runs the global balancing algorithm with **hysteresis**, enforces a **55 °C thermal gate**, drives a
cooling fan and a local LCD, and streams CSV telemetry to a **NI LabVIEW** monitoring HMI over UART.

The firmware architecture follows automotive and embedded best practices: layered separation of
hardware abstraction and application logic, explicit CAN/communication timeouts, deterministic
per-cell state retention, and bounded safety behavior. A **MATLAB 2024a Simulink** model of two
5S2P battery networks supports pre-hardware algorithm validation, and a benchmark dataset captured
through the LabVIEW HMI is provided for offline analysis.

The Master firmware is written for up to **three Slaves / 15 cells**; the deployed hardware build
uses **Slave1 and Slave2 (10 cells)**, with `Software/Slave3` reserved as a stub.

## 2. System Architecture & Block Diagram

```
        +---------------------+          UART 115200, CSV telemetry (~700 ms)          +-------------------------+
        |   PC / LabVIEW      |  <===============================================     |   STM32F103 Master      |
        |   Monitoring HMI    |                                                    |   Balancing, safety,    |
        |  (BMS UI.vi)        |                                                    |   LCD, fan, UART        |
        +---------------------+                                                    +------------+------------+
                                                                                              |
                                                                                 CAN bus, 125 kbps, ISO 11898-1
                                                                                              11-bit standard IDs
                                          +------------------------------+--------+--------+--------+
                                          |                              |                 |
                                  +-------+-------+              +-------+-------+  +-----+-------+
                                  |  Slave 1      |              |  Slave 2      |  |  Slave 3    |
                                  |  5 cells      |              |  5 cells      |  |  reserved   |
                                  |  NTC + balance|              |  NTC + balance|  |  (.gitkeep) |
                                  +---------------+              +---------------+  +-------------+
```

- **Topology:** distributed Master-Slave over classical CAN (CAN 2.0B active, ISO 11898-1).
- **Deployed configuration:** 1 Master + 2 Slaves on a 5S2P pack (10 cells, five per Slave).
- **Supported capacity:** Master firmware supports up to 3 Slaves / 15 cells.
- **HMI link:** Master to PC over UART (115200 baud), CSV telemetry.

## 3. Technical Specifications

| Parameter | Specification |
| --- | --- |
| Microcontroller | STM32F103C8T6 (ARM Cortex-M3), one Master + up to three Slaves |
| System clock | 72 MHz HCLK, 36 MHz APB1 |
| CAN controller | STM32 bxCAN, CAN 2.0B active, ISO 11898-1 |
| CAN bitrate | **125 kbps** (Prescaler 18, BS1 13 TQ, BS2 2 TQ, SJW 1, 16 TQ total) |
| CAN identifiers | 11-bit standard frames |
| Communication media | Differential CAN bus |
| Topology | Distributed Master-Slave |
| Battery pack | 5S2P 18650 lithium-ion; 10 cells deployed, 15 cells supported |
| Passive balancing | Start > `global_min + 20 mV`; stop < `global_min + 3 mV` (per-cell hysteresis) |
| Master thermal gate | Balancing disabled at `temperature_max >= 55.0 °C` |
| Slave thermal latch | Set when `temperature > 60.0 °C`; cleared only below `55.0 °C` |
| Cooling fan threshold | 27.5 °C (`FAN_TEMP_ON_THRESHOLD`) |
| Cell validity floor | 500 mV (`CELL_VALID_MIN_MV`) |
| Slave stale timeout | 500 ms (`SLAVE_TIMEOUT_MS`) |
| Slave telemetry cycle | 50 ms (20 Hz, TIM2) |
| PC HMI link | UART 115200 baud, CSV frame, ~700 ms |
| Temperature sensing | NTC thermistor via 12-bit ADC |
| Current sensing | WCS1700 Hall-effect sensor via ADC |
| SoC estimation | Per-cell linear OCV mapping, 3000–4200 mV |

## 4. Firmware Implementation Highlights

The Master control loop is implemented in `Software/Master/Core/Src/main.c`.

- **Per-cell passive balancing with hysteresis**
  - `BALANCE_START_DELTA_MV = 20` — a cell begins discharging once it exceeds the pack minimum by more than 20 mV.
  - `BALANCE_STOP_DELTA_MV = 3` — a discharging cell is released only when it falls below the minimum plus 3 mV.
  - A persistent `active_cells` bitfield retains per-cell hysteresis state across main-loop cycles, preventing balance-FET chatter.
- **Valid-cell validation and no-valid-cell handling**
  - Only cells at or above `CELL_VALID_MIN_MV` (500 mV) participate in the minimum/valid count, excluding disconnected or invalid channels.
  - When no valid cell exists, the Master transmits `enable = 1, mask = 0` so Slaves remain synchronized without discharging.
- **Master thermal gate (55 °C)**
  - When `temperature_max >= 55.0 °C`, all active balance requests are cleared and re-evaluated only after the pack cools, forcing a fresh 20 mV restart.
- **Slave thermal latch (60 °C / 55 °C)**
  - Each Slave latches `temp_cutoff` above 60.0 °C and clears it only below 55.0 °C, independently inhibiting its balancing FETs.
- **Communication-loss behavior**
  - Slaves retain their last balance command when CAN frames stop arriving; the Master marks a Slave stale after 500 ms and invalidates its cells.
- **Coordination**
  - The Master runs balance control, fan control, LCD update, and UART telemetry; telemetry is transmitted only when the `SW_UART` (PA15) switch is enabled.

## 5. Communication Protocol

CAN bus: classical CAN, 125 kbps, 11-bit standard identifiers.

| CAN ID | Direction | Frame | DLC | Payload (little-endian) | Cadence |
| --- | --- | --- | --- | --- | --- |
| `0x101` | Slave1 → Master | Telemetry Frame A | 8 | Cells 1–4, `uint16` each, mV | 50 ms |
| `0x101` | Slave1 → Master | Telemetry Frame B | 6 | Cell 5 `uint16` mV; temperature `int16` (0.1 °C); MOSFET mask `uint8`; sequence `uint8` | 50 ms |
| `0x102` | Slave2 → Master | Telemetry Frame A / B | 8 / 6 | Same layout as Slave1 | 50 ms |
| `0x103` | Slave3 (reserved) → Master | Telemetry Frame A / B | 8 / 6 | Same layout as Slave1 | 50 ms |
| `0x200` | Master → Slave1 | Balance command | 8 | Byte 0: `enable` (0/1); Byte 1: balance mask (bit *i* = cell *i*) | Control cycle |
| `0x201` | Master → Slave2 | Balance command | 8 | Same layout | Control cycle |
| `0x202` | Master → Slave3 (reserved) | Balance command | 8 | Same layout | Control cycle |

Notes:

- Frame A and Frame B share the same identifier; the Master distinguishes them by `DLC` (8 vs 6).
- The Master sends a balance command after each complete telemetry pair. When the balancing switch is off, it sends `enable = 0`; when no cell is valid, it sends `enable = 1, mask = 0`.
- The PC HMI is not on the CAN bus; the Master forwards aggregated CSV telemetry over UART at 115200 baud.

## 6. Repository Structure

```
Battery-Management-System-EV/
├── Docs/
│   ├── BMS_TESTING.csv                     # Raw experiment log recorded by LabVIEW
│   └── ReportExperimentData/
│       └── ExperimentData.csv              # Verified / cleaned experiment dataset
├── Software/
│   ├── Master/
│   │   ├── Core/Src/main.c                 # Master control loop
│   │   └── MDK-ARM/Master.uvprojx          # Keil MDK project
│   ├── Slave1/
│   │   ├── Core/Src/main.c
│   │   └── MDK-ARM/Slave1.uvprojx
│   ├── Slave2/
│   │   ├── Core/Src/main.c
│   │   └── MDK-ARM/Slave2.uvprojx
│   └── Slave3/                             # Reserved stub (.gitkeep)
├── LabVIEW/
│   └── BMS UI/
│       ├── BMS UI.lvproj
│       └── BMS UI.vi                       # Monitoring & diagnostic HMI
├── MATLAB 2024a/
│   ├── Two5s2p.slx                         # Two 5S2P battery network model
│   ├── BuildGraph.m                        # Offline CSV plotting
│   └── ScriptMonitor.m                     # Live UART monitoring
├── Hardware/                               # PCB, enclosure, mechanical assets
├── Testing Code/
│   ├── TEST_CAN_Rx/
│   └── TEST_CAN_Tx/
├── AGENTS.md
├── GEMINI.md
├── PROJECT_AI_GUIDE.md
└── README.md
```

## 7. Build & Setup Instructions

### 7.1 Firmware (Keil MDK-ARM)

1. Install Keil MDK-ARM v5 with the STM32F1xx device pack.
2. Open the desired project, e.g. `Software/Master/MDK-ARM/Master.uvprojx`.
3. Rebuild (F7 / Build). The command-line form is:

   ```
   C:\Keil_v5\UV4\UV4.exe -j0 -b "Software\Master\MDK-ARM\Master.uvprojx" -o "Master_build.log"
   ```

   Substitute `Slave1.uvprojx` or `Slave2.uvprojx` to build the slave nodes.

4. Current build status: **0 errors, 1 pre-existing warning** in `main.c`
   (`warning: #188-D: enumerated type mixed with another type`). All nodes flash
   and run.
5. Flash the Master and Slaves over ST-Link and connect the CAN bus before power-up.

### 7.2 LabVIEW HMI

1. Open `LabVIEW/BMS UI/BMS UI.lvproj` in NI LabVIEW.
2. Run `BMS UI.vi`.
3. Select the Master's COM port and set the baud rate to **115200**.
4. Ensure the Master `SW_UART` switch (PA15) is **ON**; telemetry and logging then start automatically.

### 7.3 MATLAB / Simulink

1. Open `MATLAB 2024a/Two5s2p.slx` (R2024a) to inspect or simulate the two 5S2P battery network model.
2. Run `MATLAB 2024a/BuildGraph.m` to regenerate pack and cell plots from `Docs/BMS_TESTING.csv`.
3. Run `MATLAB 2024a/ScriptMonitor.m` for live monitoring (requires an available COM port).

## 8. Verification & Experimental Datasets

- `Docs/BMS_TESTING.csv` — raw capture logged directly by the LabVIEW HMI (74,757 records, 27 fields:
  timestamp, pack total voltage, SoC, current, temperature, min/max/delta voltage, switch states, and
  the ten cell voltages). The exact column layout is documented at the top of `MATLAB 2024a/BuildGraph.m`.
- `Docs/ReportExperimentData/ExperimentData.csv` — the verified/cleaned dataset retained for reporting.

Both files currently contain the same verified capture. Recommended verification flow:

1. Rebuild the firmware and confirm the build report.
2. Power the pack and confirm the LabVIEW HMI updates cell voltages, temperature, SoC, and balancing flags.
3. Trigger a cell imbalance and confirm the 20 mV / 3 mV hysteresis behavior and that balancing stops above 55 °C.
4. Re-plot the captured data with `BuildGraph.m` and compare against the figures used in the report.

## 9. License & Credits

**Credits**

- **Vehicle Automatic Control Laboratory (VAC Lab) — HCM-UTE** (Ho Chi Minh City University of
  Technology and Education): project context, laboratory facilities, and supervision.
- Student research project, 2026 cycle: firmware, LabVIEW HMI, Simulink model, and
  experimental datasets.

**License**

No open-source license is granted by this repository. All rights are reserved by the authors and the
laboratory. Contact the Vehicle Automatic Control Laboratory — HCM-UTE before reuse, redistribution,
or derivative work.
