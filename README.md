# Smart GSM-Based DC Water Pump Controller

## Project Overview
The Smart GSM-Based DC Water Pump Controller is an embedded systems project developed using ESP32 and GSM technology. It allows users to remotely control a DC water pump through SMS and missed-call-based commands.

The system also monitors the pump's current using an ACS712 current sensor to detect dry-run and overload conditions. If a fault is detected, the controller switches OFF the pump and sends an SMS notification.

## Features
- Remote pump ON/OFF control through SMS.
- Pump control through authorized incoming calls.
- Real-time current monitoring using ACS712.
- Automatic dry-run and overload detection.
- SMS notifications for pump status and faults.
- Automatic retry after fault detection.
- System lock after multiple unsuccessful retries.
- 16x2 LCD display for system status.
- LED indicators for motor and fault conditions.

## Hardware Components
| Component | Purpose |
|---|---|
| ESP32 DevKit | Main controller |
| SIM800L GSM Module | GSM communication |
| ACS712 Current Sensor | Pump current monitoring |
| 12V DC Water Pump | Water pumping |
| 5V Relay Module | Pump switching |
| LM2596 Buck Converter | Voltage regulation |
| 16x2 LCD with I2C | Status display |
| 11.1V Li-ion Battery | Power supply |
| Capacitors | Power supply filtering |
| Fuse and Fuse Holder | Circuit protection |

## Working Principle
1. The SIM800L GSM module receives SMS commands or incoming calls.
2. The ESP32 checks whether the sender's mobile number is authorized.
3. Based on the received command, the ESP32 controls the relay to switch the pump ON or OFF.
4. The ACS712 sensor continuously monitors the current consumed by the pump.
5. The ESP32 compares the measured current with predefined threshold values.
6. If dry-run or overload conditions are detected, the pump is automatically switched OFF.
7. The system sends an SMS notification about the fault.
8. The controller attempts automatic restarting after a predefined delay. After repeated failures, the system gets locked until a reset command is received.

## SMS Commands
| Command | Function |
|---|---|
| ON | Switch ON the pump |
| OFF | Switch OFF the pump |
| STATUS | Get current pump status |
| RESET | Reset the fault or unlock the system |

The system accepts commands only from the configured authorized mobile number.

## Technologies Used
- Embedded C/C++
- ESP32 Microcontroller
- Arduino IDE
- GSM Communication
- UART Serial Communication
- ADC and Sensor Interfacing
- I2C Communication
- Relay Control

## Project Structure
```text
Smart-GSM-Based-DC-Water-Pump-Controller/
│
├── GSM_Pump_Controller.ino
├── README.md
├── LICENSE
│
├── Circuit-Diagram/
│
├── Images/
│
└── Documentation/
```

## Applications
- Agricultural irrigation systems
- Remote water pump operation
- Smart farming applications
- Remote motor monitoring

## Future Improvements
- Mobile application integration.
- Water level monitoring.
- Improved fault detection.
- 4G communication support.

## Author
**Lokesh Malakar**
B.Tech – Electronics and Communication Engineering
---

If you find this project useful, feel free to explore the source code and project documentation.
