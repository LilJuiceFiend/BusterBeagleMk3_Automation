#define ENCODER_DO_NOT_USE_INTERRUPTS
#include <Encoder.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <avr/wdt.h> //watchdog timer
#include <Servo.h>
#include <MAX6675.h>

//Constants do not change, also where stuff is plugged in to.
const int ClockPin = 2;
const int dtPin = 3;
const int EncoderButtonPin = 4;
const int thermoSO = 5;
const int thermoCS = 6;
const int thermoSCK = 7;
const int HopperServoPin = 8;  //hopper servo
const int ResetButtonPin = 9; // Define the reset button pin
const int EjectServoPin = 11; //Control for the part ejection
const int InjectPin = 12;   //relay to shoot chamber
const int VisePin = 13;     //relay to shoot air vise

Servo HopperServo; // First servo hopper 
Servo PartEjectorServo; // Second servo part ejector. 
LiquidCrystal_I2C lcd(0x27, 20, 4);
MAX6675 thermocouple(thermoSCK, thermoCS, thermoSO);

int menuIndex = 0;
//both in degrees rotation of servo.
int EjectNegative = 90; //Ejector servo position to pull back before push.
int EjectPositive = -45; //Ejector servo position to push part out
unsigned int injectTime = 0;
unsigned int viseHoldTime = 0;
float shotSize = 0.0;
unsigned int numOfParts = 0;
int encoderPos = 0; // Declare encoderPos variable

unsigned long servoStartTime;
unsigned long shotEndTime;
int servoPosition = 0; // 0 degrees
unsigned long lastTempRead = 0;
float currentTemp = 0.0;
float warmingTemp = 170.0; // Minimum temperature to start the job

int delayFor0 = 500;   // Delay for 0 degrees in milliseconds
int delayFor180 = 150;  // Delay for 180 degrees in milliseconds
int pos = 0;

int lastClk = HIGH;

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
  lcd.init();
  lcd.backlight();

  injectTime = 10; // Initial inject time in seconds
  viseHoldTime = 60; // Initial vise hold time in seconds
  shotSize = 1; // Initial shot size in seconds
  numOfParts = 5; // Initial number of parts

  lcd.setCursor((20 - 15) / 2, 1);
  lcd.print("Buster Beagle 3D");
  lcd.setCursor((20 - 3) / 2, 2);
  lcd.print("MK3");
  Serial.begin(9600);
  delay(5000);
  lcd.clear();
  lcd.setCursor((20 - 16) / 2, 1);
  lcd.print("Inject Time: " + String(injectTime) + "s");
  lcd.setCursor((20 - 14) / 2, 2);
  lcd.print("Vise Hold: " + String(viseHoldTime) + "s");
  lcd.setCursor((20 - 16) / 2, 3);
  lcd.print("Shot Size: " + String(shotSize, 1) + "s");
  lcd.setCursor((20 - 13) / 2, 4);
  lcd.print("# of Parts: " + String(numOfParts));
  delay(5000);

  lcd.clear();
  pinMode(ClockPin, INPUT_PULLUP);
  pinMode(dtPin, INPUT_PULLUP);
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
  int dtValue = digitalRead(dtPin);

  if (newClk != lastClk) {
    lastClk = newClk;

    if (newClk == LOW && dtValue == HIGH) {
      if (menuState == MAIN_MENU) {
        menuIndex = min(menuIndex + 1, 5); // Adjusted for the new line
        updateLCD();
      }
    } else if (newClk == LOW && dtValue == LOW) {
      if (menuState == MAIN_MENU) {
        menuIndex = max(menuIndex - 1, 0); // Adjusted for the new line
        updateLCD();
      }
    }
  }

  bool buttonState = digitalRead(EncoderButtonPin);
  if (buttonState == LOW && menuState == MAIN_MENU) {
    // Button is pressed while on MAIN_MENU
    if (menuIndex == 8) {
      // Switch to START_JOB state and initialize the sequence
      if (millis() - lastTempRead > 1000) { // Read every second
        currentTemp = thermocouple.getCelsius();
        Serial.print("Current Temp: ");
        Serial.println(currentTemp);
        lastTempRead = millis();
      }

      while (currentTemp < warmingTemp) { // Minimum temperature check
        lcd.clear();
        lcd.setCursor((20 - 24) / 2, 1); // Centered horizontally
        lcd.print("Warming Up...");
        delay(3000); // Display error for 3 seconds
      }
      menuState = START_JOB;
      lcd.clear();
      lcd.setCursor((20 - 13) / 2, 1); // Centered horizontally
      lcd.print("Closing Vise");
      startTime = millis(); // Store the start time for LED
      sequenceStep = 0;
      isSequenceActive = true;
      lastSequenceTime = millis();
      digitalWrite(VisePin, HIGH);  // Vise Engaged
      digitalWrite(InjectPin, LOW); // Injection off
      digitalWrite(HopperServoPin, LOW); // Hopper Off
      digitalWrite(EjectServoPin, LOW); // Part Ejector Off
      menuIndex = 0; // Set the cursor to "Working?"
      partsLeft = numOfParts; // Initialize partsLeft
    } else {
      // Button is pressed, enter value adjustment mode
      valueAdjustment();
    }
  }

  // Check if it's time to turn on PIN 12 10 seconds after PIN 13 turns on
  if (menuState == START_JOB && isSequenceActive && sequenceStep == 0 && millis() - startTime >= 10000) {
    digitalWrite(InjectPin, HIGH); // Turn on Pin 12
    lcd.clear();
    lcd.setCursor((20 - 10) / 2, 1); // Centered horizontally
    lcd.print("Injecting");
    sequenceStep++;
  }

  // Check if it's time to turn off PIN 12 after injectTime
  if (menuState == START_JOB && isSequenceActive && sequenceStep == 1 && millis() - startTime >= (injectTime * 1000 + 10000)) {
    digitalWrite(InjectPin, LOW);
    sequenceStep++;
  }

  // Check if it's time to turn on PIN 8 for shotSize seconds
  if (menuState == START_JOB && isSequenceActive && sequenceStep == 2) {
    digitalWrite(HopperServoPin, HIGH); // Turn on Pin 8

    // Calculate the time when the shot should end
    shotEndTime = millis() + (shotSize * 1000);

    lcd.clear();
    lcd.setCursor((20 - 13) / 2, 1); // Centered horizontally
    lcd.print("Refill Chamber");
    sequenceStep++;

    while (millis() < shotEndTime) {
        HopperServo.write(servoPosition); // Set servo to the current position

        // Toggle between 0 and 180 degrees and use the corresponding delay
        if (servoPosition == 0) {
          delay(delayFor0);
        } else {
          delay(delayFor180);
        }

        servoPosition = (servoPosition == 0) ? 180 : 0;
    }

    // Stop the servo
    HopperServo.write(90); // Set the servo to the neutral position

    digitalWrite(HopperServoPin, LOW); // Turn off Pin 8

    lcd.clear();
    lcd.setCursor((20 - 13) / 2, 1); // Centered horizontally
    lcd.print("Hold Vise");
    sequenceStep++;
  }

  // Check if it's time to turn off PIN 8 Hopper and end the sequence
  if (menuState == START_JOB && isSequenceActive && sequenceStep == 3 && millis() - startTime >= ((shotSize + injectTime) * 1000 + 10000)) {
    digitalWrite(HopperServoPin, LOW);
    HopperServo.write(90); // Turn off Pin 8
    sequenceStep++;
  }

  // Check if it's time to end the sequence after viseHoldTime
  if (menuState == START_JOB && isSequenceActive && sequenceStep == 4 && millis() - startTime >= ((viseHoldTime) * 1000)) {
    partsLeft--; // Decrement partsLeft
    if (partsLeft > 0) {
      // If there are more parts left, reset the sequence
      lcd.clear();
      lcd.setCursor((20 - 13) / 2, 1); // Centered horizontally
      lcd.print("Opening Vise");
      lcd.setCursor((20 - 13) / 2, 2); // Centered horizontally on the fourth row
      lcd.print("Parts Left: ");
      lcd.print(partsLeft + 1);
      sequenceStep = 0;
      digitalWrite(VisePin, LOW);
      delay (5000);

      lcd.clear();
      lcd.setCursor((20 - 13) / 2, 1); // Centered horizontally
      lcd.print("Ejecting Part");
      lcd.setCursor((20 - 13) / 2, 2); // Centered horizontally on the fourth row
      lcd.print("Parts Left: ");
      lcd.print(partsLeft + 1);

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
      lcd.clear();
      lcd.setCursor((20 - 13) / 2, 1); // Centered horizontally
      lcd.print("Closing Vise");
    } else {
      // If no parts left, end the sequence
      isSequenceActive = false;
      sequenceStep = 0; // Reset sequenceStep
      digitalWrite(VisePin, LOW); // Turn off the LED on PIN 13
      menuState = MAIN_MENU; // Return to the main menu
      lcd.clear();
      updateLCD();
    }
  }

  // Update the display to show parts left
  if (menuState == START_JOB) {
    lcd.setCursor((20 - 13) / 2, 2); // Centered horizontally on the fourth row
    lcd.print("Parts Left: ");
    lcd.print(partsLeft);
  }

  // Check if the reset button is pressed
  if (digitalRead(ResetButtonPin) == LOW) {
    // Perform a software reset
    wdt_enable(WDTO_15MS); // Enable the watchdog timer with a 15ms timeout
    while (1); // Wait for the watchdog timer to reset the Arduino
  }
}

void adjustValue(int direction) {
  switch (menuIndex) {
    case 0: // Inject Time
      if (direction > 0 && injectTime < 65535) {
        injectTime += direction;
      } else if (direction < 0 && injectTime > 0) {
        injectTime += direction;
      }
      break;
    case 1: // Vise Hold
      if (direction > 0 && viseHoldTime < 65535) {
        viseHoldTime += direction;
      } else if (direction < 0 && viseHoldTime > 0) {
        viseHoldTime += direction;
      }
      break;
    case 2: // Shot Size
      if (direction > 0 && shotSize < 15.0) {
        shotSize += 0.1 * direction; // Adjust by 0.1 seconds
      } else if (direction < 0 && shotSize > 0) {
        shotSize += 0.1 * direction; // Adjust by -0.1 seconds
      }
      break;
    case 3: // # of Parts
      if (direction > 0 && numOfParts < 65535) {
        numOfParts += direction;
      } else if (direction < 0 && numOfParts > 0) {
        numOfParts += direction;
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
      if (direction > 0 && warmingTemp < 500) {
        warmingTemp += direction * 5; // Adjust by 5 degrees
      } else if (direction < 0 && warmingTemp > 0) {
        warmingTemp += direction * 5; // Adjust by -5 degrees
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
      int dtValue = digitalRead(dtPin);

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
  // Calculate the base row for menu items based on menuIndex
  int baseRow = max(0, menuIndex - 7); // Adjusted to show 8 items

  // Print menu items with adjusted row
  for (int i = 0; i < 4; i++) {
    lcd.setCursor(0, i);
      if (i == menuIndex - baseRow) {
        lcd.print(">");
      }

      switch (baseRow + i) {
        case 0:
          lcd.print("Inject Time: ");
          lcd.print(injectTime);
          lcd.print("s");
          lcd.print("  ");
          break;

        case 1:
          lcd.print("Vise Hold: ");
          lcd.print(viseHoldTime);
          lcd.print("s");
          lcd.print("  ");
          break;

        case 2:
          lcd.print("Shot Size: ");
          lcd.print(shotSize, 1); // Display shotSize with one decimal place
          lcd.print("s");
          lcd.print("  ");
          break;

        case 3:
          lcd.print("# of Parts: ");
          lcd.print(numOfParts);
          lcd.print("  ");
          break;

        case 4:
          lcd.print("Eject Pull: ");
          lcd.print(EjectNegative);
          lcd.print("*");
          lcd.print("  ");
          break;

        case 5:
          lcd.print("Eject Push: ");
          lcd.print(EjectPositive);
          lcd.print("*");
          lcd.print("  ");
          break;

        case 6:
          lcd.print("Warming Temp: ");
          lcd.print(warmingTemp);
          lcd.print("*C");
          lcd.print("  ");
          break;

        case 7:
          lcd.clear();
          lcd.print("START");
          break;

        default:
          // Optional: handle unexpected values
          lcd.print("Unknown row");
          break;
    }
  }
} 