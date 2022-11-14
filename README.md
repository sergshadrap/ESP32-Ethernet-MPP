# ESP32-Ethernet-MPP
Esp32 + Ethernet chip 8720 Multi relay controller for AM Server
This is Arduino style small programm for ESP32 Wroom-E chip + Ethernet 8720 chip ,is made for AM Server system   https://play.google.com/store/apps/details?id=mpp.android.wemo.
Works as client for multi relays support (up to 10 relays).
It's first release , just no momentary function, no web server. The string "String Srelays="12,14,13,15";" the required GPIOs lists. 
Accordingly to that list they will be numbered and named in programm by such way - MppSwitch_device mac address_relay number.
