# NotYourAverageNixieClock
Arduino and ATMega328 based nixie clock/indoor weather station programmable display using IN-12/IN-15 tubes

December 7, 2015 initial release v1.0

This project uses an Arduino Nano to read sensors and drive a backplane that supports 10 ATMega328 tube driver modules connected by I2C.  Each driver is programmable and can sense A/B varieties of the tube it is set to, generates an I2C address from the backplane socket, and drives 11 discrete MPSA42 drivers to switch the pins of the IN-12 or IN-15 tubes.  The backplane controls each driver and uses an RTC module, a BMP180 pressure sensor, and a DHT22 for the basic clock display data.  This display also uses a Nixietubes.org NCH6100HV power supply to generate the high voltage for the tubes.
