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

// ----------------- Hier sind alle Motor Shield Rev3 Pins (Anschlösse) -----------------
#define pwmA   3
#define pwmB  11
#define brakeA 9
#define brakeB 8
#define dirA  12
#define dirB  13

// ----------------- Die drei Taster wo ich angeschlossen habe -----------------
#define BTN_START_STOP 2    // Stop/Start Button
#define BTN_FILL       7    // Füllen Button
#define BTN_EMPTY      5    // Entleeren Button

// ----------------- Motor Einstellung -----------------
const int stepsPerRevolution = 200;
Stepper myStepper = Stepper(stepsPerRevolution, dirA, dirB);

// ----------------- LCD Display -----------------
LiquidCrystal_I2C lcd(0x27, 16, 2);

// ----------------- Deutsche Umlaute selbst gemacht weil LCD sie nicht kann -----------------
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

// ----------------- Einstellungen für 10ml Spritze (getestet und funktioniert gut) -----------------
const int stepSize = 5900;           // soviele schritte braucht für ganze spritze
const float stepsPerML = 590.0;      // schritte für einen ml
const int minSteps = -5900;          // minimum position wenn spritze voll ist
const float targetVolume = 10.0;     // maximum volumen

// ----------------- verschiedene Zustände vom system -----------------
bool motorRunning = false;
bool motorStopped = false;
bool lastStopStartState = HIGH;
bool lastFillState = HIGH;
bool lastEmptyState = HIGH;

bool currentStopStartState = HIGH;
bool currentFillState = HIGH;
bool currentEmptyState = HIGH;

int motorSpeed = 100; // geschwindigkeit in rpm, nicht zu schnell sonst verliert schritte

// ----------------- Button-Entprellung für stabile Funktion -----------------
// Mechanische Buttons "prellen" beim Drücken mehrmals schnell
// Ohne diese Wartezeit würde ein Druck als mehrere Drücke erkannt werden
// Das führt zu ungewollten mehrfach-Ausführungen
unsigned long lastDebounceTime = 0;
unsigned long debounceDelay = 50;

int totalSteps = 0;

// ----------------- Zeit messen für operationen -----------------
unsigned long operationStartTime = 0;
unsigned long operationDuration  = 0;
bool operationInProgress = false;

// ----------------- Motor wird heiss fix -----------------
static unsigned long lastMoveMs = 0;
const unsigned long IDLE_RELEASE_MS = 2000; // nach 2 sekunden motor freigeben

// Optional: bisschen strom lassen statt ganz freigeben
// 0..255 wert; 0 = komplett freigeben, ca 60-80 = bisschen halten
#define HALTESTROM_DUTY 0   // auf >0 setzen wenn motor bisschen halten soll

// ----------------- Hilfs funktionen -----------------
void noteActivity() { lastMoveMs = millis(); }

// Motor freigeben damit er nicht überhitzt
// Wenn Motor steht soll kein Strom fließen, sonst wird er zu heiß
void motorRelease() {
  if (HALTESTROM_DUTY == 0) {
    // komplett freigeben
    digitalWrite(pwmA, LOW);
    digitalWrite(pwmB, LOW);
    digitalWrite(brakeA, HIGH); // bremse an
    digitalWrite(brakeB, HIGH);
  } else {
    // bisschen strom lassen falls nötig
    digitalWrite(brakeA, LOW);
    digitalWrite(brakeB, LOW);
    analogWrite(pwmA, HALTESTROM_DUTY);
    analogWrite(pwmB, HALTESTROM_DUTY);
  }
}

// Motor anschalten bevor er sich bewegen soll
void motorEnable() {
  digitalWrite(brakeA, LOW);
  digitalWrite(brakeB, LOW);
  // volle power geben
  if (HALTESTROM_DUTY == 0) {
    digitalWrite(pwmA, HIGH);
    digitalWrite(pwmB, HIGH);
  } else {
    analogWrite(pwmA, 255);
    analogWrite(pwmB, 255);
  }
}

// ----------------- Setup läuft einmal beim start -----------------
void setup() {
  pinMode(pwmA, OUTPUT);
  pinMode(pwmB, OUTPUT);
  pinMode(brakeA, OUTPUT);
  pinMode(brakeB, OUTPUT);

  // am anfang motor freigeben
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
  lcd.print("V1.2 HFMTS");
  delay(2000);

  updatePumpDisplay();
  noteActivity();
}

// ----------------- Main loop läuft immer und immer -----------------
void loop() {
  handlePumpOperation();

  // wenn nichts passiert nach 2 sekunden motor freigeben
  if (!motorRunning && !operationInProgress) {
    if (millis() - lastMoveMs > IDLE_RELEASE_MS) {
      motorRelease();
    }
  }

  delay(10);
}

// ----------------- Hier wird alles mit den buttons gemacht -----------------
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
        motorRelease(); // Sofort freigeben damit Motor nicht überhitzt
      } else {
        motorRunning = true;
        motorStopped = false;
        operationStartTime = millis();
        operationInProgress = true;
        motorEnable(); // motor bereit machen
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
    // schauen ob schon voll ist
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

    // stop button gedrückt?
    if (digitalRead(BTN_START_STOP) == LOW) {
      motorStopped = true;
      operationDuration = (millis() - operationStartTime) / 1000;
      operationInProgress = false;
      totalSteps -= i;
      motorRelease();
      delay(150);
      break;
    }

    // einen schritt rückwärts (füllen)
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
  motorRelease();   // motor freigeben nach operation
  noteActivity();   // timer zurücksetzen
  delay(80);
}

void performEmptyOperation() {
  motorRunning = false;
  motorStopped = false;
  operationInProgress = true;
  operationStartTime = millis();

  motorEnable();

  for (int i = 0; i < stepSize; i++) {
    // schauen ob schon leer ist
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

    // stop button gedrückt?
    if (digitalRead(BTN_START_STOP) == LOW) {
      motorStopped = true;
      operationDuration = (millis() - operationStartTime) / 1000;
      operationInProgress = false;
      totalSteps += i;
      motorRelease();
      delay(150);
      break;
    }

    // einen schritt vorwärts (leeren)
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
  motorRelease();   // motor freigeben nach operation
  noteActivity();   // timer zurücksetzen
  delay(80);
}

// ----------------- Display anzeige updaten -----------------
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
