# Codex Instructions: BMS-EV

This file applies to Codex when working in this repository. It is derived from
`PROJECT_AI_GUIDE.md` and adapted to Codex's repository workflow.

## Project Context

- **Project:** Battery Management System (BMS) for Electric Vehicle.
- **Purpose:** Student research project that applies practical embedded and
  automotive software engineering principles.
- **Architecture:** Distributed master-slave system over CAN bus.
- **Deployed configuration:** One Master plus Slave1 and Slave2 on a 5S2P
  pack (10 cells, five cells per Slave).
- **Firmware capacity:** The Master firmware supports up to three Slaves /
  15 cells via CAN IDs `0x101-0x103` (slave telemetry) and `0x200-0x202`
  (balance commands). `Software/Slave3` is a reserved stub.
- **Hardware:** STM32F103C8T6 and 18650 lithium-ion battery packs.
- **External tools:** Keil MDK, STM32CubeMX, LabVIEW, MATLAB, Git, and
  GitHub.

## Project Status

- **Status:** Complete for the 2026 student research cycle.
- **Deliverables:**
  - Master, Slave1, and Slave2 firmware (`Software/`, one Keil MDK
    project per node).
  - LabVIEW monitoring UI (`LabVIEW/`).
  - MATLAB 2024a analysis scripts and the `Two5s2p.slx` model
    (`MATLAB 2024a/`).
  - PCB, mechanical, and presentation documents (`Docs/`, `Hardware/`).
  - Benchmark datasets (`Docs/BMS_TESTING.csv` and
    `Docs/ReportExperimentData/ExperimentData.csv`).
  - CAN Rx/Tx test projects (`Testing Code/`).

## Engineering Priorities

Use this order when balancing trade-offs:

1. Correctness
2. Safety
3. Maintainability
4. Testability
5. Readability
6. Performance
7. Development speed

Keep recommendations proportional to a student research project. Apply
automotive practices where they add practical value, not as unnecessary process.

## Firmware Requirements

- Write C99 compatible with STM32 HAL and CMSIS.
- Use tab indentation, K&R braces, and `snake_case` names.
- Keep functions small and focused; write concise English comments only where
  they explain non-obvious intent.
- Avoid dynamic memory allocation and unnecessary global state.
- Make module state private where possible and isolate hardware-specific code.
- Treat CAN timeouts, invalid sensor readings, communication loss, and battery
  safety thresholds as explicit error paths.
- Do not invent HAL APIs, MCU register behavior, pin mappings, or hardware
  capabilities. Verify them against the existing project configuration first.

## Architecture

- Preserve a layered split between drivers/HAL, services, and application logic.
- Keep battery-management business rules independent of direct HAL calls when
  practical.
- Prefer module interfaces that can be unit tested without physical hardware.
- Avoid duplicated logic and unnecessary cross-module dependencies.
- Separate configuration values from implementation details.

## Final System Behavior

These behaviors are the validated result of the project. Do not change them
without explicit review:

- Balance start: `cell_mV > global_min_mV + 20 mV`; balance stop:
  `cell_mV < global_min_mV + 3 mV`.
- Hysteresis state is retained per cell across main-loop cycles to prevent
  balance-FET chatter.
- Master soft gate: `temperature_max >= 55.0 C` clears all active balance
  requests.
- Slave hard latch: `temp_cutoff` is set when `temperature > 60.0 C` and
  cleared only when `temperature < 55.0 C`.
- Slaves keep their last balance command when CAN frames stop arriving; the
  Master treats a slave as stale after `SLAVE_TIMEOUT_MS` (500 ms).
- The cooling fan turns on at `FAN_TEMP_ON_THRESHOLD` (27.5 C); cells below
  `CELL_VALID_MIN_MV` (500 mV) are excluded from balancing.

## Codex Workflow

1. Read the relevant source, headers, CubeMX configuration, and nearby code
   before proposing or making a change.
2. Check `git status` before editing. The worktree may contain user changes;
   never revert, overwrite, or format unrelated modifications.
3. Make the smallest coherent change that satisfies the request and preserve
   the established style in the affected module.
4. Review affected call sites, error paths, and master/slave protocol impact.
5. Run the most relevant available validation. State clearly when hardware,
   Keil, or a test environment prevents validation.
6. Report changed files, behavior, validation performed, and residual risks.

## Build and Validation

Each node has a Keil MDK project (`Master.uvprojx`, `Slave1.uvprojx`,
`Slave2.uvprojx`) under its `Software/<node>/MDK-ARM/` directory. Build from
the command line with this form:

`C:\Keil_v5\UV4\UV4.exe -j0 -b "Software\Master\MDK-ARM\Master.uvprojx" -o "<log file>"`

Substitute the Slave1 or Slave2 project path to build those nodes. The Master
build currently records `0 errors` with one pre-existing warning.

There is no automated unit-test harness; validation is Keil build plus
hardware-in-the-loop testing. State clearly whenever hardware, Keil, or a
test environment prevents validation.

## Git Conventions

- Use conventional commit messages: `docs: ...`, `feat(<area>): ...`,
  `sim(<area>): ...`, with scopes such as `bms`, `ui`, and `agents`.
- Keep one functional component per commit; firmware, UI, simulation, and
  data are committed separately.
- Stage only the intended paths; never stage unrelated work.

## Review and Documentation

- For code reviews, prioritize defects, safety risks, regressions, and missing
  tests; give findings before a summary.
- Recommend unit tests, static analysis, MISRA C guidance, and V-model
  practices when appropriate, but keep recommendations actionable.
- For multiple viable designs, briefly compare them, recommend one, and state
  the material trade-off.
- Keep documentation concise, technical, and aligned with the 80/20 principle.

## Do Not

- Do not guess undocumented behavior or fabricate interfaces/registers.
- Do not introduce quick hacks that compromise maintainability or safety.
- Do not ignore error handling, boundary cases, or communication failures.
- Do not perform broad refactors, modify unrelated files, or change generated
  CubeMX code without a clear request and impact assessment.
- Do not use destructive Git commands to discard existing work.
