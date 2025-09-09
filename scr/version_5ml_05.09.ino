/*
 * Open Source Spritzenpumpe - V7 Style
 * Arduino Leonardo + Motor Shield Rev3 + I2C LCD + 3 Buttons
 * Basiert auf bewährtem v7 Code mit 3500 Steps für 5ml
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
#define BTN_FILL 7          // Füllen (Links = -3500) - GETAUSCHT!
#define BTN_EMPTY 5         // Entleeren (Rechts = +3500) - GETAUSCHT!

// Motor Setup (exakt wie v7)
const int stepsPerRevolution = 200;
Stepper myStepper = Stepper(stepsPerRevolution, dirA, dirB);

// Hardware
LiquidCrystal_I2C lcd(0x27, 16, 2);

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
int stepSize = 4250;         // Schritte pro Tastendruck - für 5ml Spritze

// Debounce (wie v7)
unsigned long lastDebounceTime = 0;
unsigned long debounceDelay = 50;

// Spritzenpumpe Parameter - 5ml Spritze
float targetVolume = 5.0;    // ml - 5ml Spritze
const float STEPS_PER_ML = 850.0;  // 3500 Steps ÷ 5ml = 700 Steps/ml
int totalSteps = 0;

// Sicherheits-Grenzen
const int MAX_STEPS = 0;        // Nullstelle - kann nicht weiter entleeren  
const int MIN_STEPS = -4250;    // Maximum gefüllt - kann nicht weiter füllen

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

  // Motor Speed
  myStepper.setSpeed(motorSpeed);
  
  // I2C LCD Setup
  Wire.begin();
  lcd.init();
  lcd.backlight();
  
  // System Initialisierung anzeigen
  lcd.setCursor(0, 0);
  lcd.print("System");
  lcd.setCursor(0, 1);
  lcd.print("Initialisieren..");
  delay(1000);
  
  lcd.setCursor(0, 1);
  lcd.print("Initialisieren...");
  delay(1000);
  
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Spritzenpumpe V7");
  lcd.setCursor(0, 1);
  lcd.print("5ml - Bereit!");
  delay(1500);
  
  updateDisplay();
}

void loop() {
  // Button States lesen (exakt wie v7)
  currentStopStartState = digitalRead(BTN_START_STOP);
  currentFillState = digitalRead(BTN_FILL);
  currentEmptyState = digitalRead(BTN_EMPTY);
  
  // START/STOP Button (wie v7 mit Entprellung)
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
        
        updateDisplay();
      } else {
        motorRunning = true;
        motorStopped = false;
        operationStartTime = millis();
        operationInProgress = true;
      }
    }
  }
  lastStopStartState = currentStopStartState;
  
  // FILL Button (Links = -3500, GETAUSCHT - jetzt Pin 7!)
  if (currentFillState == LOW && lastFillState == HIGH) {
    // SICHERHEITS-CHECK: Nicht über Maximum hinaus füllen
    if (totalSteps <= MIN_STEPS) {
      // Zeige Warnung und blockiere
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("MAXIMUM erreicht");
      lcd.setCursor(0, 1);
      lcd.print("5ml - voll!");
      delay(2000);
      updateDisplay();
      return; // Blockiere Füllen
    }
    
    motorRunning = false;
    motorStopped = false;
    operationInProgress = true;
    operationStartTime = millis();
    
    for (int i = 0; i < stepSize; i++) {
      // SICHERHEITS-CHECK während Bewegung
      if (totalSteps - i <= MIN_STEPS) {
        // Stoppe bei Maximum
        motorStopped = true;
        operationDuration = (millis() - operationStartTime) / 1000;
        operationInProgress = false;
        totalSteps -= i;
        
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("MAXIMUM erreicht");
        lcd.setCursor(0, 1);
        lcd.print("5ml - voll!");
        delay(2000);
        break;
      }
      
      // Check Stop button während Bewegung
      if (digitalRead(BTN_START_STOP) == LOW) {
        motorStopped = true;
        operationDuration = (millis() - operationStartTime) / 1000;
        operationInProgress = false;
        totalSteps -= i;
        delay(200);
        break;
      }
      
      myStepper.step(-1);
      
      // Live-Update alle 100 Schritte
      if (i % 100 == 0) {
        float currentVol = abs((float)(totalSteps - i) / STEPS_PER_ML);
        unsigned long currentTime = (millis() - operationStartTime) / 1000;
        
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("FÜLL: ");
        lcd.print(currentVol, 1);
        lcd.print("ml/");
        lcd.print(currentTime);
        lcd.print("s");
        
        // Animation: # für alle 3 Sekunden (weniger #)
        lcd.setCursor(0, 1);
        int hashCount = currentTime / 3;  // Ein # alle 3 Sekunden
        int totalHashes = currentFillHashes + hashCount;
        for (int h = 0; h < totalHashes && h < 16; h++) {
          lcd.print("#");
        }
      }
    }
    
    if (!motorStopped) {
      totalSteps -= stepSize;
      operationDuration = (millis() - operationStartTime) / 1000;
      operationInProgress = false;
      
      // Speichere finalen # Stand (ALTER Stand + neue #)
      int newHashes = operationDuration / 3;  // Ein # alle 3 Sekunden
      currentFillHashes = currentFillHashes + newHashes;
    }
    updateDisplay();
    delay(100);
  }
  lastFillState = currentFillState;
  
  // EMPTY Button (Rechts = +3500, GETAUSCHT - jetzt Pin 5!)
  if (currentEmptyState == LOW && lastEmptyState == HIGH) {
    // SICHERHEITS-CHECK: Nicht unter Nullstelle hinaus entleeren
    if (totalSteps >= MAX_STEPS) {
      // Zeige Warnung und blockiere
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("NULLSTELLE");
      lcd.setCursor(0, 1);
      lcd.print("0ml - leer!");
      delay(2000);
      updateDisplay();
      return; // Blockiere Entleeren
    }
    
    motorRunning = false;
    motorStopped = false;
    operationInProgress = true;
    operationStartTime = millis();
    
    for (int i = 0; i < stepSize; i++) {
      // SICHERHEITS-CHECK während Bewegung
      if (totalSteps + i >= MAX_STEPS) {
        // Stoppe bei Nullstelle
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
      
      // Check Stop button während Bewegung
      if (digitalRead(BTN_START_STOP) == LOW) {
        motorStopped = true;
        operationDuration = (millis() - operationStartTime) / 1000;
        operationInProgress = false;
        totalSteps += i;
        
        // Update # Stand bei Stop
        int lostHashes = operationDuration / 3;  // Ein # alle 3 Sekunden verloren
        currentFillHashes = currentFillHashes - lostHashes;
        if (currentFillHashes < 0) currentFillHashes = 0;
        
        delay(200);
        break;
      }
      
      myStepper.step(1);
      
      // Live-Update alle 100 Schritte
      if (i % 100 == 0) {
        float currentVol = abs((float)(totalSteps + i) / STEPS_PER_ML);
        unsigned long currentTime = (millis() - operationStartTime) / 1000;
        
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("LEER: ");
        lcd.print(currentVol, 1);
        lcd.print("ml/");
        lcd.print(currentTime);
        lcd.print("s");
        
        // Animation: Zeige aktuellen Füllstand MINUS verstrichene #
        lcd.setCursor(0, 1);
        int lostHashes = currentTime / 3;  // Ein # alle 3 Sekunden verloren
        int remainingHashes = currentFillHashes - lostHashes;
        if (remainingHashes < 0) remainingHashes = 0;
        
        for (int h = 0; h < remainingHashes && h < 16; h++) {
          lcd.print("#");
        }
      }
    }
    
    if (!motorStopped) {
      totalSteps += stepSize;
      operationDuration = (millis() - operationStartTime) / 1000;
      operationInProgress = false;
      
      // Update # Stand nach komplettem Entleeren
      int lostHashes = operationDuration / 3;  // Ein # alle 3 Sekunden verloren
      currentFillHashes = currentFillHashes - lostHashes;
      if (currentFillHashes < 0) currentFillHashes = 0;
    }
    updateDisplay();
    delay(100);
  }
  lastEmptyState = currentEmptyState;
  
  // Continuous rotation wenn motor läuft
  if (motorRunning) {
    myStepper.step(1);
    
    static unsigned long lastDisplayUpdate = 0;
    if (millis() - lastDisplayUpdate > 500) {
      updateDisplay();
      lastDisplayUpdate = millis();
    }
  }
  
  delay(10);
}

void updateDisplay() {
  lcd.clear();
  
  float currentVol = abs((float)totalSteps / STEPS_PER_ML);
  
  // Bei Stop: NUR Volumen anzeigen - KEINE ZEIT!
  if (motorStopped) {
    lcd.setCursor(0, 0);
    lcd.print("Volumen noch in");
    lcd.setCursor(0, 1);
    lcd.print("der Spritze:");
    lcd.print(currentVol, 1);
    lcd.print("ml");
    return;
  }
  
  // Normale Anzeige (BEREIT oder KONTINUIERLICH)
  lcd.setCursor(0, 0);
  lcd.print("Vol: ");
  lcd.print(currentVol, 1);
  lcd.print(" ml");
  
  lcd.setCursor(0, 1);
  if (motorRunning) {
    lcd.print("KONTINUIERLICH");
  } else {
    lcd.print("BEREIT");
  }
}

/* 
 * VERKABELUNG (GETAUSCHT!):
 * Stop/Start: Pin 2 und GND
 * FILL:       Pin 7 und GND (GETAUSCHT - war Pin 5)
 * EMPTY:      Pin 5 und GND (GETAUSCHT - war Pin 7)
 * I2C LCD:    SDA, SCL
 * 
 * FUNKTIONEN (5ml Spritze):
 * - FILL (Pin 7):  Dreht -3500 Schritte (links) für 5ml
 * - EMPTY (Pin 5): Dreht +3500 Schritte (rechts) für 5ml
 * - START/STOP: Kontinuierliche Drehung oder Stop
 * - Stop funktioniert IMMER, auch während 3500-Steps
 * - Stop zeigt nur Volumen ohne Zeit
 * - 3500 Steps = 5ml (700 Steps/ml)
 */
