ESP32 IoT Environmental Monitoring System

1. Project Overview

An ESP32-based IoT environmental monitoring system developed using ESP-IDF 6.1 and FreeRTOS.

The system collects real-time temperature and humidity data from an SHT20 sensor via I2C, controls an onboard cooling fan based on temperature thresholds, and transmits sensor readings to an MQTT broker over Wi-Fi.

Home Assistant is integrated to provide real-time monitoring and historical data visualisation.

Key Features

* Environmental Monitoring: Real-time temperature and humidity acquisition using an SHT20 sensor over I2C.
* Real-Time Task Management: FreeRTOS tasks and queues for sensor acquisition, fan control and MQTT data transmission.
* Automatic Fan Control: GPIO-based fan control with hysteresis — ON at 30°C and OFF at 28°C.
* Wireless Communication: Wi-Fi connectivity and MQTT telemetry using JSON messages.
* Data Visualisation: Home Assistant dashboard with live sensor values and historical graphs.
* Fault Handling: MQTT reconnection after network interruption and fan shutdown when temperature data is unavailable for three seconds.

2. Development Environment

Component	Technology
Microcontroller	ESP32-WROOM-32
Development Framework	ESP-IDF 6.1
Programming Language	C
Real-Time Operating System	FreeRTOS
Environmental Sensor	SHT20
Sensor Interface	I2C
Fan Control	GPIO15
Network	Wi-Fi
Messaging Protocol	MQTT
Monitoring Platform	Home Assistant
Development Tools	VS Code, Git, GitHub
