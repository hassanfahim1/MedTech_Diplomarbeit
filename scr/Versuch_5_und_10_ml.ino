/*
 * Open Source Spritzenpumpe - V1.1 Style mit Rotary Encoder Menü
 * Arduino Leonardo + Motor Shield Rev3 + I2C LCD + 3 Buttons + Rotary Encoder
 * Auswahl zwischen 5ml und 10ml Spritze per Rotary Encoder Menü
 */

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Stepper.h>

// Motor Shield Rev3 Pins (exakt wie v7)
#define pwmA 3
#define pwmB 11
#define brakeA 9
#define brakeB 8
#define dirA 12
#define dirB 13

// Button Pins - GETAUSCHT!
#define BTN_START_STOP 2    // Stop/Start
#define BTN_FILL 7          // Füllen - GETAUSCHT!
#define BTN_EMPTY 5         // Entleeren - GETAUSCHT!

// Rotary Encoder Pins - FREIE DIGITALE PINS
#define ENCODER_KEY 0       // Drück-Button - Pin 0 (RX)
#define ENCODER_CLK 1       // S1 (CLK) - Pin 1 (TX)
#define ENCODER_DT 6        // S2 (DT) - Pin 6

// Motor Setup (exakt wie v7)
const int stepsPerRevolution = 200;
Stepper myStepper = Stepper(stepsPerRevolution, dirA, dirB);

// Hardware
LiquidCrystal_I2C lcd(0x27, 16, 2);

// Menü System
enum MenuState {
  MENU_SELECT,    // Menü zur Spritzenauswahl
  PUMP_OPERATION  // Normaler Pumpenbetrieb
};

MenuState currentMenuState = MENU_SELECT;
int menuSelection = 0;  // 0 = 5ml, 1 = 10ml
bool menuNeedsUpdate = true;

// Variables (wie v7 style)
bool motorRunning = false;
bool motorStopped = false;
bool lastStopStartState = HIGH;
bool lastFillState = HIGH;
bool lastEmptyState = HIGH;

bool currentStopStartState = HIGH;
bool currentFillState = HIGH;
bool currentEmptyState = HIGH;

int motorSpeed = 120;        // RPM 

// Rotary Encoder Variables
bool lastEncoderKeyState = HIGH;
bool lastCLKState = HIGH;
unsigned long lastEncoderTime = 0;
const unsigned long encoderDebounce = 300;

// Spritzengröße abhängige Parameter
struct SyringeConfig {
  int stepSize;
  float stepsPerML;
  int minSteps;
  float targetVolume;
  String displayName;
};

SyringeConfig syringeConfigs[2] = {
  {4250, 850.0, -4250, 5.0, "5ml"},   // 5ml Spritze
  {5900, 590.0, -5900, 10.0, "10ml"}  // 10ml Spritze
};

// Aktuelle Konfiguration
SyringeConfig currentConfig;

// Debounce (wie v7)
unsigned long lastDebounceTime = 0;
unsigned long debounceDelay = 50;

int totalSteps = 0;

// Zeit-Tracking
unsigned long operationStartTime = 0;
unsigned long operationDuration = 0;
bool operationInProgress = false;

// Animation für Spritze
int currentFillHashes = 0;  // Aktuelle # in der Spritze

void setup() {
  // Motor Shield Setup
  pinMode(pwmA, OUTPUT);
  pinMode(pwmB, OUTPUT);
  pinMode(brakeA, OUTPUT);
  pinMode(brakeB, OUTPUT);

  digitalWrite(pwmA, HIGH);
  digitalWrite(pwmB, HIGH);
  digitalWrite(brakeA, LOW);
  digitalWrite(brakeB, LOW);

  // Button Setup
  pinMode(BTN_START_STOP, INPUT_PULLUP);
  pinMode(BTN_FILL, INPUT_PULLUP);
  pinMode(BTN_EMPTY, INPUT_PULLUP);
  
  // Rotary Encoder Setup
  pinMode(ENCODER_KEY, INPUT_PULLUP);
  pinMode(ENCODER_CLK, INPUT_PULLUP);
  pinMode(ENCODER_DT, INPUT_PULLUP);
  
  lastCLKState = digitalRead(ENCODER_CLK);

  // Motor Speed
  myStepper.setSpeed(motorSpeed);
  
  // I2C LCD Setup
  Wire.begin();
  lcd.init();
  lcd.backlight();
  
  // System Initialisierung anzeigen
  lcd.setCursor(0, 0);
  lcd.print("Spritzenpumpe V1.1");
  lcd.setCursor(0, 1);
  lcd.print("Initialisierung..");
  delay(2000);
  
  // Starte im Menü
  currentMenuState = MENU_SELECT;
  updateMenuDisplay();
}

void loop() {
  if (currentMenuState == MENU_SELECT) {
    // Menü-Modus
    handleMenuInput();
    if (menuNeedsUpdate) {
      updateMenuDisplay();
      menuNeedsUpdate = false;
    }
  } else {
    // Normaler Pumpenbetrieb
    handlePumpOperation();
  }
  
  delay(10);
}

void handleMenuInput() {
  // Rotary Encoder Drehung - zwischen 5ml und 10ml wählen
  bool currentCLKState = digitalRead(ENCODER_CLK);
  
  if (currentCLKState != lastCLKState && (millis() - lastEncoderTime) > 100) {
    if (digitalRead(ENCODER_DT) != currentCLKState) {
      // Uhrzeigersinn - 10ml
      menuSelection = 1;
    } else {
      // Gegen Uhrzeigersinn - 5ml
      menuSelection = 0;
    }
    menuNeedsUpdate = true;
    lastEncoderTime = millis();
  }
  lastCLKState = currentCLKState;
  
  // Encoder Button - Auswahl bestätigen
  bool currentEncoderKeyState = digitalRead(ENCODER_KEY);
  
  if (currentEncoderKeyState == LOW && lastEncoderKeyState == HIGH) {
    delay(50);
    
    // Auswahl bestätigen
    updateSyringeConfig();
    currentMenuState = PUMP_OPERATION;
    
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Ausgewählt:");
    lcd.setCursor(0, 1);
    lcd.print(currentConfig.displayName + " Spritze");
    delay(1500);
    
    updatePumpDisplay();
  }
  lastEncoderKeyState = currentEncoderKeyState;
}

void handlePumpOperation() {
  // Encoder Button - zurück zum Menü
  bool currentEncoderKeyState = digitalRead(ENCODER_KEY);
  
  if (currentEncoderKeyState == LOW && lastEncoderKeyState == HIGH) {
    // Zurück zum Menü (nur wenn Motor nicht läuft)
    if (!motorRunning && !operationInProgress) {
      currentMenuState = MENU_SELECT;
      menuNeedsUpdate = true;
      
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Zurück zum");
      lcd.setCursor(0, 1);
      lcd.print("Spritzenmenü...");
      delay(1000);
      
      updateMenuDisplay();
    }
    delay(200);
  }
  lastEncoderKeyState = currentEncoderKeyState;
  
  // Standard Pumpen-Operationen
  currentStopStartState = digitalRead(BTN_START_STOP);
  currentFillState = digitalRead(BTN_FILL);
  currentEmptyState = digitalRead(BTN_EMPTY);
  
  // START/STOP Button
  if (currentStopStartState != lastStopStartState) {
    lastDebounceTime = millis();
  }
  
  if ((millis() - lastDebounceTime) > debounceDelay) {
    if (currentStopStartState == LOW && lastStopStartState == HIGH) {
      if (motorRunning || operationInProgress) {
        motorRunning = false;
        motorStopped = true;
        
        if (operationInProgress) {
          operationDuration = (millis() - operationStartTime) / 1000;
          operationInProgress = false;
        }
        
        updatePumpDisplay();
      } else {
        motorRunning = true;
        motorStopped = false;
        operationStartTime = millis();
        operationInProgress = true;
      }
    }
  }
  lastStopStartState = currentStopStartState;
  
  // FILL Button
  if (currentFillState == LOW && lastFillState == HIGH) {
    if (totalSteps <= currentConfig.minSteps) {
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("MAXIMUM erreicht");
      lcd.setCursor(0, 1);
      lcd.print(currentConfig.displayName + " - voll!");
      delay(2000);
      updatePumpDisplay();
      return;
    }
    
    performFillOperation();
  }
  lastFillState = currentFillState;
  
  // EMPTY Button
  if (currentEmptyState == LOW && lastEmptyState == HIGH) {
    if (totalSteps >= 0) {
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("NULLSTELLE");
      lcd.setCursor(0, 1);
      lcd.print("0ml - leer!");
      delay(2000);
      updatePumpDisplay();
      return;
    }
    
    performEmptyOperation();
  }
  lastEmptyState = currentEmptyState;
  
  // Continuous rotation
  if (motorRunning) {
    myStepper.step(1);
    
    static unsigned long lastDisplayUpdate = 0;
    if (millis() - lastDisplayUpdate > 500) {
      updatePumpDisplay();
      lastDisplayUpdate = millis();
    }
  }
}

void updateMenuDisplay() {
  lcd.clear();
  
  if (menuSelection == 0) {
    // 5ml ausgewählt
    lcd.setCursor(0, 0);
    lcd.print("> 5ml Spritze");
    lcd.setCursor(0, 1);
    lcd.print("  Drehen für 10ml");
  } else {
    // 10ml ausgewählt
    lcd.setCursor(0, 0);
    lcd.print("> 10ml Spritze");
    lcd.setCursor(0, 1);
    lcd.print("  Drehen für 5ml");
  }
}

void updateSyringeConfig() {
  currentConfig = syringeConfigs[menuSelection];
  // Reset beim Wechsel für Sicherheit
  totalSteps = 0;
  currentFillHashes = 0;
}

void performFillOperation() {
  motorRunning = false;
  motorStopped = false;
  operationInProgress = true;
  operationStartTime = millis();
  
  for (int i = 0; i < currentConfig.stepSize; i++) {
    if (totalSteps - i <= currentConfig.minSteps) {
      motorStopped = true;
      operationDuration = (millis() - operationStartTime) / 1000;
      operationInProgress = false;
      totalSteps -= i;
      
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("MAXIMUM erreicht");
      lcd.setCursor(0, 1);
      lcd.print(currentConfig.displayName + " - voll!");
      delay(2000);
      break;
    }
    
    if (digitalRead(BTN_START_STOP) == LOW) {
      motorStopped = true;
      operationDuration = (millis() - operationStartTime) / 1000;
      operationInProgress = false;
      totalSteps -= i;
      delay(200);
      break;
    }
    
    myStepper.step(-1);
    
    if (i % 100 == 0) {
      float currentVol = abs((float)(totalSteps - i) / currentConfig.stepsPerML);
      unsigned long currentTime = (millis() - operationStartTime) / 1000;
      
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("FÜLL: ");
      lcd.print(currentVol, 1);
      lcd.print("ml/");
      lcd.print(currentTime);
      lcd.print("s");
      
      lcd.setCursor(0, 1);
      int hashCount = currentTime / 3;
      int totalHashes = currentFillHashes + hashCount;
      for (int h = 0; h < totalHashes && h < 16; h++) {
        lcd.print("#");
      }
    }
  }
  
  if (!motorStopped) {
    totalSteps -= currentConfig.stepSize;
    operationDuration = (millis() - operationStartTime) / 1000;
    operationInProgress = false;
    
    int newHashes = operationDuration / 3;
    currentFillHashes = currentFillHashes + newHashes;
  }
  updatePumpDisplay();
  delay(100);
}

void performEmptyOperation() {
  motorRunning = false;
  motorStopped = false;
  operationInProgress = true;
  operationStartTime = millis();
  
  for (int i = 0; i < currentConfig.stepSize; i++) {
    if (totalSteps + i >= 0) {
      motorStopped = true;
      operationDuration = (millis() - operationStartTime) / 1000;
      operationInProgress = false;
      totalSteps += i;
      
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("NULLSTELLE");
      lcd.setCursor(0, 1);
      lcd.print("0ml - leer!");
      delay(2000);
      break;
    }
    
    if (digitalRead(BTN_START_STOP) == LOW) {
      motorStopped = true;
      operationDuration = (millis() - operationStartTime) / 1000;
      operationInProgress = false;
      totalSteps += i;
      
      int lostHashes = operationDuration / 3;
      currentFillHashes = currentFillHashes - lostHashes;
      if (currentFillHashes < 0) currentFillHashes = 0;
      
      delay(200);
      break;
    }
    
    myStepper.step(1);
    
    if (i % 100 == 0) {
      float currentVol = abs((float)(totalSteps + i) / currentConfig.stepsPerML);
      unsigned long currentTime = (millis() - operationStartTime) / 1000;
      
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("LEER: ");
      lcd.print(currentVol, 1);
      lcd.print("ml/");
      lcd.print(currentTime);
      lcd.print("s");
      
      lcd.setCursor(0, 1);
      int lostHashes = currentTime / 3;
      int remainingHashes = currentFillHashes - lostHashes;
      if (remainingHashes < 0) remainingHashes = 0;
      
      for (int h = 0; h < remainingHashes && h < 16; h++) {
        lcd.print("#");
      }
    }
  }
  
  if (!motorStopped) {
    totalSteps += currentConfig.stepSize;
    operationDuration = (millis() - operationStartTime) / 1000;
    operationInProgress = false;
    
    int lostHashes = operationDuration / 3;
    currentFillHashes = currentFillHashes - lostHashes;
    if (currentFillHashes < 0) currentFillHashes = 0;
  }
  updatePumpDisplay();
  delay(100);
}

void updatePumpDisplay() {
  lcd.clear();
  
  float currentVol = abs((float)totalSteps / currentConfig.stepsPerML);
  
  if (motorStopped) {
    lcd.setCursor(0, 0);
    lcd.print("Volumen noch in");
    lcd.setCursor(0, 1);
    lcd.print("der Spritze:");
    lcd.print(currentVol, 1);
    lcd.print("ml");
    return;
  }
  
  lcd.setCursor(0, 0);
  lcd.print(currentConfig.displayName + " Vol:");
  lcd.print(currentVol, 1);
  lcd.print("ml");
  
  lcd.setCursor(0, 1);
  if (motorRunning) {
    lcd.print("KONTINUIERLICH");
  } else {
    lcd.print("BEREIT");
  }
}
