#define ENCODER_DO_NOT_USE_INTERRUPTS
#include <Encoder.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <avr/wdt.h> //watchdog timer
#include <Servo.h>
#include <MAX6675.h>

//Constants do not change, also where stuff is plugged in to.
const int ClockPin = 2;
const int DTPin = 3;
const int EncoderButtonPin = 4;
const int ThermoSO = 5;
const int ThermoCS = 6;
const int ThermoSCK = 7;
const int HopperServoPin = 8;  //hopper servo
const int ResetButtonPin = 9; // Define the reset button pin
const int EjectServoPin = 11; //Control for the part ejection
const int InjectPin = 12;   //relay to shoot chamber
const int VisePin = 13;     //relay to shoot air vise

Servo HopperServo; // First servo hopper 
Servo PartEjectorServo; // Second servo part ejector. 
LiquidCrystal_I2C LCD(0x27, 20, 4);
MAX6675 Thermocouple(ThermoSCK, ThermoCS, ThermoSO);

int MenuIndex = 0;
//both in degrees rotation of servo.
int EjectNegative = -45; //Ejector servo position to pull back before push.
int EjectPositive = 90; //Ejector servo position to push part out
unsigned int InjectTime = 0;
unsigned int ViseHoldTime = 0;
float ShotSize = 0.0;
unsigned int NumOfParts = 0;
int EncoderPos = 0; // Declare EncoderPos variable

unsigned long ServoStartTime;
unsigned long ShotEndTime;
int ServoPosition = 0; // 0 degrees
unsigned long LastTempRead = 0;
float CurrentTemp = 0.0;
float WarmingTemp = 170.0; // Minimum temperature to start the job

int DelayFor0 = 500;   // Delay for 0 degrees in milliseconds
int DelayFor180 = 150;  // Delay for 180 degrees in milliseconds
int pos = 0;

int LastClk = HIGH;

enum MenuState {
  MAIN_MENU,
  START_JOB
};

MenuState menuState = MAIN_MENU;
bool yesSelected = true; // Start with "YES" selected

unsigned long startTime = 0; // To store the start time for LED

int sequenceStep = 0; // Track the current part in the sequence
bool isSequenceActive = false; // Flag to track if the sequence is active
unsigned long lastSequenceTime = 0; // Store the last time the sequence started
int partsLeft = 0; // Number of parts left to process

void setup() {
  LCD.init();
  LCD.backlight();

  InjectTime = 10; // Initial inject time in seconds
  ViseHoldTime = 60; // Initial vise hold time in seconds
  ShotSize = 1; // Initial shot size in seconds
  NumOfParts = 5; // Initial number of parts

  LCD.setCursor((20 - 15) / 2, 1);
  LCD.print("Buster Beagle 3D");
  LCD.setCursor((20 - 3) / 2, 2);
  LCD.print("MK3");
  Serial.begin(9600);
  delay(5000);
  LCD.clear();
  LCD.setCursor((20 - 16) / 2, 1);
  LCD.print("Inject Time: " + String(InjectTime) + "s");
  LCD.setCursor((20 - 14) / 2, 2);
  LCD.print("Vise Hold: " + String(ViseHoldTime) + "s");
  LCD.setCursor((20 - 16) / 2, 3);
  LCD.print("Shot Size: " + String(ShotSize, 1) + "s");
  LCD.setCursor((20 - 13) / 2, 4);
  LCD.print("# of Parts: " + String(NumOfParts));
  delay(5000);

  LCD.clear();
  pinMode(ClockPin, INPUT_PULLUP);
  pinMode(DTPin, INPUT_PULLUP);
  pinMode(EncoderButtonPin, INPUT_PULLUP);
  pinMode(VisePin, OUTPUT);  
  pinMode(InjectPin, OUTPUT); 
  pinMode(HopperServoPin, OUTPUT); 
  pinMode(EjectServoPin, OUTPUT); 
  pinMode(ResetButtonPin, INPUT_PULLUP); // Set the reset button pin as input with pull-up resistor
  digitalWrite(VisePin, LOW);  // Open the vise initially
  digitalWrite(InjectPin, LOW); // Open the Pneumatic Cylinder initially
  digitalWrite(HopperServoPin, LOW); // Turn off Hopper initially
  digitalWrite(EjectServoPin, LOW); // Turn off Part Ejector initially

  HopperServo.attach(8);
  PartEjectorServo.attach(11);

  updateLCD();
}

void loop() {
  int newClk = digitalRead(ClockPin);
  int dtValue = digitalRead(DTPin);

  if (newClk != LastClk) {
    LastClk = newClk;

    if (newClk == LOW && dtValue == HIGH) {
      if (menuState == MAIN_MENU) {
        MenuIndex = min(MenuIndex + 1, 7); // Adjusted for the new line
        updateLCD();
      }
    } else if (newClk == LOW && dtValue == LOW) {
      if (menuState == MAIN_MENU) {
        MenuIndex = max(MenuIndex - 1, 0); // Adjusted for the new line
        updateLCD();
      }
    }
  }

  bool buttonState = digitalRead(EncoderButtonPin);
  if (buttonState == LOW && menuState == MAIN_MENU) {
    // Button is pressed while on MAIN_MENU
    if (MenuIndex == 7) { // If "START" is selected
      // Switch to START_JOB state and initialize the sequence

      while (CurrentTemp < WarmingTemp) { // Minimum temperature check
        // Check if the reset button is pressed
        if (digitalRead(ResetButtonPin) == LOW) {
          // Perform a software reset
          wdt_enable(WDTO_15MS); // Enable the watchdog timer with a 15ms timeout
          while (1); // Wait for the watchdog timer to reset the Arduino
        }
        LCD.clear();
        LCD.setCursor(1, 1);
        LCD.print("Warming Up...");
        CurrentTemp = Thermocouple.getCelsius();
        LCD.setCursor(1, 2);
        LCD.print("Current Temp: ");
        LCD.print(CurrentTemp);
        LCD.print("*C");
        delay(3000); // Display error for 3 seconds
      }
      menuState = START_JOB;
      LCD.clear();
      LCD.setCursor((20 - 13) / 2, 1); // Centered horizontally
      LCD.print("Closing Vise");
      startTime = millis(); // Store the start time for LED
      sequenceStep = 0;
      isSequenceActive = true;
      lastSequenceTime = millis();
      digitalWrite(VisePin, HIGH);  // Vise Engaged
      digitalWrite(InjectPin, LOW); // Injection off
      digitalWrite(HopperServoPin, LOW); // Hopper Off
      digitalWrite(EjectServoPin, LOW); // Part Ejector Off
      MenuIndex = 0; // Set the cursor to "Working?"
      partsLeft = NumOfParts; // Initialize partsLeft
    } else {
      // Button is pressed, enter value adjustment mode
      valueAdjustment();
    }
  }

  // Check if it's time to turn on PIN 12 10 seconds after PIN 13 turns on
  if (menuState == START_JOB && isSequenceActive && sequenceStep == 0 && millis() - startTime >= 10000) {
    digitalWrite(InjectPin, HIGH); // Turn on Pin 12
    LCD.clear();
    LCD.setCursor((20 - 10) / 2, 1); // Centered horizontally
    LCD.print("Injecting");
    sequenceStep++;
  }

  // Check if it's time to turn off PIN 12 after InjectTime
  if (menuState == START_JOB && isSequenceActive && sequenceStep == 1 && millis() - startTime >= (InjectTime * 1000 + 10000)) {
    digitalWrite(InjectPin, LOW);
    sequenceStep++;
  }

  // Check if it's time to turn on PIN 8 for ShotSize seconds
  if (menuState == START_JOB && isSequenceActive && sequenceStep == 2) {
    digitalWrite(HopperServoPin, HIGH); // Turn on Pin 8

    // Calculate the time when the shot should end
    ShotEndTime = millis() + (ShotSize * 1000);

    LCD.clear();
    LCD.setCursor((20 - 13) / 2, 1); // Centered horizontally
    LCD.print("Refill Chamber");
    sequenceStep++;

    while (millis() < ShotEndTime) {
        HopperServo.write(ServoPosition); // Set servo to the current position

        // Toggle between 0 and 180 degrees and use the corresponding delay
        if (ServoPosition == 0) {
          delay(DelayFor0);
        } else {
          delay(DelayFor180);
        }

        ServoPosition = (ServoPosition == 0) ? 180 : 0;
    }

    // Stop the servo
    HopperServo.write(90); // Set the servo to the neutral position

    digitalWrite(HopperServoPin, LOW); // Turn off Pin 8

    LCD.clear();
    LCD.setCursor((20 - 13) / 2, 1); // Centered horizontally
    LCD.print("Hold Vise");
    sequenceStep++;
  }

  // Check if it's time to turn off PIN 8 Hopper and end the sequence
  if (menuState == START_JOB && isSequenceActive && sequenceStep == 3 && millis() - startTime >= ((ShotSize + InjectTime) * 1000 + 10000)) {
    digitalWrite(HopperServoPin, LOW);
    HopperServo.write(90); // Turn off Pin 8
    sequenceStep++;
  }

  // Check if it's time to end the sequence after ViseHoldTime
  if (menuState == START_JOB && isSequenceActive && sequenceStep == 4 && millis() - startTime >= ((ViseHoldTime) * 1000)) {
    partsLeft--; // Decrement partsLeft
    if (partsLeft > 0) {
      // If there are more parts left, reset the sequence
      LCD.clear();
      LCD.setCursor((20 - 13) / 2, 1); // Centered horizontally
      LCD.print("Opening Vise");
      LCD.setCursor((20 - 13) / 2, 2); // Centered horizontally on the fourth row
      LCD.print("Parts Left: ");
      LCD.print(partsLeft + 1);
      sequenceStep = 0;
      digitalWrite(VisePin, LOW);
      delay (5000);

      LCD.clear();
      LCD.setCursor((20 - 13) / 2, 1); // Centered horizontally
      LCD.print("Ejecting Part");
      LCD.setCursor((20 - 13) / 2, 2); // Centered horizontally on the fourth row
      LCD.print("Parts Left: ");
      LCD.print(partsLeft + 1);

      HopperServo.write(90); // Set HopperServo to the neutral position
      // Delay to ensure HopperServo has completed its task
      delay(100); // Adjust the delay time as needed
      PartEjectorServo.write(EjectNegative);
      // You may need to adjust the delay to ensure the servo reaches the desired position
      delay(3000);
      PartEjectorServo.write(EjectPositive);
      // You may need to adjust the delay to allow time for the servo to reach the position

      delay(10000); // Wait for 10 seconds
      startTime = millis(); // Store the start time for the next part
      // Turn off PIN13 for 10 seconds
      //digitalWrite(VisePin, LOW);
      //delay(10000); // Wait for 10 seconds
      digitalWrite(VisePin, HIGH); // Turn on PIN13 again
      LCD.clear();
      LCD.setCursor((20 - 13) / 2, 1); // Centered horizontally
      LCD.print("Closing Vise");
    } else {
      // If no parts left, end the sequence
      isSequenceActive = false;
      sequenceStep = 0; // Reset sequenceStep
      digitalWrite(VisePin, LOW); // Turn off the LED on PIN 13
      menuState = MAIN_MENU; // Return to the main menu
      LCD.clear();
      updateLCD();
    }
  }

  // Update the display to show parts left
  if (menuState == START_JOB) {
    LCD.setCursor((20 - 13) / 2, 2); // Centered horizontally on the fourth row
    LCD.print("Parts Left: ");
    LCD.print(partsLeft);
  }

  // Check if the reset button is pressed
  if (digitalRead(ResetButtonPin) == LOW) {
    // Perform a software reset
    wdt_enable(WDTO_15MS); // Enable the watchdog timer with a 15ms timeout
    while (1); // Wait for the watchdog timer to reset the Arduino
  }
}

void adjustValue(int direction) {
  switch (MenuIndex) {
    case 0: // Inject Time
      if (direction > 0 && InjectTime < 65535) {
        InjectTime += direction;
      } else if (direction < 0 && InjectTime > 0) {
        InjectTime += direction;
      }
      break;
    case 1: // Vise Hold
      if (direction > 0 && ViseHoldTime < 65535) {
        ViseHoldTime += direction;
      } else if (direction < 0 && ViseHoldTime > 0) {
        ViseHoldTime += direction;
      }
      break;
    case 2: // Shot Size
      if (direction > 0 && ShotSize < 15.0) {
        ShotSize += 0.1 * direction; // Adjust by 0.1 seconds
      } else if (direction < 0 && ShotSize > 0) {
        ShotSize += 0.1 * direction; // Adjust by -0.1 seconds
      }
      break;
    case 3: // # of Parts
      if (direction > 0 && NumOfParts < 65535) {
        NumOfParts += direction;
      } else if (direction < 0 && NumOfParts > 0) {
        NumOfParts += direction;
      }
      break;
    case 4: //eject pull
        if (direction > 0 && EjectNegative < 180) {
            EjectNegative += direction;
        } else if (direction < 0 && EjectNegative > 0) {
            EjectNegative += direction;
        }
      break;
    case 5: //eject push
        if (direction > 0 && EjectPositive < 180) {
            EjectPositive += direction;
        } else if (direction < 0 && EjectPositive > -90) {
            EjectPositive += direction;
        }
      break;
    case 6: // Warming temp
      if (direction > 0 && WarmingTemp < 500) {
        WarmingTemp += direction * 5; // Adjust by 5 degrees
      } else if (direction < 0 && WarmingTemp > 0) {
        WarmingTemp += direction * 5; // Adjust by -5 degrees
      }
      break;
  }
  updateLCD();
}

void valueAdjustment() {
  int encoderValue = 0;
  int lastEncoderValue = digitalRead(ClockPin);

  while (true) {
    int newEncoderValue = digitalRead(ClockPin);
    if (newEncoderValue != lastEncoderValue) {
      lastEncoderValue = newEncoderValue;
      int dtValue = digitalRead(DTPin);

      if (newEncoderValue == LOW && dtValue == HIGH) {
        adjustValue(1);
      }
      if (newEncoderValue == LOW && dtValue == LOW) {
        adjustValue(-1);
      }
      updateLCD();
    }

    int buttonState = digitalRead(EncoderButtonPin);
    if (buttonState == LOW) {
      break;
    }
  }
}

void updateLCD() {
  LCD.clear(); // Clear the screen

  int pageStart = (MenuIndex / 4) * 4; // Start index of current page
  int arrowRow = MenuIndex % 4;        // Row to draw the selector arrow

  for (int i = 0; i < 4; i++) {
    int itemIndex = pageStart + i;
    LCD.setCursor(0, i); // Line i on the display

    // Draw selector arrow if this is the selected item
    if (i == arrowRow) {
      LCD.print(">");
    }

    // Print label and value
    switch (itemIndex) {
      case 0: {
        LCD.print("Inject Time: ");
        LCD.print(InjectTime);
        LCD.print("s");
        break;
      }

      case 1: {
        LCD.print("Vise Hold: ");
        LCD.print(ViseHoldTime);
        LCD.print("s");
        break;
      }

      case 2: {
        LCD.print("Shot Size: ");
        LCD.print(ShotSize, 1);
        LCD.print("s");
        break;
      }

      case 3: {
        LCD.print("# of Parts: ");
        LCD.print(NumOfParts);
        break;
      }

      case 4: {
        LCD.print("Eject Pull: ");
        LCD.print(EjectNegative);
        LCD.print("*");
        break;
      }

      case 5: {
        LCD.print("Eject Push: ");
        LCD.print(EjectPositive);
        LCD.print("*");
        break;
      }

      case 6: {
        LCD.print("Warming Temp: ");
        LCD.print(WarmingTemp);
        LCD.print("*C");
        break;
      }

      case 7: {
        LCD.print("START");
        break;
      }

      default: {
        LCD.print(" ");
        break;
      }
    }
  }
}