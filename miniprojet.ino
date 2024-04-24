#include <RTClib.h>
#include <LiquidCrystal_AIP31068_I2C.h>
#include <Keypad.h>
#include <ModbusRTUSlave.h>
#include <stdlib.h> //atoi(), itoa()

// Doc : On older boards (Uno, Nano, Mini, and Mega), pins 0 and 1 are used for communication with the computer. Connecting anything to these pins can interfere with that communication, including causing failed uploads to the board.
// Set up components
const int leds[5][3] = {
  {16, 15, 14},  // R, G, B
  {17, 6, 2},  
  {3, 4, 5}, 
  {7, 8, 9}, 
  {10, 11, 12} 
};

const int buzzerPin = 13;

LiquidCrystal_AIP31068_I2C lcd(0x3E,20,2); 

RTC_DS1307 rtc;
DateTime now;

const char keys[4][3] = {
  {'1','2','3'},
  {'4','5','6'},
  {'7','8','9'},
  {'*','0','#'}
};
const byte pin_rows[4] = {A12, A13, A14, A15};
const byte pin_column[3] = {A3, A2, A1};
Keypad keypad = Keypad( makeKeymap(keys), pin_rows, pin_column, 4, 3 );

const byte LDRpin = A0;
int LDRValue = 0;

// Variables to handle passwords
const int passwordLength = 5;
char dayPassword[passwordLength] = "1234"; // Password by default
char nightPassword[passwordLength] = "5678"; 
char enteredPassword[passwordLength];
bool dayPasswordEntered = false;
int errorPassword = 0;
int currentLength = 0;

// Variables to handle LEDS
int currentLed = 0;
bool ledsHigh = false;
unsigned long previousMillis = 0;
unsigned long currentMillis = 0;

// Variables to handle mode
enum {STATUS_IDLE, WAITING, ACCESS, PASSWORD_ERROR, SECURITY};
byte status = STATUS_IDLE;

//All about Modbus
ModbusRTUSlave modbus(Serial1);
uint16_t inputRegisters[2]; // Status, Luminosity
uint16_t holdingRegisters[4]; // Day passwprd, Night password, Security, MAJ
const byte inputStatus = 0;
const byte inputLuminosity = 1;
const byte holdingDayPassword = 0;
const byte holdingNightPassword = 1;
const byte holdingSecurity = 2;
const byte holdingMAJ = 3;

void setup() {
	for (int i = 0; i < 5; i++) {
    	for (int j = 0; j < 3; j++) {
      		pinMode(leds[i][j], OUTPUT);
    	}
  	}

	ledsMode();
	
	pinMode(buzzerPin, OUTPUT);
	for (int i = 0; i < 3; i++) {
	    tone(buzzerPin, 500);
	    delay(1000);
	    noTone(buzzerPin);
	    delay(1000);
  	}

	lcd.init();
	lcd.backlight();
	
	rtc.begin();
	rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
	
	modbus.configureInputRegisters(inputRegisters, 2);
	modbus.configureHoldingRegisters(holdingRegisters, 4);
	setupRegister(); // register can only be initialized within a function
	modbus.begin(1, 9600);
	
	Serial.begin(9600);
}

void loop() {
	now = rtc.now();
	currentTimeLCD();
	ledsMode();
	backlight();

	if (status == SECURITY){
		return;
	}
	//else modbus : à compléter pour sortir du status security
	
	unsigned long lastKeyMillis;
 
	char key = keypad.getKey();
    if (key != NO_KEY) {
			lastKeyMillis = millis();
			if (status == STATUS_IDLE) status = WAITING;
			handlePassword(key);
    }
	
	//Return to IDLE from any status except security if no entry in keypad for 10 seconds
	if (status != SECURITY && millis() - lastKeyMillis > 10000) setIDLE();
	
	//Process Modbus tasks
	modbus.poll();
}

/*--------------Principal functions used in loop--------------*/
// Process the entered letters
void handlePassword(char key) {
	switch(key){
		case '#':
			Serial.println("handlePassword #");
			lcdPrintFirstRow("Goodbye");
			lcdClearPassword();
			delay(3000);
			setIDLE();	
			break;
			
		case '*':
			enteredPassword[passwordLength-1] = '\0';
			//If it is night (between 20h - 7h)
			if(20 <= rtc.now().hour() || rtc.now().hour() <= 7){
				if(dayPasswordEntered){
	        		comparePassword(enteredPassword, nightPassword);
					lcdClearPassword();
				}else{
					comparePassword(enteredPassword, dayPassword);
					dayPasswordEntered = true;
					Serial.println("In boucle night ");	
					Serial.print(dayPasswordEntered);	
					lcdPrintFirstRow("Enter night password");
					lcdClearPassword();
				}
			// If it is day
	     }else{
					comparePassword(enteredPassword, dayPassword);
					lcdClearPassword();
	     }
			break;
			
		default:
			if (currentLength < passwordLength - 1) {
				enteredPassword[currentLength++] = key;
        		enteredPassword[currentLength] = '\0'; // Keep string terminated
        		lcd.setCursor(10, 1);
        		for (int i = 0; i < currentLength; i++) lcd.print("*");
        		tone(buzzerPin, 1000, 200);
			}
		}
}

// Handle leds according to status
void ledsMode() {	
	switch (status) {
    case STATUS_IDLE: // IDLE : all leds green
 			for (int i = 0; i < 5; i++) {
	    	digitalWrite(leds[i][0], LOW);
	      digitalWrite(leds[i][1], HIGH);
	      digitalWrite(leds[i][2], LOW); 
	    }
     	break;

    case WAITING: // Waiting for password : orange from left to right
			currentMillis = millis();
			if (currentMillis - previousMillis > 500) { 
				ledsAllLow();
	      digitalWrite(leds[currentLed][0], HIGH);
	      digitalWrite(leds[currentLed][1], HIGH);
	      digitalWrite(leds[currentLed][2], LOW);
	
	      currentLed = (currentLed + 1) % 5;
	      previousMillis = currentMillis;
      }
      break;

    case PASSWORD_ERROR: // Password error : all leds in red and blink
			currentMillis = millis();
			if (currentMillis - previousMillis > 750){
				ledsHigh = !ledsHigh;
				for (int i = 0; i < 5; i++) {
		    	digitalWrite(leds[i][0], ledsHigh ? HIGH : LOW); 
		      digitalWrite(leds[i][1], LOW);
		      digitalWrite(leds[i][2], LOW);
		    }
				previousMillis = currentMillis;
			}
	    break;
	
    case SECURITY: // Security mode : all leds red
	      for (int i = 0; i < 5; i++) {
	        digitalWrite(leds[i][0], HIGH); 
	        digitalWrite(leds[i][1], LOW); 
	        digitalWrite(leds[i][2], LOW); 
	      }
	      break;
  }	
}

// Display current time at second row
void currentTimeLCD(){
	lcd.setCursor(0, 1);
	  
	if (now.hour() < 10) lcd.print("0"); 
	lcd.print(now.hour(), DEC);
	lcd.print(':');
	if (now.minute() < 10) lcd.print("0"); 
	lcd.print(now.minute(), DEC);
	lcd.print(':');
	if (now.second() < 10) lcd.print("0");
	lcd.print(now.second(), DEC);
}

// Set LiquidCrystal_AIP31068_I2C.h version to 1.0.2 to use backlight() and noBacklight(), which are deleted since 1.0.3
// Although SimulIDE does not support backlight.
void backlight(){
	LDRValue = analogRead(LDRpin);
	inputRegisters[inputLuminosity] = LDRValue;

	if (LDRValue > 512) {
		lcd.noBacklight();
	}else{
		lcd.backlight();
	}
}

// Those code cannot be declared in a gloabl scope, needs to be in a function 
void setupRegister(){
	inputRegisters[inputStatus] = status;
	holdingRegisters[holdingDayPassword] = atoi(dayPassword);
	holdingRegisters[holdingNightPassword] = atoi(nightPassword);
	holdingRegisters[holdingSecurity] = 0;
	holdingRegisters[holdingMAJ] = 0;
}

/*----------Basic functions to serve principal functions---------*/

// Turn off all leds
void ledsAllLow(){
	for (int i = 0; i < 5; i++) {
		digitalWrite(leds[i][0], LOW);
	    digitalWrite(leds[i][1], LOW);
	    digitalWrite(leds[i][2], LOW); 
	}
}

// Print messsages at first rows
void lcdPrintFirstRow(String text){
  lcd.setCursor(0, 0);
  lcd.print("                        ");
  lcd.setCursor(0, 0);
  lcd.print(text);
}

// Clear the area for password in screen 
void lcdClearPassword(){
	lcd.setCursor(10, 1);
	lcd.print("     ");
}

// Handle when password is wrong
void passwordError() {
	status = PASSWORD_ERROR;
	resetPassword();
	
	lcdPrintFirstRow("Access denied");
  tone(buzzerPin, 1000, 2000);
  	
  errorPassword++;
	Serial.print(errorPassword);
    if (errorPassword >= 3) {
      lcdPrintFirstRow("!SECURITY MODE!");
      status = SECURITY;
    }
}

// Handle when password is correct
void access(){
	lcdPrintFirstRow("Access granted");
	tone(buzzerPin, 1000, 1000);
	status = ACCESS;
	resetPassword();
 	errorPassword = 0;
}

// Compare entered password and correct password
void comparePassword(const char* password, const char* correct){
	if(strcmp(password, correct) == 0){
    	access();
    }else{
    	passwordError();
    }
}

// Reset the variable that stores the entered password
void resetPassword(){
	memset(enteredPassword, 0, passwordLength);
	currentLength = 0;
}

// Set all components to IDLE
void setIDLE(){
	status = STATUS_IDLE;
	resetPassword();
	lcdPrintFirstRow("");
	lcdClearPassword();
	dayPasswordEntered= false;
}
