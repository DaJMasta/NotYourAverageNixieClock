# NotYourAverageNixieClock
Arduino and ATMega328 based nixie clock/indoor weather station programmable display using IN-12/IN-15 tubes
By: Jonathan Zepp - @DaJMasta - http://medpants.com

March 18, 2016 updated release v1.1

Update includes DST calculation (USA localization), a command for setting the RTC when installed in the clock without reflashing the Arduino nano, a fix for a minor bug, and a processing sketch to upload time to the clock's RTC.

Included:
driver_module.ino is the firmware for each of the 10 nixie driver boards running ATMega328 chips without external crystals
nixie_display_backplane.ino is the software for the backplane and handles all of the serial interface, sensor reading, and module commands
boards.txt is a modified arduino on a breadboard file to allow running both 328s and 328ps in 8MHz no external crystal mode - you install the breadboard package normally, then replace the boards.txt file with this one
SyncNixieClockRTC.pde - a Processing sketch to upload time to the onboard RTC with the correct header (adapted from the one included with the Arduino software)
The schematics are included as .png files generated in TinyCAD

Dependancies:
This was written in the Arduino IDE 1.6.5 and uses:
DHT library 0.1.13 http://playground.arduino.cc//Main/DHTLib
DS3232RTC library 1.0 https://github.com/JChristensen/DS3232RTC
SFE_BMP180 library 1.1.2 https://github.com/sparkfun/BMP180_Breakout_Arduino_Library/tree/V_1.1.2
CapacitiveSensor 05 https://github.com/PaulStoffregen/CapacitiveSensor

This project uses several prebuilt modules, including:
Arduino Nano v3.0
DS3232 RTC module
BMP 180 pressure sensor module
NCH6100HV Nixie power supply from Nixieclock.org

For more information, pictures, and video of some of the features, the project's homepage is here:
http://medpants.com/not-your-average-nixie-clock
