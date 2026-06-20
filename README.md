# Horizontal Axis Wind Turbine Electronic Control Unit (ECU)

## Load Control & User Interface Firmware

Embedded firmware for the **Load Control & User Interface Microcontroller Unit (MCU)** of the **Horizontal Axis Wind Turbine Electronic Control Unit (ECU)**.

This repository contains the embedded software responsible for electrical power management, battery charging, user interaction, data logging, and system coordination with the Turbine MCU.

---

# Documentation & Demo

| Resource                 | Link                            |
| ------------------------ | ------------------------------- |
| 🎥 Project Demonstration | *(Add YouTube Link)*            |
| 📄 Final Project Report  | [*(Add Final Report Link)* ](https://drive.google.com/file/d/1cckKhvj7mvzCEbm3IqWp0KvrUp9g6mgg/view?usp=sharing
)      |
| 📚 Technical Appendix    | *(Add Technical Appendix Link)* |

---

# Overview

The Horizontal Axis Wind Turbine Electronic Control Unit (ECU) was developed for the AeroPower research team at the University of Puerto Rico – Mayagüez to provide a complete embedded monitoring and control platform for a laboratory-scale horizontal-axis wind turbine.

The ECU is composed of two independent embedded controllers:

* **Turbine MCU**
* **Load Control & UI MCU** *(this repository)*

The Load Control & UI MCU supervises the complete electrical subsystem, including battery charging, converter regulation, power monitoring, load protection, user interaction, SD card logging, and communication with the Turbine MCU.

Separating mechanical and electrical responsibilities into two microcontrollers improves reliability, simplifies software development, and allows both processors to execute independently in real time.

---

# Engineering Highlights

* Dual-MCU distributed architecture
* SEPIC converter control
* Lead-acid battery charging controller
* Finite State Machine (Bulk, Absorption, Float, Fault)
* Dual INA229 power monitors
* LCD user interface
* Push-button navigation
* microSD data logging
* Load relay control
* Dump load protection
* UART communication
* Real-time telemetry

---

# Features

* Battery charging supervision
* SEPIC converter PWM control
* Battery voltage and current monitoring
* Turbine power monitoring
* Load relay management
* Critical fault detection
* LCD menu interface
* User input through push buttons
* Status LEDs
* Real-Time Clock (RTC)
* Data logging to microSD card
* Communication with the Turbine MCU

---

# Firmware Architecture

The firmware follows a modular architecture where each subsystem performs a dedicated function while operating as part of the complete wind turbine controller.

Main responsibilities include:

* Monitoring turbine electrical power
* Monitoring battery voltage and current
* Controlling the battery charging process
* Managing converter duty cycle
* Updating the LCD interface
* Logging operational data
* Managing user inputs
* Coordinating with the Turbine MCU

**Suggested image**

`images/system_overview.png`

*Figure 2 – Top-Level System View*

---

# Battery Charging State Machine

Battery charging is implemented as a Finite State Machine specifically designed for sealed lead-acid batteries.

Charging stages include:

* Initialization
* Bulk Charge
* Absorption
* Hold / Float
* Fault

The controller continuously evaluates:

* Battery voltage
* Battery current
* Turbine power availability
* Safety conditions

Whenever abnormal operating conditions are detected, the firmware transitions to a protected state and disconnects the load if necessary.

**Suggested image**

`images/load_state_machine.png`

*Figure 6 – Load State Diagram*

---

# User Interface

The user interface provides operators with real-time access to turbine and battery information.

Available functions include:

* System status
* Battery voltage
* Charging state
* Turbine measurements
* Data logging status
* Start / Stop control
* Menu navigation

**Suggested image**

`images/user_interface.png`

*Figure 4 – User Interface Layout*

---

# Data Logging

The firmware supports long-term operational data logging using a microSD card.

Recorded parameters include:

* Wind speed
* Rotor RPM
* Turbine voltage
* Turbine current
* Battery voltage
* Battery current
* Charging state
* System status

This information enables post-test analysis and supports future improvements to turbine performance and controller design.

---

# Communication

The Load Control & UI MCU exchanges information with the Turbine MCU through a UART interface.

### Received

* Wind speed
* Rotor RPM
* Turbine operating state
* Blade position

### Transmitted

* Critical operating conditions
* Emergency shutdown requests
* System synchronization
* Controller status

This communication enables coordinated control of both the electrical and mechanical subsystems.

**Suggested image**

`images/communication_architecture.png`

*Figure 41 – Communication Flow*

---

# Development Environment

## Hardware

* Texas Instruments MSPM0G3519
* INA229 Power Monitor (2x)
* 20×4 LCD
* Real-Time Clock (RTC)
* microSD Card Interface
* Relay Driver
* Push Buttons
* Status LEDs

## Software

* C
* Code Composer Studio (CCS)
* TI MSPM0 SDK
* SysConfig

---

# Repository Structure

```text
HAWT-Load_Control_and_UI
│
├── App/                     Application modules
├── Core/                    Core firmware and system logic
├── drivers/                 Peripheral and device drivers
├── .ccsproject              CCS project configuration
├── .cproject                Eclipse CDT project configuration
├── .project                 Eclipse project metadata
├── .gitignore               Git ignored files
├── load_ui_control.syscfg   TI SysConfig configuration
├── README.md                Repository documentation
└── LICENSE                  Project license
```

---

# Related Repositories

| Repository               | Description                                         |
| ------------------------ | --------------------------------------------------- |
| HAWT-TurbineMCU-Firmware | Turbine monitoring and blade pitch control firmware |
| HAWT-TurbineMCU-PCB      | PCB design for the Turbine MCU                      |
| HAWT-LoadUI-PCB          | PCB design for the Load Control & UI MCU            |

---

# Authors

* Hiram R. Rodríguez Hernández
* José M. Burgos Guntín
* Sergio A. Meléndez Padilla
* Sergio A. Da Silva López

Department of Electrical & Computer Engineering

University of Puerto Rico – Mayagüez

---

# License

This project was developed for educational and research purposes as part of the Embedded Systems Design course at the University of Puerto Rico – Mayagüez.
