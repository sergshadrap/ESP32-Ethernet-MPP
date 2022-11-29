# ESP32-Ethernet-MPP
Esp32 + Ethernet chip 8720 Multi relay controller for AM Server
This is Arduino style small programm for ESP32 Wroom-E chip + Ethernet 8720 chip ,is made for AM Server system   https://play.google.com/store/apps/details?id=mpp.android.wemo.
Works as client for multi relays support (up to 10 relays).
It's second release , no momentary function, web server added. Web server is running on port 80. By default only on GPIO12 is set for relay.
For setting GPIOs you need please visit web server of the device and enter GPIO numbers comma separated. 
Accordingly to that list they will be numbered and named in programm, by such way - MppSwitch_device mac address_relay number.
