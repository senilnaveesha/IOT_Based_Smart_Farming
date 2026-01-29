IoT-Based Smart Farming System (ESP32)

An **IoT-based smart farming and automated irrigation system** built using an **ESP32 microcontroller** and multiple **soil moisture sensors** for real-time environmental monitoring and intelligent water management.  
The system supports **multi-zone irrigation**, **sensor calibration**, **noise filtering**, and **safety-focused control logic** to optimize water usage and improve plant health.

This project demonstrates practical **embedded systems**, **IoT**, and **automation engineering** concepts suitable for real-world smart agriculture applications.



 Key Features

- ESP32-based control system
- 4-zone irrigation support (independent soil moisture sensors)
- Automated watering logic based on soil moisture levels
- Hysteresis control (prevents frequent ON/OFF switching)
- Median filtering** to reduce sensor noise
- Calibration-based moisture percentage calculation
- Cooldown & minimum ON-time logic
- Sensor fault detection & fail-safe shutdown
- Serial monitoring & calibration commands
- Designed for scalability and future IoT expansion



System Overview

Each irrigation zone continuously monitors soil moisture using an analog sensor.  
Based on configurable thresholds and safety rules, the system automatically controls pumps or solenoid valves to supply water only when required.

High-Level Logic
1. Read soil moisture sensor (multiple samples)
2. Apply median filtering
3. Convert raw ADC values to moisture percentage using calibration
4. Compare with defined thresholds
5. Activate irrigation if soil is dry and safety conditions are met
6. Stop watering once moisture recovers or safety limits are reached



Hardware Requirements

- ESP32 Development Board
- 4 × Capacitive Soil Moisture Sensors (Analog)
- 4 × Relay Module or MOSFET Driver
- Water Pump **or** Solenoid Valves
- External Power Supply (for pump/valves)
- Jumper wires & basic plumbing setup

> Important:  
> Do NOT power pumps or valves directly from the ESP32. Always use an external power source with proper isolation.

---

Default Pin Configuration

| Zone | Moisture Sensor (ADC) | Relay / Valve Pin |
|-----:|------------------------|-------------------|
| 1    | GPIO 34                | GPIO 25           |
| 2    | GPIO 35                | GPIO 26           |
| 3    | GPIO 32                | GPIO 27           |
| 4    | GPIO 33                | GPIO 14           |

> ESP32 ADC1 pins are used to ensure stable readings when Wi-Fi is enabled.

---

Software Setup (Arduino IDE)

Requirements
- Arduino IDE 1.8.x or newer
- ESP32 Board Package (Espressif Systems)

Installation Steps
1. Open Arduino IDE
2. Go to File → Preferences
3. Add the following to Additional Board Manager URLs:
