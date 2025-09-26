/*
 * Open Source Spritzenpumpe - V1.2.1 (Stop-Button Fix)
 * - Verbessertes Debouncing für Stop-Button
 * - Saubere Interrupt-Behandlung
 * - Robustere Zustandsübergänge
 *
 * Hardware:
 * Arduino Leonardo + Motor Shield Rev3 (L298P) + I2C LCD + 3 Buttons + Rotary Encoder
 * NEMA17 17HS08-1004S (3V/1A) – Achtung: L298P hat keine Stromregelung
 */

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Stepper.h>

// ----------------- Hier sind alle Motor Shield Rev3 Pins (Anschlüsse) -----------------
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

// ----------------- Rotary Encoder Pins für Menü Auswahl -----------------
#define ENCODER_KEY 0       // Drück-Button - Pin 0 (RX)
#define ENCODER_CLK 1       // S1 (CLK) - Pin 1 (TX)
#define ENCODER_DT  6       // S2 (DT) - Pin 6

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

// ----------------- Menü System für Spritzenauswahl -----------------
enum MenuState {
  MENU_SELECT,
  PUMP_OPERATION
};

MenuState currentMenuState = MENU_SELECT;
int menuSelection = 0;
bool menuNeedsUpdate = true;

// ----------------- verschiedene Zustände vom System -----------------
bool motorRunning = false;
bool motorStopped = false;

// ----------------- Button-Entprellung für stabile Funktion -----------------
// Mechanische Buttons "prellen" beim Drücken mehrmals schnell
// Ohne diese Wartezeit würde ein Druck als mehrere Drücke erkannt werden
// Das führt zu ungewollten mehrfach-Ausführungen
struct ButtonState {
  bool current;
  bool previous;
  bool pressed;
  unsigned long lastChangeTime;
  unsigned long debounceTime;
};

ButtonState stopStartBtn;
ButtonState fillBtn;
ButtonState emptyBtn;

int motorSpeed = 120; // Geschwindigkeit in rpm, nicht zu schnell sonst verliert Schritte

// ----------------- Encoder für Menü Navigation -----------------
bool lastEncoderKeyState = HIGH;
bool lastCLKState = HIGH;
unsigned long lastEncoderTime = 0;
const unsigned long encoderDebounce = 300;

// ----------------- Spritzeneinstellungen für beide Größen -----------------
struct SyringeConfig {
  int stepSize;
  float stepsPerML;
  int minSteps;
  float targetVolume;
  String displayName;
};

SyringeConfig syringeConfigs[2] = {
  {4250, 850.0, -4250, 5.0,  "5ml"},
  {5900, 590.0, -5900, 10.0, "10ml"}
};

SyringeConfig currentConfig;

int totalSteps = 0;

// ----------------- Zeit messen für Operationen -----------------
unsigned long operationStartTime = 0;
unsigned long operationDuration  = 0;
bool operationInProgress = false;

// ----------------- Motor wird heiß Fix -----------------
static unsigned long lastMoveMs = 0;
const unsigned long IDLE_RELEASE_MS = 2000; // nach 2 Sekunden Motor freigeben

// Optional: bisschen Strom lassen statt ganz freigeben
// 0..255 Wert; 0 = komplett freigeben, ca 60-80 = bisschen halten
#define HALTESTROM_DUTY 0   // auf >0 setzen wenn Motor bisschen halten soll

// ----------------- Hilfs Funktionen -----------------
void noteActivity() { lastMoveMs = millis(); }

// Verbesserte Button-Abfrage mit robustem Debouncing
bool updateButtonState(ButtonState &btn, int pin) {
  bool currentReading = digitalRead(pin);
  unsigned long currentTime = millis();
  
  // Zustand geändert? -> Timer neu starten
  if (currentReading != btn.current) {
    btn.lastChangeTime = currentTime;
    btn.current = currentReading;
    return false; // Noch im Debounce-Zeitraum
  }
  
  // Genug Zeit vergangen seit letzter Änderung?
  if (currentTime - btn.lastChangeTime >= btn.debounceTime) {
    // Stabiler neuer Zustand -> Flanke erkennen
    if (btn.previous == HIGH && btn.current == LOW) {
      btn.pressed = true;
    } else {
      btn.pressed = false;
    }
    btn.previous = btn.current;
    return btn.pressed;
  }
  
  return false;
}

// Motor freigeben damit er nicht überhitzt
// Wenn Motor steht soll kein Strom fließen, sonst wird er zu heiß
void motorRelease() {
  if (HALTESTROM_DUTY == 0) {
    // komplett freigeben
    digitalWrite(pwmA, LOW);
    digitalWrite(pwmB, LOW);
    digitalWrite(brakeA, HIGH); // Bremse an
    digitalWrite(brakeB, HIGH);
  } else {
    // bisschen Strom lassen falls nötig
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
  // volle Power geben
  if (HALTESTROM_DUTY == 0) {
    digitalWrite(pwmA, HIGH);
    digitalWrite(pwmB, HIGH);
  } else {
    analogWrite(pwmA, 255);
    analogWrite(pwmB, 255);
  }
}

// Sicherer Motor-Stop für Notfälle
void emergencyMotorStop() {
  motorRunning = false;
  operationInProgress = false;
  motorStopped = true;
  motorRelease();
  
  if (operationStartTime > 0) {
    operationDuration = (millis() - operationStartTime) / 1000;
  }
  
  // kurze Pause für Hardware-Stabilisierung
  delay(10);
}

// ----------------- Setup läuft einmal beim Start -----------------
void setup() {
  pinMode(pwmA, OUTPUT);
  pinMode(pwmB, OUTPUT);
  pinMode(brakeA, OUTPUT);
  pinMode(brakeB, OUTPUT);

  // am Anfang Motor freigeben
  motorRelease();

  pinMode(BTN_START_STOP, INPUT_PULLUP);
  pinMode(BTN_FILL,       INPUT_PULLUP);
  pinMode(BTN_EMPTY,      INPUT_PULLUP);

  pinMode(ENCODER_KEY, INPUT_PULLUP);
  pinMode(ENCODER_CLK, INPUT_PULLUP);
  pinMode(ENCODER_DT,  INPUT_PULLUP);

  lastCLKState = digitalRead(ENCODER_CLK);

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
  lcd.print("OSPumpe V1.2.1");
  lcd.setCursor(0, 1);
  lcd.print("HFMTS");
  delay(1200);

  currentMenuState = MENU_SELECT;
  updateMenuDisplay();
  noteActivity();
  
  // Button-Zustände initialisieren
  stopStartBtn.current = digitalRead(BTN_START_STOP);
  stopStartBtn.previous = stopStartBtn.current;
  stopStartBtn.pressed = false;
  stopStartBtn.lastChangeTime = 0;
  stopStartBtn.debounceTime = 50;
  
  fillBtn.current = digitalRead(BTN_FILL);
  fillBtn.previous = fillBtn.current;
  fillBtn.pressed = false;
  fillBtn.lastChangeTime = 0;
  fillBtn.debounceTime = 50;
  
  emptyBtn.current = digitalRead(BTN_EMPTY);
  emptyBtn.previous = emptyBtn.current;
  emptyBtn.pressed = false;
  emptyBtn.lastChangeTime = 0;
  emptyBtn.debounceTime = 50;
}

// ----------------- Main Loop läuft immer und immer -----------------
void loop() {
  if (currentMenuState == MENU_SELECT) {
    handleMenuInput();
    if (menuNeedsUpdate) {
      updateMenuDisplay();
      menuNeedsUpdate = false;
    }
  } else {
    handlePumpOperation();
  }

  // wenn nichts passiert nach 2 Sekunden Motor freigeben
  if (!motorRunning && !operationInProgress) {
    if (millis() - lastMoveMs > IDLE_RELEASE_MS) {
      motorRelease();
    }
  }

  delay(5); // schnellere Reaktionszeit
}

// ----------------- Menü Navigation mit Encoder -----------------
void handleMenuInput() {
  bool currentCLKState = digitalRead(ENCODER_CLK);
  if (currentCLKState != lastCLKState && (millis() - lastEncoderTime) > 100) {
    if (digitalRead(ENCODER_DT) != currentCLKState) {
      menuSelection = 1;
    } else {
      menuSelection = 0;
    }
    menuNeedsUpdate = true;
    lastEncoderTime = millis();
  }
  lastCLKState = currentCLKState;

  bool currentEncoderKeyState = digitalRead(ENCODER_KEY);
  if (currentEncoderKeyState == LOW && lastEncoderKeyState == HIGH) {
    delay(50);
    updateSyringeConfig();
    currentMenuState = PUMP_OPERATION;

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Ausgew");
    lcd.write(CHAR_AE_KLEIN);
    lcd.print("hlt:");
    lcd.setCursor(0, 1);
    lcd.print(currentConfig.displayName + " Spritze");
    delay(1200);

    updatePumpDisplay();
  }
  lastEncoderKeyState = currentEncoderKeyState;
}

void updateMenuDisplay() {
  lcd.clear();
  if (menuSelection == 0) {
    lcd.setCursor(0, 0);
    lcd.print("> 5ml Spritze");
    lcd.setCursor(0, 1);
    lcd.print("Drehen f");
    lcd.write(CHAR_UE_KLEIN);
    lcd.print("r 10ml");
  } else {
    lcd.setCursor(0, 0);
    lcd.print("> 10ml Spritze");
    lcd.setCursor(0, 1);
    lcd.print("Drehen f");
    lcd.write(CHAR_UE_KLEIN);
    lcd.print("r 5ml");
  }
}

void updateSyringeConfig() {
  currentConfig = syringeConfigs[menuSelection];
  totalSteps = 0;
}

// ----------------- Hier wird alles mit den Buttons gemacht -----------------
void handlePumpOperation() {
  bool currentEncoderKeyState = digitalRead(ENCODER_KEY);
  if (currentEncoderKeyState == LOW && lastEncoderKeyState == HIGH) {
    if (!motorRunning && !operationInProgress) {
      currentMenuState = MENU_SELECT;
      menuNeedsUpdate = true;

      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Zur");
      lcd.write(CHAR_UE_KLEIN);
      lcd.print("ck zu");
      lcd.setCursor(0, 1);
      lcd.print("Men");
      lcd.write(CHAR_UE_KLEIN);
      lcd.print("...");
      delay(800);

      updateMenuDisplay();
    }
    delay(200);
  }
  lastEncoderKeyState = currentEncoderKeyState;

  // Verbesserte Button-Behandlung
  bool stopPressed = updateButtonState(stopStartBtn, BTN_START_STOP);
  bool fillPressed = updateButtonState(fillBtn, BTN_FILL);
  bool emptyPressed = updateButtonState(emptyBtn, BTN_EMPTY);

  // Stop/Start Button (höchste Priorität)
  if (stopPressed) {
    if (motorRunning || operationInProgress) {
      // sofortiger Stop
      emergencyMotorStop();
      
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("GESTOPPT!");
      lcd.setCursor(0, 1);
      lcd.print("Dauer: ");
      lcd.print(operationDuration);
      lcd.print("s");
      delay(1500);
      
      updatePumpDisplay();
    } else {
      // Start
      motorRunning = true;
      motorStopped = false;
      operationStartTime = millis();
      operationInProgress = true;
      motorEnable();
      noteActivity();
      
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("GESTARTET!");
      delay(500);
    }
  }

  // Fill Button
  if (fillPressed && !motorRunning && !operationInProgress) {
    if (totalSteps <= currentConfig.minSteps) {
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("MAX erreicht");
      lcd.setCursor(0, 1);
      lcd.print(currentConfig.displayName + " - voll!");
      delay(1200);
      updatePumpDisplay();
    } else {
      performFillOperation();
    }
  }

  // Empty Button
  if (emptyPressed && !motorRunning && !operationInProgress) {
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

  // Motor-Betrieb
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

  for (int i = 0; i < currentConfig.stepSize; i++) {
    // schauen ob schon voll ist
    if (totalSteps - i <= currentConfig.minSteps) {
      motorStopped = true;
      operationDuration = (millis() - operationStartTime) / 1000;
      operationInProgress = false;
      totalSteps -= i;

      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("MAX erreicht");
      lcd.setCursor(0, 1);
      lcd.print(currentConfig.displayName + " - voll!");
      delay(1200);
      break;
    }

    // Stop Button gedrückt?
    if (digitalRead(BTN_START_STOP) == LOW) {
      totalSteps -= i;
      emergencyMotorStop();
      
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("F");
      lcd.write(CHAR_UE_KLEIN);
      lcd.print("llen STOP!");
      delay(1000);
      break;
    }

    // einen Schritt rückwärts (füllen)
    myStepper.step(-1);
    noteActivity();

    if (i % 100 == 0) {
      float currentVol = abs((float)(totalSteps - i) / currentConfig.stepsPerML);
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

      int currentHashes = (int)((currentVol / currentConfig.targetVolume) * 13.0);
      if (currentHashes > 13) currentHashes = 13;
      for (int h = 0; h < currentHashes; h++) lcd.print("#");
    }
  }

  if (!motorStopped) {
    totalSteps -= currentConfig.stepSize;
    operationDuration = (millis() - operationStartTime) / 1000;
    operationInProgress = false;
  }

  updatePumpDisplay();
  motorRelease(); // Motor freigeben nach Operation
  noteActivity(); // Timer zurücksetzen
  delay(80);
}

void performEmptyOperation() {
  motorRunning = false;
  motorStopped = false;
  operationInProgress = true;
  operationStartTime = millis();

  motorEnable();

  for (int i = 0; i < currentConfig.stepSize; i++) {
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

    // Stop Button gedrückt?
    if (digitalRead(BTN_START_STOP) == LOW) {
      motorStopped = true;
      operationDuration = (millis() - operationStartTime) / 1000;
      operationInProgress = false;
      totalSteps += i;
      motorRelease();
      delay(150);
      break;
    }

    // einen Schritt vorwärts (leeren)
    myStepper.step(1);
    noteActivity();

    if (i % 100 == 0) {
      float currentVol = abs((float)(totalSteps + i) / currentConfig.stepsPerML);
      unsigned long currentTime = (millis() - operationStartTime) / 1000;

      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Leeren:");
      lcd.print(currentVol, 2);
      lcd.print("ml");

      lcd.setCursor(0, 1);
      lcd.print(currentTime);
      lcd.print("s ");

      int currentHashes = (int)((currentVol / currentConfig.targetVolume) * 13.0);
      if (currentHashes > 13) currentHashes = 13;
      if (currentHashes < 0)  currentHashes = 0;
      for (int h = 0; h < currentHashes; h++) lcd.print("#");
    }
  }

  if (!motorStopped) {
    totalSteps += currentConfig.stepSize;
    operationDuration = (millis() - operationStartTime) / 1000;
    operationInProgress = false;
  }

  updatePumpDisplay();
  motorRelease(); // Motor freigeben nach Operation
  noteActivity(); // Timer zurücksetzen
  delay(80);
}

// ----------------- Display Anzeige updaten -----------------
void updatePumpDisplay() {
  lcd.clear();
  float currentVol = abs((float)totalSteps / currentConfig.stepsPerML);

  if (motorStopped) {
    lcd.setCursor(0, 0);
    lcd.print("V in der Spritze:");
    lcd.setCursor(0, 1);
    lcd.print(currentVol, 2);
    lcd.print("ml");
    return;
  }

  lcd.setCursor(0, 0);
  lcd.print(currentConfig.displayName + " Vol:");
  lcd.print(currentVol, 2);
  lcd.print("ml");

  lcd.setCursor(0, 1);
  if (motorRunning) {
    lcd.print("LAUFEND");
  } else {
    lcd.print("BEREIT");
  }
}
