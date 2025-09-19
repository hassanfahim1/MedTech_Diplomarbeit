/*
 * Open Source Spritzenpumpe - 10ml Version (Thermik-Optimierung)
 * - Nur für 10ml Spritzen optimiert
 * - Ohne Encoder - nur 3 Buttons
 * - motorEnable() / motorRelease() für weniger Wärme im Stillstand
 * - Idle-Timer: Nach 2 s Inaktivität werden die Phasen freigegeben
 *
 * Hardware:
 * Arduino Leonardo + Motor Shield Rev3 (L298P) + I2C LCD + 3 Buttons
 * NEMA17 17HS08-1004S (3V/1A) – Achtung: L298P hat keine Stromregelung
 */

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Stepper.h>

// ----------------- Motor Shield Rev3 Pins -----------------
#define pwmA   3
#define pwmB  11
#define brakeA 9
#define brakeB 8
#define dirA  12
#define dirB  13

// ----------------- Button Pins -----------------
#define BTN_START_STOP 2    // Stop/Start
#define BTN_FILL       7    // Füllen
#define BTN_EMPTY      5    // Entleeren

// ----------------- Motor Setup -----------------
const int stepsPerRevolution = 200;
Stepper myStepper = Stepper(stepsPerRevolution, dirA, dirB);

// ----------------- LCD -----------------
LiquidCrystal_I2C lcd(0x27, 16, 2);

// ----------------- Umlaute -----------------
byte umlaut_ue[8] = {0x0A,0x00,0x11,0x11,0x11,0x13,0x0D,0x00}; // ü
byte umlaut_ae[8] = {0x0A,0x00,0x0E,0x01,0x0F,0x11,0x0F,0x00}; // ä
byte umlaut_oe[8] = {0x0A,0x00,0x0E,0x11,0x11,0x11,0x0E,0x00}; // ö
byte umlaut_UE[8] = {0x0A,0x00,0x11,0x11,0x11,0x11,0x0E,0x00}; // Ü
byte umlaut_AE[8] = {0x0A,0x00,0x0E,0x11,0x1F,0x11,0x11,0x00}; // Ä
byte umlaut_OE[8] = {0x0A,0x00,0x0E,0x11,0x11,0x11,0x0E,0x00}; // Ö

#define CHAR_UE_KLEIN 0
#define CHAR_AE_KLEIN 1
#define CHAR_OE_KLEIN 2
#define CHAR_UE_GROSS 3
#define CHAR_AE_GROSS 4
#define CHAR_OE_GROSS 5

// ----------------- 10ml Spritze Konfiguration -----------------
const int stepSize = 5900;           // Schritte pro Bewegung
const float stepsPerML = 590.0;      // Schritte pro ml
const int minSteps = -5900;          // Minimale Position (voll)
const float targetVolume = 10.0;     // Zielvolumen in ml

// ----------------- Zustände -----------------
bool motorRunning = false;
bool motorStopped = false;
bool lastStopStartState = HIGH;
bool lastFillState = HIGH;
bool lastEmptyState = HIGH;

bool currentStopStartState = HIGH;
bool currentFillState = HIGH;
bool currentEmptyState = HIGH;

int motorSpeed = 120; // rpm

// ----------------- Debounce -----------------
unsigned long lastDebounceTime = 0;
unsigned long debounceDelay = 50;

int totalSteps = 0;

// ----------------- Zeit-Tracking -----------------
unsigned long operationStartTime = 0;
unsigned long operationDuration  = 0;
bool operationInProgress = false;

// ----------------- Idle-Handling / Thermik -----------------
static unsigned long lastMoveMs = 0;
const unsigned long IDLE_RELEASE_MS = 2000; // nach 2s Phasen freigeben

// Optional: reduzierter Haltestrom statt komplett freigeben
// 0..255 Duty; 0 = freigeben (empfohlen), z.B. 60..80 = ~25-30% Duty
#define HALTESTROM_DUTY 0   // auf >0 setzen, wenn leichtes Haltemoment nötig

// ----------------- Hilfsfunktionen -----------------
void noteActivity() { lastMoveMs = millis(); }

// Motor freigeben: keine Dauerbestromung -> deutlich weniger Wärme
void motorRelease() {
  if (HALTESTROM_DUTY == 0) {
    // voll freigeben
    digitalWrite(pwmA, LOW);
    digitalWrite(pwmB, LOW);
    digitalWrite(brakeA, HIGH); // dynamische Bremse
    digitalWrite(brakeB, HIGH);
  } else {
    // reduzierter "Haltestrom" per PWM (Notlösung)
    digitalWrite(brakeA, LOW);
    digitalWrite(brakeB, LOW);
    analogWrite(pwmA, HALTESTROM_DUTY);
    analogWrite(pwmB, HALTESTROM_DUTY);
  }
}

// Motor aktivieren vor Bewegung
void motorEnable() {
  digitalWrite(brakeA, LOW);
  digitalWrite(brakeB, LOW);
  // Volle Versorgung auf die H-Brücken
  if (HALTESTROM_DUTY == 0) {
    digitalWrite(pwmA, HIGH);
    digitalWrite(pwmB, HIGH);
  } else {
    analogWrite(pwmA, 255);
    analogWrite(pwmB, 255);
  }
}

// ----------------- Setup -----------------
void setup() {
  pinMode(pwmA, OUTPUT);
  pinMode(pwmB, OUTPUT);
  pinMode(brakeA, OUTPUT);
  pinMode(brakeB, OUTPUT);

  // Startzustand: Motor freigeben
  motorRelease();

  pinMode(BTN_START_STOP, INPUT_PULLUP);
  pinMode(BTN_FILL,       INPUT_PULLUP);
  pinMode(BTN_EMPTY,      INPUT_PULLUP);

  myStepper.setSpeed(motorSpeed);

  Wire.begin();
  lcd.init();
  lcd.backlight();

  lcd.createChar(CHAR_UE_KLEIN, umlaut_ue);
  lcd.createChar(CHAR_AE_KLEIN, umlaut_ae);
  lcd.createChar(CHAR_OE_KLEIN, umlaut_oe);
  lcd.createChar(CHAR_UE_GROSS, umlaut_UE);
  lcd.createChar(CHAR_AE_GROSS, umlaut_AE);
  lcd.createChar(CHAR_OE_GROSS, umlaut_OE);

  lcd.setCursor(0, 0);
  lcd.print("10ml Spritzenpumpe");
  lcd.setCursor(0, 1);
  lcd.print("Ready...");
  delay(2000);

  updatePumpDisplay();
  noteActivity();
}

// ----------------- Loop -----------------
void loop() {
  handlePumpOperation();

  // Idle-Handling: wenn nicht in Bewegung und nicht in Operation -> freigeben
  if (!motorRunning && !operationInProgress) {
    if (millis() - lastMoveMs > IDLE_RELEASE_MS) {
      motorRelease();
    }
  }

  delay(10);
}

// ----------------- Pump Operation -----------------
void handlePumpOperation() {
  currentStopStartState = digitalRead(BTN_START_STOP);
  currentFillState      = digitalRead(BTN_FILL);
  currentEmptyState     = digitalRead(BTN_EMPTY);

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
        motorRelease(); // sofort freigeben
      } else {
        motorRunning = true;
        motorStopped = false;
        operationStartTime = millis();
        operationInProgress = true;
        motorEnable(); // bereit für Bewegung
      }
    }
  }
  lastStopStartState = currentStopStartState;

  if (currentFillState == LOW && lastFillState == HIGH) {
    if (totalSteps <= minSteps) {
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("MAX erreicht");
      lcd.setCursor(0, 1);
      lcd.print("10ml - voll!");
      delay(1200);
      updatePumpDisplay();
    } else {
      performFillOperation();
    }
  }
  lastFillState = currentFillState;

  if (currentEmptyState == LOW && lastEmptyState == HIGH) {
    if (totalSteps >= 0) {
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("NULLSTELLE");
      lcd.setCursor(0, 1);
      lcd.print("0.00ml - leer!");
      delay(1200);
      updatePumpDisplay();
    } else {
      performEmptyOperation();
    }
  }
  lastEmptyState = currentEmptyState;

  if (motorRunning) {
    motorEnable();
    myStepper.step(1);
    noteActivity();
    static unsigned long lastDisplayUpdate = 0;
    if (millis() - lastDisplayUpdate > 500) {
      updatePumpDisplay();
      lastDisplayUpdate = millis();
    }
  }
}

void performFillOperation() {
  motorRunning = false;
  motorStopped = false;
  operationInProgress = true;
  operationStartTime = millis();

  motorEnable();

  for (int i = 0; i < stepSize; i++) {
    // Grenze prüfen
    if (totalSteps - i <= minSteps) {
      motorStopped = true;
      operationDuration = (millis() - operationStartTime) / 1000;
      operationInProgress = false;
      totalSteps -= i;

      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("MAX erreicht");
      lcd.setCursor(0, 1);
      lcd.print("10ml - voll!");
      delay(1200);
      break;
    }

    // Not-Halt
    if (digitalRead(BTN_START_STOP) == LOW) {
      motorStopped = true;
      operationDuration = (millis() - operationStartTime) / 1000;
      operationInProgress = false;
      totalSteps -= i;
      delay(150);
      break;
    }

    // Schritt (Füllen = -1)
    myStepper.step(-1);
    noteActivity();

    if (i % 100 == 0) {
      float currentVol = abs((float)(totalSteps - i) / stepsPerML);
      unsigned long currentTime = (millis() - operationStartTime) / 1000;

      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("F");
      lcd.write(CHAR_UE_GROSS);
      lcd.print("llen:");
      lcd.print(currentVol, 2);
      lcd.print("ml");

      lcd.setCursor(0, 1);
      lcd.print(currentTime);
      lcd.print("s ");

      int currentHashes = (int)((currentVol / targetVolume) * 13.0);
      if (currentHashes > 13) currentHashes = 13;
      for (int h = 0; h < currentHashes; h++) lcd.print("#");
    }
  }

  if (!motorStopped) {
    totalSteps -= stepSize;
    operationDuration = (millis() - operationStartTime) / 1000;
    operationInProgress = false;
  }

  updatePumpDisplay();
  motorRelease();   // nach der Operation freigeben
  noteActivity();   // Idle-Timer neu setzen (jetzt frei)
  delay(80);
}

void performEmptyOperation() {
  motorRunning = false;
  motorStopped = false;
  operationInProgress = true;
  operationStartTime = millis();

  motorEnable();

  for (int i = 0; i < stepSize; i++) {
    // Grenze prüfen
    if (totalSteps + i >= 0) {
      motorStopped = true;
      operationDuration = (millis() - operationStartTime) / 1000;
      operationInProgress = false;
      totalSteps += i;

      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("NULLSTELLE");
      lcd.setCursor(0, 1);
      lcd.print("0.00ml - leer!");
      delay(1200);
      break;
    }

    // Not-Halt
    if (digitalRead(BTN_START_STOP) == LOW) {
      motorStopped = true;
      operationDuration = (millis() - operationStartTime) / 1000;
      operationInProgress = false;
      totalSteps += i;
      delay(150);
      break;
    }

    // Schritt (Leeren = +1)
    myStepper.step(1);
    noteActivity();

    if (i % 100 == 0) {
      float currentVol = abs((float)(totalSteps + i) / stepsPerML);
      unsigned long currentTime = (millis() - operationStartTime) / 1000;

      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Leeren:");
      lcd.print(currentVol, 2);
      lcd.print("ml");

      lcd.setCursor(0, 1);
      lcd.print(currentTime);
      lcd.print("s ");

      int currentHashes = (int)((currentVol / targetVolume) * 13.0);
      if (currentHashes > 13) currentHashes = 13;
      if (currentHashes < 0)  currentHashes = 0;
      for (int h = 0; h < currentHashes; h++) lcd.print("#");
    }
  }

  if (!motorStopped) {
    totalSteps += stepSize;
    operationDuration = (millis() - operationStartTime) / 1000;
    operationInProgress = false;
  }

  updatePumpDisplay();
  motorRelease();   // nach der Operation freigeben
  noteActivity();   // Idle-Timer neu setzen (jetzt frei)
  delay(80);
}

// ----------------- Anzeige -----------------
void updatePumpDisplay() {
  lcd.clear();
  float currentVol = abs((float)totalSteps / stepsPerML);

  if (motorStopped) {
    lcd.setCursor(0, 0);
    lcd.print("V in der Spritze:");
    lcd.setCursor(0, 1);
    lcd.print(currentVol, 2);
    lcd.print("ml (10ml max)");
    return;
  }

  lcd.setCursor(0, 0);
  lcd.print("10ml Vol:");
  lcd.print(currentVol, 2);
  lcd.print("ml");

  lcd.setCursor(0, 1);
  if (motorRunning) {
    lcd.print("LAUFEND");
  } else {
    lcd.print("BEREIT");
  }
}
