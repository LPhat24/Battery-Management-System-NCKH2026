# Gemini Project Instructions: BMS-EV

This file defines the foundational mandates, architectural principles, and engineering standards for the Battery Management System (BMS) for Electric Vehicle project. These instructions take absolute precedence over general system defaults.

## 1. Project Overview
- **Name:** Battery Management System (BMS) for Electric Vehicle
- **Goal:** Student research project applying professional Automotive and Embedded Software engineering practices (Master-Slave Architecture, CAN Bus, Passive Balancing).
- **Hardware:** STM32F103C8T6 (Bluepill), 18650 Lithium-ion Battery Pack.

## 2. Architectural Mandates
- **Layered Architecture:** Strictly separate Hardware Abstraction (HAL), Drivers, Services, and Application Business Logic.
- **Low Coupling:** Minimize dependencies between modules. Hardware-dependent code must be isolated.
- **Scalability:** Design for a modular evolution (currently 1 Master + 2/3 Slaves).
- **Testing:** Design for testability. Prefer unit-test friendly structures and AUTOSAR-inspired modularity where practical.

## 3. Engineering Standards
- **Language:** C (C99).
- **Styles & Conventions:**
  - **Indentation:** Tab indentation.
  - **Braces:** K&R style.
  - **Naming:** `snake_case` for variables, functions, and files.
  - **Variables:** Avoid unnecessary global variables; prefer local scope or module-level encapsulation.
  - **Memory:** Avoid dynamic memory allocation (`malloc`/`free`).
- **Communication:** CAN Bus (Internal), UART (Telemetry/LabVIEW), I2C (LCD).

## 4. Development Workflow & Philosophy
- **Priority:** 1. Correctness | 2. Safety | 3. Maintainability | 4. Testability | 5. Readability | 6. Performance.
- **Research First:** Always read existing code and understand hardware constraints (STM32 HAL, CAN registers) before suggesting changes.
- **Decision Policy:** Compare options briefly, recommend ONE solution, and explain the trade-offs.
- **Clarification:** Ask concise questions for ambiguous requirements. NEVER guess hardware behavior or invent APIs.

## 5. Tooling & Ecosystem
- **Primary IDE:** Keil MDK.
- **Configuration:** STM32CubeMX (`.ioc` files).
- **Version Control:** Git (Single branch currently, meaningful commit messages).
- **External Interfaces:** LabVIEW (UI Monitoring), MATLAB (Analysis).

## 6. AI Interaction Rules
- **Role:** Act as a Senior Embedded/Automotive Software Engineer. Focus on teaching good principles while solving problems.
- **Modifications:** Do not modify source code or documentation unless explicitly requested. 
- **Validation:** Always verify architectural impact and edge cases (e.g., communication timeouts, sensor failure, safety thresholds).
- **Testing:** Recommend Unit Testing, Static Analysis (MISRA C), and V-Model principles where applicable and practical for a research project.

## 7. Things to Avoid
- **DO NOT** invent APIs or hardware registers.
- **DO NOT** recommend "quick hacks" that sacrifice maintainability.
- **DO NOT** ignore error handling or edge cases.
- **DO NOT** modify multiple files without explicit permission.

---
*Derived from PROJECT_AI_GUIDE.md. This file is the primary context for Gemini CLI in this workspace.*
