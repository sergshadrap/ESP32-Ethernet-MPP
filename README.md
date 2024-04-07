# ESP32-Ethernet-MPP
Esp32 + Ethernet chip 8720 Multi relay controller for AM Server
This is Arduino style small programm for ESP32 Wroom-E chip + Ethernet 8720 chip ,is made for AM Server system   https://play.google.com/store/apps/details?id=mpp.android.wemo.
Works as client for multi relays support (up to 10 relays).
It's second release , no momentary function, web server added. Web server is running on port 80. By default only on GPIO12 and GPIO14 is set for relays.
For setting GPIOs you need please visit web server of the device and enter GPIO numbers comma separated. Remeber , that simple web server doesn't clear prompt from input ! Be carefull!
Accordingly to that list they will be numbered and named in programm, by such way - MppSwitch_device mac address_relay number.
After all you can rename it in AM server console - as you wish. Note - there is no space allowed in the name.
The new version stores the current config in EEPROM memory, such as relays pins, state of each relay, names. Even if the power was down - it will be restored for the last succsesfull state.
The 1.2.1 version add UDP discovery , UDP notifications of subscribers.
