/*
 * Driver_Module by Jonathan Zepp - @DaJMasta
 * Version 3 - December 7, 2015
 * For the Not Your Average Nixie Clock project
 * 
 * The firmware for each driver board ATMega328.  Includes tube identification and checking, some error reporting over an auxiliary serial,
 * autogenerating an i2c address according to resistors in the backplane, a no-input test mode, and plenty more.
 * 
 * Built in Arduino iDE 1.6.5
 * Uses a modified arduino breadboard hardware file to support both the 328P and 328 processors running the 8MHz internal clock
 */

#define LEDPin 13
#define HVCheckPin A0
#define tubeTypeCheckPin A1
#define typeSwitchPin A2
#define i2cAddressResistorPin A3
#define tube2Pin 2
#define tube3Pin 3
#define tube4Pin 4
#define tube5Pin 12
#define tube6Pin 11
#define tube7Pin 10
#define tube8Pin 9
#define tube9Pin 5
#define tube10Pin 6
#define tube11Pin 7
#define tube12Pin 8
#define indicatorDelay 250
#define testInterval 1000

#include <Wire.h>

byte tubeType, i2cAddress, testModeSelect ;
bool bTubeCheck, bStatusLED, bDisabled, bHighVoltage, bIsIN12, bDecimalPoint, bEnableLED ;
char input, currentDisplay ;
long lastLEDOn, lastCommand, lastTestSwitch ;
byte versionNumber = 3 ;

void setup() {
  int raw ;
  pinMode(LEDPin, OUTPUT) ;
  pinMode(HVCheckPin, INPUT) ;
  pinMode(tubeTypeCheckPin, INPUT) ;
  pinMode(typeSwitchPin, INPUT) ;
  pinMode(i2cAddressResistorPin, INPUT) ;
  pinMode(tube2Pin, OUTPUT) ;
  pinMode(tube3Pin, OUTPUT) ;
  pinMode(tube4Pin, OUTPUT) ;
  pinMode(tube5Pin, OUTPUT) ;
  pinMode(tube6Pin, OUTPUT) ;
  pinMode(tube7Pin, OUTPUT) ;
  pinMode(tube8Pin, OUTPUT) ;
  pinMode(tube9Pin, OUTPUT) ;
  pinMode(tube10Pin, OUTPUT) ;
  pinMode(tube11Pin, OUTPUT) ;
  pinMode(tube12Pin, OUTPUT) ;

  Serial.begin(57600) ;

  raw = (analogRead(i2cAddressResistorPin) / 7) + 1 ;                                     //Find i2c address from resistor in backplane
  i2cAddress = raw ;
  Serial.print("Address: ") ;
  Serial.println(i2cAddress) ;
  Wire.begin(i2cAddress) ;
  Wire.onReceive(receiveEvent) ;
  Wire.onRequest(requestEvent);

  Serial.print("Firmware version: ") ;
  Serial.println(versionNumber) ;

  testModeSelect = 1 ;
  bStatusLED = true ;
  bDisabled = false ;
  bTubeCheck = false ;
  bIsIN12 = false ;
  bHighVoltage = false ;
  bDecimalPoint = false ;
  tubeType = 0 ;
  lastLEDOn = millis() ;
  lastTestSwitch = millis() ;
  lastCommand = millis() ;
  currentDisplay = 0 ;
  bEnableLED = true ;
  
  checkTubeType() ; 

  tubeWrite() ;

  Serial.println("Initialized!") ;
}

void loop() {
  if(bEnableLED){
    if(lastLEDOn + indicatorDelay <= millis())
      bStatusLED = false ;
    else
      bStatusLED = true ;
  }
  else
    bStatusLED = false ;

  digitalWrite(LEDPin, bStatusLED) ;

  if(testModeSelect > 0)
    testModeCheck() ;
  tubeWrite() ;
}

void testModeCheck(){                                                       //Incrementing digits to test elements
  if(lastTestSwitch + testInterval <= millis()){
      testModeSelect++ ;
      lastTestSwitch = millis() ;
      
      if(testModeSelect > 12){
        testModeSelect = 1 ;
        Serial.println("Test mode rollover") ;
        lastLEDOn = millis() ;
      }
  }
}

void tubeWrite(){
  byte toDisplay = 0 ;

  if(testModeSelect > 0)
    toDisplay = testModeSelect + 1 ;
  else
    toDisplay = mapCharacter() ;

  digitalWrite(tube2Pin, LOW) ;
  digitalWrite(tube3Pin, LOW) ;
  digitalWrite(tube4Pin, LOW) ;
  digitalWrite(tube5Pin, LOW) ;
  digitalWrite(tube6Pin, LOW) ;
  digitalWrite(tube7Pin, LOW) ;
  digitalWrite(tube8Pin, LOW) ;
  digitalWrite(tube9Pin, LOW) ;
  digitalWrite(tube10Pin, LOW) ;
  digitalWrite(tube11Pin, LOW) ;
  digitalWrite(tube12Pin, LOW) ;

  if(bDisabled)
    return ;

  switch(toDisplay){
      case 2: digitalWrite(tube2Pin, HIGH) ;
              break ;
      case 3: digitalWrite(tube3Pin, HIGH) ;
              break ;
      case 4: digitalWrite(tube4Pin, HIGH) ;
              break ;
      case 5: digitalWrite(tube5Pin, HIGH) ;
              break ;
      case 6: digitalWrite(tube6Pin, HIGH) ;
              break ;
      case 7: digitalWrite(tube7Pin, HIGH) ;
              break ;
      case 8: digitalWrite(tube8Pin, HIGH) ;
              break ;
      case 9: digitalWrite(tube9Pin, HIGH) ;
              break ;
      case 10: digitalWrite(tube10Pin, HIGH) ;
              break ;
      case 11: digitalWrite(tube11Pin, HIGH) ;
              break ;
      case 12: digitalWrite(tube12Pin, HIGH) ;
              break ;
      default: break ;
    }

  if(bDecimalPoint && tubeType == 2)
    digitalWrite(tube12Pin, HIGH) ;
}

byte mapCharacter(){
  switch(tubeType){
    case 1:   switch(currentDisplay){
                case '0': return 2 ;
                case '1': return 11 ;
                case '2': return 10 ;
                case '3': return 9 ;
                case '4': return 8 ;
                case '5': return 7 ;
                case '6': return 6 ;
                case '7': return 5 ;
                case '8': return 4 ;
                case '9': return 3 ;
                case ' ': return 0 ;
                default:  Serial.print("Bad Character for IN12A: ") ;
                          Serial.println(currentDisplay) ;
                          return 0 ;
              }
              break ;
    case 2:   switch(currentDisplay){
                case '0': return 2 ;
                case '1': return 11 ;
                case '2': return 10 ;
                case '3': return 9 ;
                case '4': return 8 ;
                case '5': return 7 ;
                case '6': return 6 ;
                case '7': return 5 ;
                case '8': return 4 ;
                case '9': return 3 ;
                case '.': return 12 ;
                case ' ': return 0 ;
                default:  Serial.print("Bad Character for IN12B: ") ;
                          Serial.println(currentDisplay) ;
                          return 0 ;
              }
              break ;
    case 3:   switch(currentDisplay){
                case 'u': return 2 ;
                case 'P': return 3 ;
                case '-': return 4 ;
                case '+': return 5 ;
                case 'm': return 6 ;
                case 'M': return 7 ;
                case 'k': return 8 ;
                case 'p': return 9 ;                                  //Displays П
                case '%': return 10 ;
                case 'n': return 11 ;
                case ' ': return 0 ;
                default:  Serial.print("Bad Character for IN15A: ") ;
                          Serial.println(currentDisplay) ;
                          return 0 ;
              }
              break ;
    case 4:   switch(currentDisplay){
                case 'W': return 2 ;
                case 'F': return 3 ;
                case 'z': return 5 ;                                   //Displays Hz
                case 'H': return 6 ;
                case 'V': return 7 ;
                case 'S': return 8 ;
                case 'O': return 10 ;                                  //Displays Ω
                case 'A': return 11 ;
                case ' ': return 0 ;
                default:  Serial.print("Bad Character for IN15B: ") ;
                          Serial.println(currentDisplay) ;
                          return 0 ;
              }
              break ;
    default:  Serial.println("Bad tube type") ;
              return 0 ;
  }
}

/*                                              Tube Types:
 *                                              0 - None
 *                                              1 - in12a
 *                                              2 - in12b
 *                                              3 - in15a
 *                                              4 - in15b
 */
void checkTubeType(){                                 
  int raw, raw2, raw3 ;
  raw = analogRead(typeSwitchPin) ;                                  
  raw2 = analogRead(HVCheckPin) ;

  if(raw2 > 100)
    bHighVoltage = true ;
  else {
    bHighVoltage = false ;
    bDisabled = true ;
    tubeType = 0 ;
    Serial.println("No high voltage found") ;
    return ;
  }
  
  if(raw > 512){
    bIsIN12 = true ;    

    raw3 = analogRead(tubeTypeCheckPin) ;
    digitalWrite(tube12Pin, HIGH) ;
    if(raw3 >= analogRead(tubeTypeCheckPin) + 10)
      bTubeCheck = true ;
    else
      bTubeCheck = false ;
    digitalWrite(tube12Pin, LOW) ;
    
    if(bTubeCheck)
      tubeType = 2 ;
    else 
      tubeType = 1 ;
  }
  else {
    bIsIN12 = false ;

    raw3 = analogRead(tubeTypeCheckPin) ;
    
    if(raw3 > 40)
      bTubeCheck = true ;
    else
      bTubeCheck = false ;
    
    if(!bTubeCheck)
      tubeType = 4 ;
    else 
      tubeType = 3 ;
  }

  switch(tubeType){
    case 1:   Serial.println("IN-12A tube found") ;
              break ;
    case 2:   Serial.println("IN-12B tube found") ;
              break ;
    case 3:   Serial.println("IN-15A tube found") ;
              break ;
    case 4:   Serial.println("IN-15B tube found") ;
              break ;
    default:  Serial.println("No tube found") ;
  }
}

void receiveEvent(int byteCount)
{
  char oldDisplay = currentDisplay ;
  Serial.print("Received: ") ;
  bDecimalPoint = false ;
  
  while(Wire.available()){
    currentDisplay = Wire.read() ;  
    if(currentDisplay == '.')
      bDecimalPoint = true ;  
    Serial.print(currentDisplay) ; 
  }
  switch(currentDisplay){
    case 'E': bDisabled = false ;                     //Enable
              Serial.println(" - Enabled") ;
              currentDisplay = ' ' ;
              break ;
    case 'D': bDisabled = true ;                     //Disable
              Serial.println(" - Disabled") ;
              currentDisplay = ' ' ;
              break ;
    case 'N': testModeSelect = 0 ;                    //Normal mode
              Serial.println(" - Normal run mode") ;
              currentDisplay = ' ' ;
              break ;
    case 'T': testModeSelect = 1 ;                    //Test mode
              Serial.println(" - Test mode") ;
              currentDisplay = ' ' ;
              break ;
    case 'R': checkTubeType() ;                        //Recheck tube
              currentDisplay = oldDisplay ;
              break ;   
    case 'L': bEnableLED = true ;                     //Turn on status LED
              currentDisplay = oldDisplay ;
              break ;
    case 'Q': bEnableLED = false ;                    //Turn off status LED
              currentDisplay = oldDisplay ;
              break ;          
    case 255: Serial.println(" - Device check") ;
              break ;  
    default:  break ;
  }
  if(currentDisplay == '.')
    currentDisplay = ' ' ;

  if(testModeSelect > 0)
    currentDisplay = oldDisplay ;
  lastLEDOn = millis() ;
}

/*                                                Return packet:
 *                                      D firmware version, i2c address tube type, mode, currently displayed
 *                                      D0*2BE6 - 0 firmware version, * i2c address, in12b tube, display enabled, displaying 6 - 7 bytes
 */
void requestEvent(){
  char toReturn[7] ;
  toReturn[0] = 'D' ;
  toReturn[1] = versionNumber ;
  toReturn[2] = i2cAddress ;
  switch(tubeType){
    case 0: toReturn[3] = '0' ;
            toReturn[4] = '0' ;
            break ;
    case 1: toReturn[3] = '2' ;
            toReturn[4] = 'A' ;
            break ;
    case 2: toReturn[3] = '2' ;
            toReturn[4] = 'B' ;
            break ;
    case 3: toReturn[3] = '5' ;
            toReturn[4] = 'A' ;
            break ;
    case 4: toReturn[3] = '5' ;
            toReturn[4] = 'B' ;
            break ;
    default : toReturn[3] = 'E' ;
            toReturn[4] = 'R' ;
            break ;
  }       
  if(bDisabled)
    toReturn[5] = 'D' ;
  else if(testModeSelect > 0)
    toReturn[5] = 'T' ;
  else
    toReturn[5] = 'E' ;
  toReturn[6] = currentDisplay ;

  Wire.write(toReturn) ;

  lastLEDOn = millis() ;
  Serial.println("Info request") ;
}
