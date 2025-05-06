# On-The-Go Connect4 and Pixel Art Frame

This is a PlatformIO project for the ESP32 that powers my **On-The-Go Connect4 and Pixel Art Frame** – a fun and interactive portable device that lets you play Connect4 and create pixel art anywhere!

🔗 **Project Page**: [View on Hackster.io](https://www.hackster.io/arvindsa/on-the-go-connect4-and-pixel-art-frame-48584d)  
👤 **Author Profile**: [Arvind S.A. on Hackster.io](https://www.hackster.io/arvindsa/)



## 📝 Description

This project brings together game logic, pixel drawing, and ESP32's wireless features to create a handheld, versatile interactive display. Whether you're playing solo, challenging a friend, or drawing pixel art, this device makes it easy and portable.

Key features:
- Connect4 game with two-player support
- Pixel art drawing with selectable colors
- OLED/LED matrix display integration
- ESP32-based for wireless updates or future features



## 📁 Project Structure

This project uses [PlatformIO](https://platformio.org/) as the development environment.



## 🚀 Getting Started

1. **Install PlatformIO**  
   Install PlatformIO inside VSCode or as a standalone CLI.

2. **Clone this repository**  
   ```bash
   git clone <repo-url>
   cd <repo-folder>

3. **Update MQTT Credentials and Wifi**

   ```c
   const char *ssid = "************";
   const char *password = "************";
   const char *mqtt_server = "************";
   const int mqtt_port = 8883;
   const char *mqtt_topic = "************";
   const char *mqtt_user = "************"; // replace with your EMQX username
   const char *mqtt_pass = "************"; // replace with your EMQX password
   ```

4. Program to Connect4 Board using a serial prgrammer

