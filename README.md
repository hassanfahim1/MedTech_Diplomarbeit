# MedTech_Diplomarbeit | Open Source Spritzenpumpe

Eine präzise, temperaturoptimierte Spritzenpumpe für medizinische und Laboranwendungen.

## 🎯 Features

- **Präzise Volumensteuerung** für 5ml und 10ml Spritzen
- **Temperaturoptimiert** - Motor wird nicht warm durch intelligente Steuerung
- **Einfache Bedienung** mit nur 3 Buttons
- **LCD-Display** mit deutscher Benutzeroberfläche
- **Not-Halt Funktion** für sicheren Betrieb
- **Open Source** - alle Dateien frei verfügbar

## 🔧 Hardware

### Benötigte Komponenten:
- Arduino Leonardo
- Motor Shield Rev3 (L298P)
- NEMA17 Schrittmotor (17HS08-1004S, 3V/1A)
- I2C LCD Display (16x2)
- 3x Taster (Start/Stop, Füllen, Entleeren)
- 5ml oder 10ml Spritzen

### Anschlüsse:
- **Start/Stop Button:** Pin 2
- **Füllen Button:** Pin 7  
- **Entleeren Button:** Pin 5
- **LCD:** I2C (SDA/SCL)
- **Motor Shield:** Standard Pins (3, 8, 9, 11, 12, 13)

## 💾 Code

### Verfügbare Versionen:
- **`spritzenpumpe_5ml.ino`** - Optimiert für 5ml Spritzen
- **`spritzenpumpe_10ml.ino`** - Optimiert für 10ml Spritzen
- **`Spritzenpumpe_10ml_5ml_RotaryEncoder.ino`** - Universalversion mit Rotary Encoder (5ml + 10ml wählbar)

### Installation:
1. Arduino IDE installieren
2. Benötigte Libraries installieren:
   ```
   - Wire.h (Standard)
   - LiquidCrystal_I2C.h
   - Stepper.h (Standard)
   ```
3. Gewünschte Version auf Arduino Leonardo hochladen

## 🚀 Bedienung

1. **Einschalten** - Display zeigt "Ready..."
2. **Füllen** - Button "Füllen" drücken
3. **Entleeren** - Button "Entleeren" drücken  
4. **Start/Stop** - Jederzeit Motor stoppen/starten

### Display-Anzeigen:
- Aktuelles Volumen in ml
- Füll-/Entleervorgang mit Fortschrittsbalken
- Status: "BEREIT" oder "LAUFEND"

## ⚡ Temperatur-Optimierung

**Problem gelöst:** Motor wird nicht mehr warm!

### Technische Lösung:
- Motor nur bei Bewegung bestromt
- Automatische Freigabe nach 2s Inaktivität
- `motorEnable()` / `motorRelease()` Funktionen
- Bis zu 90% weniger Wärmeentwicklung

## 📋 Technische Daten

### 5ml Version:
- Schritte pro ml: 850
- Schritte pro Bewegung: 4250
- Maximales Volumen: 5.00ml

### 10ml Version:  
- Schritte pro ml: 590
- Schritte pro Bewegung: 5900
- Maximales Volumen: 10.00ml

## ⚠️ Wichtige Hinweise

- **L298P hat keine Stromregelung** - Temperatur-Feature ist essentiell
- **Nur für Laboranwendungen** - nicht für medizinische Direktanwendung
- **Kalibrierung** je nach Spritze eventuell anpassen
- **Sicherheit** - immer Not-Halt griffbereit

## 📁 Projektstruktur

```
MedTech_Diplomarbeit/
├── CAD/                        # CAD-Dateien und Zeichnungen
├── Schema/                     # Schaltpläne und Schemas
├── images/                     # Bilder und Dokumentation
├── scr/                        # Source Code
│   ├── spritzenpumpe_5ml.ino                      # Code für 5ml Spritzen
│   ├── spritzenpumpe_10ml.ino                     # Code für 10ml Spritzen
│   └── Spritzenpumpe_10ml_5ml_RotaryEncoder.ino   # Universalversion mit Encoder
└── README.md                   # Diese Datei
```

## 🛠️ Entwicklung

**Hauptverbesserungen in V1.2:**
- ✅ Temperaturproblem gelöst
- ✅ Encoder entfernt für Einfachheit
- ✅ Separate Versionen für 5ml/10ml
- ✅ Verbesserte Benutzeroberfläche

## 📜 Lizenz

Open Source - frei für Bildung und Forschung verwendbar.

## 🔗 Repository

**GitHub:** https://github.com/hassanfahim1/MedTech_Diplomarbeit

---

*Entwickelt als Teil einer Diplomarbeit im Bereich Medizintechnik*
