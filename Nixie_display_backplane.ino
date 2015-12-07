/*
 * Nixie_Display_Backplane by Jonathan Zepp - @DaJMasta
 * Version 2 - December 7, 2015
 * For the Not Your Average Nixie Clock project
 * 
 * Programmed to an arduino nano in the backplane, this software reads the required sensors and controls the driver boards.  Includes
 * an RTC, a temperature and humidity sensor, a barometric pressure sensor, a potentiometer to control brightness, and a capacitive
 * touch sensor to change display modes easily.  Reports actions and errors over a host serial connection and the display can be
 * fully controlled through it.  Translates String input to each tube and checks if they are capable of displaying the character needed.
 * 
 * Built in Arduino iDE 1.6.5
 * Uses:
 * DHT library 0.1.13 http://playground.arduino.cc//Main/DHTLib
 * DS3232RTC library 1.0 https://github.com/JChristensen/DS3232RTC
 * SFE_BMP180 library 1.1.2 https://github.com/sparkfun/BMP180_Breakout_Arduino_Library/tree/V_1.1.2
 * CapacitiveSensor 05 https://github.com/PaulStoffregen/CapacitiveSensor
 */

#define minPot 512
#define maxBrightness 255
#define minBrightness 10
#define potentiometerPin A0
#define dimmingPin 3
#define dhtPin 2
#define resetPin 4
#define capSenseAnalogPin A1
#define capSensePowerPin 8
#define longPressDuration 1000
#define displayStages 5
#define testStages 11                                                 //10 sequential tubes plus free run
#define freeTestDuration 15000                                        //Must be higher than sequentialTestDuration
#define sequentialTestDuration 750
#define sensorReadDelay 3000
#define inputPollDelay 33                                             //30 FPS touch speed
#define tempOffset -6.7

#include <dht.h>
#include <Wire.h>
#include <Time.h>
#include <DS3232RTC.h>
#include <SFE_BMP180.h>
#include <CapacitiveSensor.h>
#include <EEPROM.h>

byte brightness, displayStage, testStage, dateInterval, dateFrequency ;
int nDevices, altitudeM ;
float humidity, fahrenheit ;
double barometerTemperature, pressure, relPressure ;
byte i2cDevices[13], driverAddresses[10], tubeTypes[10], driverFirmwares[10] ;
char tubeStates[10], currentDisplays[10], toWrite[10] ;
bool writeDecimals[10], displayedDecimals[10] ;
long touchStart, lastStageChange, lastSensorRead, lastTouchCheck ;
bool bLongPressLock, bDisplaySeconds, bRelativePressure, bDriverLightEnable ;
unsigned int displayStageDuration, displayStagePrimaryDuration ;

dht tempHumSensor ;
SFE_BMP180 pressureSensor ;
CapacitiveSensor displayHousing = CapacitiveSensor(capSensePowerPin, capSenseAnalogPin) ;

struct EEPROMBlock{
  int altitudeM ;
  unsigned int displayStagePrimaryDuration ;
  unsigned int displayStageDuration ;
  bool bDisplaySeconds ;
  bool bRelativePressure ;
  bool bDriverLightEnable ;
  byte dateFrequency ;
} ;

void setup() {                                                      //Initialization
  EEPROMBlock readFromEEPROM ;
  TCCR2B = TCCR2B & B11111000 | B00000110 ;                         //122.55 Hz for reduced power noise
  
  pinMode(potentiometerPin, INPUT) ;
  pinMode(dimmingPin, OUTPUT) ;
  pinMode(resetPin, OUTPUT) ;

  Serial.begin(57600) ;
  Wire.begin() ;

  Serial.println("Driver reset") ;
  digitalWrite(resetPin, LOW) ;                                           
  delay(200) ;
  digitalWrite(resetPin, HIGH) ;
  delay(3000) ;

  for(int i = 0; i < 13; i++)
    i2cDevices[i] = 255 ;

  Serial.print("I2C bus scan: ") ;

  nDevices = 0 ;
  scanI2cBus() ;

  Serial.println() ;

  setSyncProvider(RTC.get);
  if(timeStatus()!= timeSet) 
     Serial.println("RTC sync error");
  else
     Serial.println("RTC time set");      

  if(pressureSensor.begin())
    Serial.println("Barometer initialized") ;
  else
    Serial.println("Barometer error") ;

  readTubeDrivers() ;
     
  humidity = -100.0 ;
  fahrenheit = -1000.0 ;
  barometerTemperature = -1000.0 ;
  pressure = -100.0 ;
  relPressure = -100.0 ;
  touchStart = 0 ;
  displayStage = 1 ;
  testStage = 0 ;
  bLongPressLock = false ;
  lastTouchCheck = 0 ;
  dateInterval = 0 ;
  
  EEPROM.get(0, readFromEEPROM) ;

  bDisplaySeconds = readFromEEPROM.bDisplaySeconds ;
  altitudeM = readFromEEPROM.altitudeM ;                                  
  bRelativePressure = readFromEEPROM.bRelativePressure ;
  dateFrequency = readFromEEPROM.dateFrequency ;                            
  displayStageDuration = readFromEEPROM.displayStageDuration ;
  displayStagePrimaryDuration = readFromEEPROM.displayStagePrimaryDuration ;
  bDriverLightEnable = readFromEEPROM.bDriverLightEnable ;

  if(!bDriverLightEnable)                                               //Assumes drivers default to on
    allTubes('Q') ;

  allTubes('N') ;

  for(int i = 0; i < 10; i++){
    writeDecimals[i] = false ;
    toWrite[i] = ' ' ;
    currentDisplays[i] = ' ' ;
    displayedDecimals[i] = false ;
  }

  readTubeDrivers() ;
  listDrivers() ;

  calcBrightness() ;
  readTempHumPres() ;   
  
  Serial.println("Initialization complete!") ;
  lastStageChange = millis() ;
  lastSensorRead = millis() ;
}


void loop() {                                                                               //Main loop
  if(lastTouchCheck + inputPollDelay <= millis()){
    if(checkTouch()){                                                                         //Capacitive touch sensing for changing display modes
      if(touchStart == 0)
        touchStart = millis() ;
      else if(touchStart + longPressDuration <= millis() && !bLongPressLock){
        if(testStage > 0){
          displayStage = 1 ;
          testStage = 0 ;
          bLongPressLock = true ;
          lastStageChange = millis() ;
          allTubes('N') ;
          touchStart = 0 ;
          for(int i = 0; i < 10; i++){
            currentDisplays[i] = ' ' ;
            displayedDecimals[i] = false ;
          }
        }
        else if(displayStage > 0){
          displayStage = 0 ;
          testStage = 1 ;
          bLongPressLock = true ;
          lastStageChange = millis() ;
          allTubes('T') ;
          touchStart = 0 ;
        }
        else {
          Serial.println("Long Press") ;
          touchStart = 0 ;
        }
      }
    }
    else {
      if(touchStart > 0 && !bLongPressLock){
        if(displayStage > 0){
          displayStage++ ;
          lastStageChange = millis() ;
        }
        else if(testStage > 0){
          if(testStage == 1){
            allTubes('N') ;
            for(int i = 0; i < 10; i++){
              currentDisplays[i] = ' ' ;
              displayedDecimals[i] = false ;
          }
          }
          testStage++ ;
          lastStageChange = millis() ;
        }
        else
          Serial.println("Short Press") ;
      }
      touchStart = 0 ;
      bLongPressLock = false ;
    }
    lastTouchCheck = millis() ;
  }

  if(displayStage == 1 && lastStageChange + displayStagePrimaryDuration <= millis()){
      displayStage++ ;
      if(dateInterval > 0)
        displayStage++ ;
      dateInterval++ ;
      if(dateInterval >= dateFrequency)
        dateInterval = 0 ;
      lastStageChange = millis() ;
    }
  else if(displayStage > 1 && lastStageChange + displayStageDuration <= millis()){
      displayStage++ ;
      lastStageChange = millis() ;
  }
 
  if(testStage == 1 && lastStageChange + freeTestDuration <= millis()){
      testStage++ ;
      lastStageChange = millis() ;
      allTubes('N') ;
      for(int i = 0; i < 10; i++){
        currentDisplays[i] = ' ' ;
        displayedDecimals[i] = false ;
      }
  }
  else if(testStage > 1 && lastStageChange + sequentialTestDuration <= millis()){
      testStage++ ;
      lastStageChange = millis() ;
  }

  if(displayStage > displayStages){
    displayStage = 1 ;
  }
  if(testStage > testStages){
    testStage = 1 ;
    allTubes('T') ;
  }

  if(lastSensorRead + sensorReadDelay <= millis()){
    readTempHumPres() ;
    lastSensorRead = millis() ;
  }
  
  calcBrightness() ;
  
  switch(displayStage){
    case 1:   mapToDisplay(clockDisplay()) ;
              break ;
    case 2:   mapToDisplay(dateDisplay()) ;
              break ;
    case 3:   mapToDisplay(tempDisplay()) ;
              break ;
    case 4:   mapToDisplay(humDisplay()) ;
              break ;
    case 5:   mapToDisplay(pressureDisplay()) ;
              break ;
    default:  break ;          
  }
  if(testStage > 1)
    staggeredWrite() ;
  else if(testStage != 1)
    writeAll() ;

  recieveSerial() ;
}


void calcBrightness(){
  brightness = (((analogRead(potentiometerPin) - minPot) / 2) * ((maxBrightness - minBrightness)) / maxBrightness) + minBrightness ;
  analogWrite(dimmingPin, (255 - brightness)) ;
}

void readTempHumPres(){
  double toSend ;
  byte status ;
  
  tempHumSensor.read22(dhtPin) ;
  fahrenheit = tempHumSensor.temperature ;
  humidity = tempHumSensor.humidity ;

  status = pressureSensor.startTemperature() ;
  if(status != 0){
    delay(status) ;
    status = pressureSensor.getTemperature(toSend) ;
    if(status != 0){
        barometerTemperature = toSend ;
        status = pressureSensor.startPressure(3) ;
        if(status != 0){
          delay(status) ;
          status = pressureSensor.getPressure(toSend, barometerTemperature) ;
          if(status != 0){
            pressure = toSend ;
            relPressure = pressureSensor.sealevel(pressure, altitudeM) ;
          }
        }
      }
   }
  fahrenheit += barometerTemperature ;
  fahrenheit /= 2 ;
  fahrenheit *= 1.8 ;
  fahrenheit += 32 + tempOffset ;
}

String clockDisplay(){
  String toReturn = "" ;
  int temp ;
  bool bPM = false ;
  
  if(bDisplaySeconds){
    temp = hour() ;
    if(temp >= 12)
      bPM = true ;
    if(temp > 12)
      temp -= 12 ;
    if(temp == 0)
      temp += 12 ;

    if(bPM)
      toReturn += " " ;
    if(temp < 10)
      toReturn += " " ;
    toReturn += temp ;
    toReturn += "." ;
    temp = minute() ;
    if(temp < 10)
      toReturn += "0" ;
    toReturn += temp ;
    toReturn += "." ;
    temp = second() ;
    if(temp < 10)
      toReturn += "0" ;
    toReturn += temp ;
    if(bPM)
      toReturn += " PM" ;
    else
      toReturn += " AM " ;
  }
  else{
    toReturn += " " ;
    
    temp = hour() ;
    if(temp > 12){
      temp -= 12 ;
      bPM = true ;
    }

    if(bPM)
      toReturn += " " ;
      
    if(temp < 10)
      toReturn += " " ;
    toReturn += temp ;
    toReturn += " " ;
    temp = minute() ;
    if(temp < 10)
      toReturn += "0" ;
    toReturn += temp ;
    if(bPM)
      toReturn += " PM" ;
    else
      toReturn += " AM" ;
  }

  return toReturn ;
}

String dateDisplay(){
  String toReturn = " " ;
  int temp ;

  temp = month() ;
  if(temp < 10)
    toReturn += " " ;
  toReturn += temp ;
  toReturn += "." ;
  temp = day() ;
  if(temp < 10)
    toReturn += "0" ;
  toReturn += temp ;
  toReturn += "." ;
  temp = year() ;
  if(temp % 100 < 10)
    toReturn += "0" ;
  toReturn += temp % 100 ;

  toReturn += "   " ;

  return toReturn ;
}

String tempDisplay(){
  String toReturn = " " ;

  if(fahrenheit < 100.0)
    toReturn += " " ;
  if(fahrenheit < 10.0)
    toReturn += " " ;

  toReturn += fahrenheit ;
  toReturn += " F  " ;

  return toReturn ;
}

String humDisplay(){
  String toReturn = "  " ;
  if(humidity < 100.0)
    toReturn += " " ;
  if(humidity < 10.0)
    toReturn += " " ;

  toReturn += humidity ;

  toReturn += " % " ;
  
  return toReturn ;
}

String pressureDisplay(){
  String toReturn = "  " ;
  float displayPressure = relPressure * 0.1 ;
  if(!bRelativePressure)
    displayPressure = pressure * 0.1 ;
  if(displayPressure < 100.0)
    toReturn += " " ;

  toReturn += displayPressure ;

  toReturn += " kP" ;
  
  return toReturn ;
}

void scanI2cBus(){
  byte error, address;
   
  nDevices = 0;
  for(address = 1; address < 127; address++ )
  {
    Wire.beginTransmission(address);
    error = Wire.endTransmission();
    if (error != 2)
    {
      if(nDevices > 0)
        Serial.print(", ") ;
      i2cDevices[nDevices] = address ;
      Serial.print(address) ;
      nDevices++;
    }   
  }
}

void readTubeDrivers(){
  char replyData[7] ;
  byte driverCount = 0 ;
  
  for(int i = 0; i <10; i++){
    driverAddresses[i] = 255 ;
    tubeTypes[i] = 0 ;
    tubeStates[i] = 0 ;
    currentDisplays[i] = 0 ;
    driverFirmwares[i] = 0 ;
  }

  for(int i = 0; i < 13; i++){
    if(i2cDevices[i] != 255){
      Wire.requestFrom(i2cDevices[i], (byte)7) ;

      while(Wire.available() < 7){} ;
  
      for(int i = 0; i < 7; i++){
        replyData[i] = Wire.read() ;
      }  
  
      if(replyData[0] == 'D'){
        driverAddresses[driverCount] = replyData[2] ;
        driverFirmwares[driverCount] = replyData[1] ;
        if(replyData[3] == '2'){
          if(replyData[4] == 'A')
            tubeTypes[driverCount] = 1 ;
          else if(replyData[4] == 'B')
            tubeTypes[driverCount] = 2 ;
          else
            tubeTypes[driverCount] = 255 ;
        }
        else if(replyData[3] == '5'){
          if(replyData[4] == 'A')
            tubeTypes[driverCount] = 3 ;
          else if(replyData[4] == 'B')
            tubeTypes[driverCount] = 4 ;
          else
            tubeTypes[driverCount] = 255 ;
        }
        else
          tubeTypes[driverCount] = 0 ;
  
        tubeStates[driverCount] = replyData[5] ;
        currentDisplays[driverCount] = replyData[6] ;
        driverCount++ ;
      }
    }
  }
}

void listDrivers(){
  Serial.print("Driver list: ") ;
  for(int i = 0; i < 10; i++){
    if(driverAddresses[i] != 255){
      if(i > 0)
        Serial.print(", ") ;
      if(i % 2 == 0)
        Serial.println() ;
        
      Serial.print(driverAddresses[i]) ;
      Serial.print(" version: ") ;
      Serial.print(driverFirmwares[i]) ;
      Serial.print(" running: ") ;
      switch(tubeTypes[i]){
        case 1: Serial.print("IN-12A") ;
                break ;
        case 2: Serial.print("IN-12B") ;
                break ;
        case 3: Serial.print("IN-15A") ;
                break ;
        case 4: Serial.print("IN-15B") ;
                break ;
        default:Serial.print("No tube") ;
                break ;
      }
      switch(tubeStates[i]){
        case 'E': Serial.print(" in normal mode displaying: ") ;
                  Serial.print(currentDisplays[i]) ;
                  break ;
        case 'D': Serial.print(" and is disabled") ;
                  break ;
        case 'T': Serial.print(" in test mode") ;
                  break ;
        default:  Serial.print(" in an invalid mode") ;
                  break ;
      }
    }
  }
  Serial.println() ;
}

void driverReset(){                                                        //Full reset of driver boards and toWrite data
  Serial.println("Driver reset") ;
  digitalWrite(resetPin, LOW) ;                                           
  delay(200) ;
  digitalWrite(resetPin, HIGH) ;
  delay(3000) ;

  for(int i = 0; i < 10; i++){
    writeDecimals[i] = false ;
    toWrite[i] = ' ' ;
    currentDisplays[i] = ' ' ;
    displayedDecimals[i] = false ;
  }

  readTubeDrivers() ;
  listDrivers() ;
}

void writeAll(){
  for(int i = 0; i < 10; i++){
    if(driverAddresses[i] != 255){
      if(writeDecimals[i] != displayedDecimals[i] || toWrite[i] != currentDisplays[i]){
        Wire.beginTransmission(driverAddresses[i]) ;
        if(writeDecimals[i])
          Wire.write('.') ;
        displayedDecimals[i] = writeDecimals[i] ;
        Wire.write(toWrite[i]) ;
        currentDisplays[i] = toWrite[i] ;
        Wire.endTransmission() ;
      }
    }
  }
}

void allTubes(char command){
  for(int i = 0; i < 10; i++){
    if(driverAddresses[i] != 255){
      Wire.beginTransmission(driverAddresses[i]) ;
      Wire.write(command) ;
      Wire.endTransmission() ;
    }
  }
}

void staggeredWrite(){
  toWrite[0] = '0' ;
  toWrite[1] = '1' ;
  toWrite[2] = '2' ;
  toWrite[3] = '3' ;
  toWrite[4] = '4' ;
  toWrite[5] = '5' ;
  toWrite[6] = '6' ;
  toWrite[7] = 'A' ;
  toWrite[8] = 'M' ;
  toWrite[9] = 'P' ;
      
  for(int i = 0; i < 10; i++)
    writeDecimals[i] = false ;
    
  writeDecimals[1] = true ;
        
  for(int i = 0; i < 10; i++){
    if(driverAddresses[i] != 255 && testStage - 2 >= i){
      if(writeDecimals[i] != displayedDecimals[i] || toWrite[i] != currentDisplays[i]){
        Wire.beginTransmission(driverAddresses[i]) ;
        if(writeDecimals[i])
          Wire.write('.') ;
        displayedDecimals[i] = writeDecimals[i] ;
        Wire.write(toWrite[i]) ;
        currentDisplays[i] = toWrite[i] ;
        Wire.endTransmission() ;
      }
    }
  }
}

void mapToDisplay(String toDisplay){
  byte numTubes = 0 ;
  byte numDecimals = 0 ;
  byte decimalOffset = 0 ;
  
  for(int i = 0; i < 10; i++){
    if(driverAddresses[i] != 255)
      numTubes++ ;
    writeDecimals[i] = false ;
    toWrite[i] = ' ' ;
  }

  for(int i = 0; i < toDisplay.length(); i++)
    if(toDisplay.charAt(i) == '.')
      numDecimals++ ;
      
  if(toDisplay.length() - numDecimals > numTubes){
    Serial.print(toDisplay) ;
    Serial.println(" is too long to display") ;
    toDisplay.substring(0, numTubes - 1 - numDecimals) ;
  }
  else if(toDisplay.length() - numDecimals < numTubes){
    for(int i = 0; i < numTubes - toDisplay.length() + numDecimals; i++)
      toDisplay += " " ;
  }

  for(int i = 0; i < 10; i++){
    if(toDisplay.charAt(i + decimalOffset) == '.'){
      writeDecimals[i] = true ;
      decimalOffset++ ;
    }
    if((toDisplay.charAt(i + decimalOffset) == ' ') || (requiredTube(toDisplay.charAt(i + decimalOffset)) == tubeTypes[i]) && tubeTypes[i] != 255)
      toWrite[i] = toDisplay.charAt(i + decimalOffset) ;
    else if(requiredTube(toDisplay.charAt(i + decimalOffset)) == 1 && tubeTypes[i] == 2)
      toWrite[i] = toDisplay.charAt(i + decimalOffset) ;
    else {
      Serial.print("Tube ") ;
      Serial.print(i) ;
      Serial.print(" cannot display: ") ;
      Serial.println(toDisplay.charAt(i + decimalOffset)) ;
    }

    if(tubeTypes[i] != 2 && writeDecimals[i]){
      Serial.print("Tube ") ;
      Serial.print(i) ;
      Serial.println(" cannot display: .") ;
    }
  }
}

byte requiredTube(char toDisplay){
  if(toDisplay == ' ')
    return 0 ;
  if(toDisplay == '0' || toDisplay == '1' || toDisplay == '2' || toDisplay == '3' || toDisplay == '4' || 
    toDisplay == '5' || toDisplay == '6' || toDisplay == '7' || toDisplay == '8' || toDisplay == '9')
    return 1 ;
  if(toDisplay == '.')
    return 2 ;
  if(toDisplay == 'u' || toDisplay == 'P' || toDisplay == '-' || toDisplay == '+' || toDisplay == 'm' || 
    toDisplay == 'M' || toDisplay == 'k' || toDisplay == 'p' || toDisplay == '%' || toDisplay == 'n')
    return 3 ;
  if(toDisplay == 'W' || toDisplay == 'F' || toDisplay == 'z' || toDisplay == 'H' || toDisplay == 'V' || 
    toDisplay == 'S' || toDisplay == 'O' || toDisplay == 'A')
    return 4 ;
  return 255 ;
}

bool checkTouch(){
  long check = displayHousing.capacitiveSensor(100) ;
  
  if(check > 3400)
    return true ;
  return false ;
}

void fullReport(){  
  Serial.println("Full readout:") ;
  Serial.print("Uptime: ") ;
  Serial.print(millis()) ;
  Serial.print(", system time: ") ;
  Serial.print(clockDisplay()) ;
  Serial.print(" ") ;
  Serial.println(dateDisplay()) ;
  Serial.print("Sensors: temp ") ;
  Serial.print(fahrenheit) ;
  Serial.print("F, ") ;
  Serial.print(humidity) ;
  Serial.print("% humidity, ") ;
  Serial.print(pressure) ;
  Serial.print(" millibar, ") ;
  Serial.print(brightness) ;
  Serial.print(" brightness,") ;
  Serial.print(checkTouch()) ;
  Serial.println(" touch button") ;
  Serial.print("Displaying: ") ;
  for(int i = 0; i < 10; i++){
    if(displayedDecimals[i])
      Serial.print(".") ;
    Serial.print(currentDisplays[i]) ;
  }
  Serial.println() ;
  listDrivers() ;
}

/* Serial commands:
 * FR - Full report
 * DW - Write the following to display
 * CM - Display control mode
 * XD - Clear display
 * CD - Currently displaying
 * TM - Test mode
 * NM - Normal mode
 * RT - Read tubes
 * RS - Reset drivers
 * RC - Read current settings (ones that can be stored in EEPROM)
 * 
 * SA - Set altitude
 * SP - Set bRelativePressure Y/N
 * SS - Set bDisplaySeconds Y/N
 * SF - Set date display frequency
 * ST - Set time display duration (milliseconds)
 * SD - Set date/temp/humidity/pressure display duration (milliseconds)
 * SX - Driver lights off
 * SL - Driver lights on
 * DF - Revert to default settings
 * LD - Load settings from eeprom
 * 
 * EEPROM - Write current settings to eeprom
 */

void recieveSerial(){
  char inputBuffer[22] ;
  int counter = 0 ;
  String toWrite = "" ;
  EEPROMBlock readFromEEPROM ;

  for(int i = 0; i < 22; i++)
    inputBuffer[i] = ' ' ;
  
  if(Serial.available()){
    Serial.readBytesUntil('\n', inputBuffer, 22) ;
    while(Serial.available())
      Serial.read() ;

    if(inputBuffer[0] == 'F' && inputBuffer[1] == 'R'){                     //Report
      fullReport() ;
    }
    else if(inputBuffer[0] == 'D' && inputBuffer[1] == 'W'){                // Write
      counter = 2 ;
      while(counter < 22){
        if(counter < 12 || inputBuffer[counter] != ' ')
          toWrite += inputBuffer[counter] ;
        counter++ ;
      }
      mapToDisplay(toWrite) ;
      Serial.print("Wrote: ") ;
      Serial.println(toWrite) ;
    }
    else if(inputBuffer[0] == 'C' && inputBuffer[1] == 'M'){                //Serial control
      testStage = 0 ;
      displayStage = 0 ;
      allTubes('N') ;
      Serial.println("Controlled by host") ;
    }
    else if(inputBuffer[0] == 'X' && inputBuffer[1] == 'D'){                //Clear
      mapToDisplay("          ") ;
      writeAll() ;
      Serial.println("Display Cleared") ;
    }
    else if(inputBuffer[0] == 'C' && inputBuffer[1] == 'D'){                //Read
      Serial.print("Current display: ") ;
      for(int i = 0; i < 10; i++){
        if(displayedDecimals[i])
          Serial.print(".") ;
        Serial.print(currentDisplays[i]) ;
      }
      Serial.println() ;
    }
    else if(inputBuffer[0] == 'T' && inputBuffer[1] == 'M'){                //Test
      displayStage = 0 ;
      testStage = 1 ;
      lastStageChange = millis() ;
      allTubes('T') ;
      touchStart = 0 ;
      Serial.println("Enter test mode") ;
    }
    else if(inputBuffer[0] == 'N' && inputBuffer[1] == 'M'){                //Normal
      displayStage = 1 ;
      testStage = 0 ;
      lastStageChange = millis() ;
      allTubes('N') ;
      touchStart = 0 ;
      for(int i = 0; i < 10; i++){
        currentDisplays[i] = ' ' ;
       displayedDecimals[i] = false ;
      }
      Serial.println("Enter normal mode") ;
    }
    else if(inputBuffer[0] == 'R' && inputBuffer[1] == 'T'){                //Tubes
      readTubeDrivers() ;
      listDrivers() ;
    }
    else if(inputBuffer[0] == 'R' && inputBuffer[1] == 'S'){                //Reset drivers
      driverReset() ;
    }
    else if(inputBuffer[0] == 'R' && inputBuffer[1] == 'C'){                //Current storable settings
      Serial.println("EEPROM Storable settings:") ;
      Serial.print("Display seconds? ") ;
      Serial.print(bDisplaySeconds) ;
      Serial.print(", altitude: ") ;
      Serial.print(altitudeM) ;     
      Serial.print("m, show relative pressure? ") ;                             
      Serial.print(bRelativePressure) ;
      Serial.print(",cycles per date display: ") ;
      Serial.println(dateFrequency) ;                            
      Serial.print("Date/Temp/Hum/Pres display duration: ") ;
      Serial.print(displayStageDuration) ;
      Serial.print(",time display duration: ") ;
      Serial.print(displayStagePrimaryDuration) ;
      Serial.print(", driver LEDs enabled? ") ;
      Serial.println(bDriverLightEnable) ;
    }
    else if(inputBuffer[0] == 'S' && inputBuffer[1] == 'A'){                //Altitude
      counter = 2 ;
      while(counter < 22){
        toWrite += inputBuffer[counter] ;
        counter++ ;
      }
  
      altitudeM = toWrite.toInt() ;
      Serial.print("Altitude: ") ;
      Serial.print(altitudeM) ;
      Serial.println("m") ;
    }
    else if(inputBuffer[0] == 'S' && inputBuffer[1] == 'P'){                //Relative pressure
      if(inputBuffer[2] == 'Y')
        bRelativePressure = true ;
      else if(inputBuffer[2] == 'N')
        bRelativePressure = false ;

      Serial.print("Show relative pressure? ") ;
      if(bRelativePressure)
        Serial.println("Yes") ;
      else
        Serial.println("No") ;
    }
    else if(inputBuffer[0] == 'S' && inputBuffer[1] == 'S'){                //Display seconds
      if(inputBuffer[2] == 'Y')
        bDisplaySeconds = true ;
      else if(inputBuffer[2] == 'N')
        bDisplaySeconds = false ;

      Serial.print("Display seconds? ") ;
      if(bDisplaySeconds)
        Serial.println("Yes") ;
      else
        Serial.println("No") ;
    }
    else if(inputBuffer[0] == 'S' && inputBuffer[1] == 'F'){                //Date frequency
      counter = 2 ;
      while(counter < 22){
        toWrite += inputBuffer[counter] ;
        counter++ ;
      }
  
      dateFrequency = toWrite.toInt() ;

      Serial.print("Cycles between date displays: ") ;
      Serial.println(dateFrequency) ;
    }
    else if(inputBuffer[0] == 'S' && inputBuffer[1] == 'T'){                //Time display duration
      counter = 2 ;
      while(counter < 22 && inputBuffer[counter] != ' '){
        toWrite += inputBuffer[counter] ;
        counter++ ;
      }
  
      displayStagePrimaryDuration = toWrite.toFloat() ;
      Serial.print("Time display duration: ") ;
      Serial.print(displayStagePrimaryDuration) ;
      Serial.println("ms") ;
    }
    else if(inputBuffer[0] == 'S' && inputBuffer[1] == 'D'){                //Alt duration
      counter = 2 ;
      while(counter < 22){
        toWrite += inputBuffer[counter] ;
        counter++ ;
      }
  
      displayStageDuration = toWrite.toFloat() ;
      Serial.print("Date/Temp/Hum/Press display duration: ") ;
      Serial.print(displayStageDuration) ;
      Serial.println("ms") ;
    }
    else if(inputBuffer[0] == 'D' && inputBuffer[1] == 'F'){                //Revert to defaults
      displayStage = 1 ;
      testStage = 0 ;
      lastStageChange = millis() ;
      allTubes('N') ;
      touchStart = 0 ;
      for(int i = 0; i < 10; i++){
        currentDisplays[i] = ' ' ;
       displayedDecimals[i] = false ;
      }
      altitudeM = 132 ;                                   //Burtonsville, MD
      bDisplaySeconds = true ;
      bRelativePressure = true ;
      dateFrequency = 3 ;                                 
      displayStageDuration = 3000 ;
      displayStagePrimaryDuration = 25000 ;    
      bDriverLightEnable = true ;
      Serial.println("Defaults restored") ;                  
    }
    else if(inputBuffer[0] == 'L' && inputBuffer[1] == 'D'){                //Load from eeprom
      EEPROM.get(0, readFromEEPROM) ;

      bDisplaySeconds = readFromEEPROM.bDisplaySeconds ;
      altitudeM = readFromEEPROM.altitudeM ;                                  
      bRelativePressure = readFromEEPROM.bRelativePressure ;
      dateFrequency = readFromEEPROM.dateFrequency ;                            
      displayStageDuration = readFromEEPROM.displayStageDuration ;
      displayStagePrimaryDuration = readFromEEPROM.displayStagePrimaryDuration ;
      bDriverLightEnable = readFromEEPROM.bDriverLightEnable ;
    
      if(!bDriverLightEnable)                                             
        allTubes('Q') ;
      else
        allTubes('L') ;
      Serial.println("EEPROM settings loaded") ;
    }
    else if(inputBuffer[0] == 'S' && inputBuffer[1] == 'X'){                //Load from eeprom
      allTubes('Q') ;
      bDriverLightEnable = false ;
      Serial.println("Driver LEDs disabled") ;
    }
    else if(inputBuffer[0] == 'S' && inputBuffer[1] == 'L'){                //Load from eeprom
      allTubes('L') ;
      bDriverLightEnable = true ;
      Serial.println("Driver LEDs enabled") ;
    }
    else if(inputBuffer[0] == 'E' && inputBuffer[1] == 'E' && inputBuffer[2] == 'P' && 
          inputBuffer[3] == 'R' && inputBuffer[4] == 'O' && inputBuffer[5] == 'M'){                //Write to eeprom

      readFromEEPROM.bDisplaySeconds = bDisplaySeconds ;
      readFromEEPROM.altitudeM = altitudeM ;                                  
      readFromEEPROM.bRelativePressure = bRelativePressure ;
      readFromEEPROM.dateFrequency = dateFrequency ;                            
      readFromEEPROM.displayStageDuration = displayStageDuration ;
      readFromEEPROM.displayStagePrimaryDuration = displayStagePrimaryDuration ;
      readFromEEPROM.bDriverLightEnable = bDriverLightEnable ;

      EEPROM.put(0, readFromEEPROM) ;

      Serial.println("EEPROM settings stored") ;
    }
    else
      Serial.println("Unrecognized command") ;
  }
}

