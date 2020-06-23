<p align="center">
  <a href="https://www.espressif.com/en/products/hardware/esp32/resources">
    <img alt="Espressif" src="https://www.espressif.com/sites/all/themes/espressif/logo.svg" width="500" />
  </a>
  <a href="https://www.arduino.cc/en/main/software">
    <img alt="Arduino" src="https://www.arduino.cc/en/pub/skins/arduinoWide/img/ArduinoAPP-01.svg" width="100" />
  </a>
</p>
<h1 align="center">
  ESP32 Victron Test Project for Arduino IDE
</h1>

Contains an Arduino project for Espressif ESP32 that will run a web server with web sockets to display data from a Victron MPPT charge controller in real time.

## ⚙️ Gear you need

### 🛠 Hardware

You need an ESP32 development board. I used a [NodeMCU-32s](https://www.amazon.com/dp/B07PP1R8YK/ref=twister_B0816C3VDG?_encoding=UTF8&psc=1). Also a couple of generic NPN transistors to shift the level of the 5V RS-232 signaling from the Victron unit to the 3.3V needed by the ESP32. A level-shift board could be used instead of the transistors.

### 📀 Software

You need the [Arduino IDE](https://www.arduino.cc/en/main/software), of course. It must be set up for ESP32 development, including the sketch data uploader. Check out [my guide](https://ideaup.online/blog/esp32-set-up-on-arduino/) if you need help!

You will also need the ArduinoJSON, ESPAsyncWebServer, and ESPAsyncTCP libraries. I have a [guide to downloading and setting all those up](https://ideaup.online/blog/esp32-webserver-with-websockets/), too!

## 🧩 Getting set up

Once you have this project downloaded there are still a couple of things to do.

### 👫 Hookups

Hook up the charge controller's VE.Direct port to the ESP32's serial port #2. Those are pins GPIO 16 & 17. Since we only want to listen, it is just GPIO 16 that needs to be connected. Connect GND and VE.Direct-TX (pins 1 & 2, respectively, see pdf) from the controller's VE.Direct to GND and your level shifter. From the level shifter, connect to GPIO 16.

### 📡 WiFi credentials

You will need to rename the file `sample.config.json` to `config.json` and move it to the `data` directory. Edit the file to reflect the ssid and key for your network. The ESP32 will connect to this network and attempt to establish an mDNS responder.

## 🚀 Launching the project

Upload sketch data to the board first, then upload the sketch. Watch the serial monitor for WiFi to connect. If the mDNS responder was set up successfully, browse to [http://esp32.local](http://esp32.local) to see a UI with real-time updates. If the mDNS didn't work, point your browser to the IP printed in the serial monitor instead.

<p align="center" style="padding-top: 50">🍀 Good Luck! 🍀
