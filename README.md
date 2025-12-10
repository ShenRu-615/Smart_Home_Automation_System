# 🏠 Smart Home Security & Automation Hub

A comprehensive smart home system using **ESP32** and **ESP RainMaker**. This project combines a robust security system with home automation controls, environmental monitoring, and cloud connectivity.

---

## 🛠️ Features

### 🔒 Security System
- **Keypad Access Control**: Arm/Disarm the system using a secure password (default: `2580`).
- **Door Monitoring**: Ultrasonic sensor detects if a person is nearby or if the door is open.
- **Auto-Lock**: Automatically arms the system if no activity is detected for 10 seconds.
- **Dynamic Password**: Change the security master password directly from the RainMaker app.

### 💡 Home Automation
- **Device Control**: Control Fan, Light, TV, and Smart Plug via app or keypad.
- **Fan Speed Control**: 5-speed fan regulation with unique sound feedback.
- **Keypad Shortcuts**:
  - `A`: Cycle Fan Speed
  - `B`: Toggle Light
  - `C`: Toggle TV
  - `D`: Toggle Plug

### 🌡️ Environmental Monitoring
- **Real-time Data**: Monitors Temperature and Humidity using DHT11.
- **Safety Alerts**: Sends a notification if the temperature exceeds 50°C.

### ☁️ Cloud & Connectivity
- **ESP RainMaker**: Remote control, status monitoring, and push notifications.
- **ESP Insights**: Remote diagnostics and system health monitoring.

---

## 📦 Components Used

- **ESP32 Development Board**
- **4x4 Matrix Keypad** (Input)
- **HC-SR04 Ultrasonic Sensor** (Door detection)
- **DHT11 Sensor** (Temperature/Humidity)
- **Active Buzzer** (Alarms & Feedback)
- **LEDs** (Red for Armed, Green for Disarmed/Open)
- **Jumper Wires**

---

## 🔌 Pin Mapping

| Component | ESP32 Pin |
|-----------|-----------|
| **Buzzer** | GPIO 2 |
| **Red LED** | GPIO 4 |
| **Green LED** | GPIO 5 |
| **Ultrasonic Trig** | GPIO 3 |
| **Ultrasonic Echo** | GPIO 1 |
| **DHT11** | GPIO 10 |
| **Keypad Rows** | GPIO 21, 20, 19, 18 |
| **Keypad Cols** | GPIO 9, 8, 7, 6 |

---

## 📱 RainMaker Dashboard

The app provides the following controls:
1.  **Home**: View Temp, Humidity, and overall device status.
2.  **Security**:
    - View Door Status (Open/Closed).
    - **Set Password**: Update the keypad access code.
3.  **Devices**: Individual toggles for Fan, Light, TV, and Plug.

---

## 🚀 Getting Started

1.  **Flash the Code**: Build and flash the project to your ESP32.
2.  **Provisioning**: Open the ESP RainMaker app, scan the QR code from the terminal, and connect the device to Wi-Fi.
3.  **Operation**:
    - The system starts in **Armed (Locked)** mode (Red LED ON).
    - Enter `2580#` on the keypad to disarm (Green LED ON).
    - Use keys `A`, `B`, `C`, `D` to control devices.
