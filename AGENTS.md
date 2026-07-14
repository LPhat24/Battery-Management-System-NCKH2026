# Codex Instructions: BMS-EV

This file applies to Codex when working in this repository. It is derived from
`PROJECT_AI_GUIDE.md` and adapted to Codex's repository workflow.

## Project Context

- **Project:** Battery Management System (BMS) for Electric Vehicle.
- **Purpose:** Student research project that applies practical embedded and
  automotive software engineering principles.
- **Architecture:** Distributed master-slave system over CAN bus; currently one
  master and two slave nodes.
- **Hardware:** STM32F103C8T6 and 18650 lithium-ion battery packs.
- **External tools:** Keil MDK, STM32CubeMX, LabVIEW, and MATLAB.

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
