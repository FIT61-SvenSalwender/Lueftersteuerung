#include <Wire.h>                          // Ermoeglicht die I2C-Kommunikation.
#include <DHT.h>                           // Steuert den DHT11-Sensor.
#include <Adafruit_LiquidCrystal.h>         // Steuert das LCD.
#include <Adafruit_MCP23X08.h>              // Steuert den MCP23008 der Tastenmatrix.
 
const byte FAN_PIN = 14;                   // ESP32-Anschluss fuer den Lueftertreiber.
const int MIN_PROZENT = 40;                // Kleinste erlaubte Ansteuerung bei laufendem Luefter.
const int MIN_PWM = (MIN_PROZENT * 255 + 99) / 100; // Rechnet den Mindestwert auf die PWM-Skala um und rundet auf.
const unsigned long ANLAUF_MS = 500;       // Dauer des Anlaufimpulses mit 100 Prozent.
const unsigned long RAMPE_MS = 20;        // Zeit zwischen zwei PWM-Schritten; groesser bedeutet langsamer.
const float REGELBEREICH = 3.0;            // Temperaturbereich zwischen rechnerisch 0 und 100 Prozent.
const float ABSCHALT_ABSTAND = 1.0;        // Abschaltgrenze liegt ein Grad unter der Solltemperatur.
 
const byte zeilen[] = {0, 1, 2, 3};        // Zeilenanschluesse am MCP23008.
const byte spalten[] = {4, 5, 6, 7};       // Spaltenanschluesse am MCP23008.
 
const char tasten[4][4] = {                 // Ordnet jeder Taste ihr Zeichen zu.
  {'7', '8', '9', 'X'},                     // Zeichen der ersten Zeile.
  {'4', '5', '6', '/'},                     // Zeichen der zweiten Zeile.
  {'1', '2', '3', '='},                     // Zeichen der dritten Zeile.
  {'0', '#', '+', '-'}                      // Zeichen der vierten Zeile.
};                                         // Beendet die Tastentabelle.
 
DHT dht(17, DHT11);                         // Verwendet einen DHT11 an GPIO 17.
Adafruit_LiquidCrystal lcd(0x21);           // Verwendet das LCD an I2C-Adresse 0x21.
Adafruit_MCP23X08 keypad;                   // Erstellt das Objekt fuer den Keypad-Expander.
 
float soll = 26.5;                         // Gewuenschte Temperatur in Grad Celsius.
float ist = NAN;                           // Noch kein gueltiger Temperaturwert vorhanden.
int pwmAktuell = 255;                      // Startet bis zur ersten gueltigen Messung mit 100 Prozent.
 
char leseTaste() {                         // Gibt ein erkanntes Tastenzeichen oder 0 zurueck.
  for (byte r = 0; r < 4; r++) {            // Prueft die vier Zeilen nacheinander.
    keypad.digitalWrite(zeilen[r], LOW);   // Aktiviert die aktuelle Zeile.
    delayMicroseconds(300);                // Wartet auf stabile Anschlusspegel.
 
    for (byte c = 0; c < 4; c++) {          // Prueft die vier Spalten.
      if (keypad.digitalRead(spalten[c]) == LOW) { // Erkennt eine gedrueckte Taste.
        keypad.digitalWrite(zeilen[r], HIGH); // Deaktiviert die Zeile vor der Rueckgabe.
        return tasten[r][c];               // Gibt das zugehoerige Tastenzeichen zurueck.
      }                                    // Beendet die Tastenpruefung.
    }                                      // Beendet die Spaltenschleife.
 
    keypad.digitalWrite(zeilen[r], HIGH);  // Deaktiviert die gepruefte Zeile.
  }                                        // Beendet die Zeilenschleife.
  return 0;                                // Keine Taste wurde erkannt.
}                                          // Beendet die Funktion leseTaste.
 
void regleLuefter() {                      // Regelt PWM, Anlaufimpuls und Mindestansteuerung.
  static unsigned long rampenZeit = 0;     // Speichert den Zeitpunkt der letzten Rampenanpassung.
  static unsigned long anlaufZeit = 0;     // Speichert den Beginn des Anlaufimpulses.
  static bool anlaufAktiv = false;         // Merkt sich, ob gerade der Anlaufimpuls laeuft.
 
  unsigned long jetzt = millis();          // Liest die aktuelle Zeit seit dem Programmstart.
 
  if (isnan(ist)) {                        // Prueft auf einen fehlenden oder fehlerhaften Messwert.
    anlaufAktiv = false;                   // Beendet einen eventuell aktiven Anlaufimpuls.
    pwmAktuell = 255;                      // Setzt den PWM-Wert sofort auf 100 Prozent.
    analogWrite(FAN_PIN, pwmAktuell);      // Gibt volle Ansteuerung am Luefteranschluss aus.
    rampenZeit = jetzt;                    // Setzt den Zeitbezug fuer die Rampe zurueck.
    return;                                // Ueberspringt die normale Regelung bei einem Sensorfehler.
  }                                        // Beendet die Sensorfehlerbehandlung.
 
  float anteil = (ist - (soll - ABSCHALT_ABSTAND)) / REGELBEREICH; // Berechnet die Position im Temperatur-Regelbereich.
  anteil = constrain(anteil, 0.0f, 1.0f);   // Begrenzt den berechneten Anteil auf 0 bis 1.
  int pwmZiel = (int)(anteil * 255 + 0.5f); // Berechnet den gerundeten PWM-Zielwert zwischen 0 und 255.
 
  if (anteil > 0) {                        // Prueft, ob die Temperatur oberhalb der Abschaltgrenze liegt.
    pwmZiel = max(pwmZiel, MIN_PWM);        // Verlangt bei Luefterbedarf mindestens 40 Prozent.
  }                                        // Beendet die Begrenzung des Zielwerts.
 
  if (!anlaufAktiv && pwmAktuell == 0 && pwmZiel > 0) { // Erkennt das Einschalten nach einer Ausgabe von 0 Prozent.
    anlaufAktiv = true;                    // Aktiviert den Anlaufimpuls.
    anlaufZeit = jetzt;                    // Speichert den Startzeitpunkt des Impulses.
    pwmAktuell = 255;                      // Setzt den aktuellen PWM-Wert auf 100 Prozent.
  }                                        // Beendet die Erkennung eines neuen Starts.
 
  if (anlaufAktiv) {                       // Prueft, ob der Anlaufimpuls aktiv ist.
    if (pwmZiel > 0 && jetzt - anlaufZeit < ANLAUF_MS) { // Haelt den Impuls bei bestehendem Bedarf fuer 500 Millisekunden.
      analogWrite(FAN_PIN, 255);           // Gibt waehrend des Impulses 100 Prozent aus.
      rampenZeit = jetzt;                  // Verhindert angesammelte Rampenschritte waehrend des Impulses.
      return;                              // Wartet mit der normalen Regelung bis zum Ende des Impulses.
    }                                      // Beendet die Ausgabe des Anlaufimpulses.
 
    anlaufAktiv = false;                   // Beendet den Impuls nach Ablauf oder bei entfallenem Bedarf.
    rampenZeit = jetzt;                    // Beginnt die anschliessende Rampe ab diesem Zeitpunkt.
  }                                        // Beendet die Behandlung des Anlaufimpulses.
 
  unsigned long schritte = (jetzt - rampenZeit) / RAMPE_MS; // Berechnet die Anzahl faelliger PWM-Schritte.
  if (schritte == 0) return;                // Wartet ohne Blockierung bis zum naechsten faelligen Schritt.
  rampenZeit = jetzt;                       // Speichert den Zeitpunkt der aktuellen Anpassung.
 
  int aenderung = (int)min(schritte, 255UL); // Begrenzt die Anpassung auf maximal 255 PWM-Stufen.
 
  if (pwmAktuell < pwmZiel) {              // Prueft, ob die Ansteuerung erhoeht werden muss.
    pwmAktuell = min(pwmAktuell + aenderung, pwmZiel); // Erhoeht den PWM-Wert bis maximal zum Ziel.
  } else if (pwmAktuell > pwmZiel) {        // Prueft, ob die Ansteuerung verringert werden muss.
    pwmAktuell = max(pwmAktuell - aenderung, pwmZiel); // Senkt den PWM-Wert bis maximal zum Ziel ab.
  }                                        // Beendet die schrittweise Anpassung.
 
  if (pwmAktuell > 0 && pwmAktuell < MIN_PWM) { // Erkennt eine Ausgabe im unzuverlaessigen Bereich unter 40 Prozent.
    pwmAktuell = (pwmZiel == 0) ? 0 : MIN_PWM; // Schaltet ganz aus oder begrenzt auf den Mindestwert.
  }                                        // Beendet die Begrenzung des ausgegebenen PWM-Werts.
 
  analogWrite(FAN_PIN, pwmAktuell);        // Gibt entweder 0 Prozent oder mindestens 40 Prozent aus.
}                                          // Beendet die Funktion regleLuefter.
 
void zeigeLCD() {                          // Zeigt Temperaturen und den aktuellen PWM-Prozentwert an.
  int prozent = (pwmAktuell * 100L + 127) / 255; // Rechnet den ausgegebenen PWM-Wert gerundet in Prozent um.
 
  lcd.setCursor(0, 0);                     // Springt an den Anfang der ersten Zeile.
  lcd.print("            ");               // Loescht die ersten zwoelf Zeichen der ersten Zeile.
  lcd.setCursor(0, 0);                     // Springt erneut an den Anfang der ersten Zeile.
 
  if (isnan(ist)) {                        // Prueft auf einen fehlenden oder fehlerhaften Messwert.
    lcd.print("Sensorfehler");             // Zeigt die Fehlermeldung links neben dem Prozentwert.
  } else {                                 // Waehlt bei gueltiger Messung die Temperaturanzeige.
    lcd.print("IST:");                     // Schreibt die Beschriftung der Isttemperatur.
    lcd.print(ist, 1);                     // Schreibt die Temperatur mit einer Nachkommastelle.
    lcd.print("C");                        // Schreibt die Einheit Celsius.
  }                                        // Beendet die Auswahl der Temperaturanzeige.
 
  lcd.setCursor(12, 0);                    // Setzt die Ausgabe auf die letzten vier Stellen oben rechts.
  if (prozent < 100) lcd.print(" ");       // Ergaenzt ein Leerzeichen bei Werten unter 100 Prozent.
  if (prozent < 10) lcd.print(" ");        // Ergaenzt ein weiteres Leerzeichen bei einstelligen Werten.
  lcd.print(prozent);                      // Schreibt den aktuell ausgegebenen PWM-Prozentwert.
  lcd.print("%");                          // Schreibt das Prozentzeichen an die letzte Stelle.
 
  lcd.setCursor(0, 1);                     // Springt an den Anfang der zweiten Zeile.
  lcd.print("SOLL:");                      // Schreibt die Beschriftung der Solltemperatur.
  lcd.print(soll, 1);                       // Schreibt den Sollwert mit einer Nachkommastelle.
  lcd.print(" C     ");                    // Schreibt die Einheit und entfernt alte Zeichen.
}                                          // Beendet die Funktion zeigeLCD.
 
void setup() {                            // Initialisiert die Komponenten einmal beim Start.
  Serial.begin(115200);                    // Startet die serielle Schnittstelle.
  pinMode(FAN_PIN, OUTPUT);                // Konfiguriert den Luefteranschluss als Ausgang.
  analogWrite(FAN_PIN, pwmAktuell);        // Startet die PWM-Ausgabe mit 100 Prozent.
  analogWriteFrequency(FAN_PIN, 1000);     // Stellt die PWM-Frequenz auf 1000 Hertz.
  dht.begin();                            // Initialisiert den DHT11-Sensor.
  Wire.begin(21, 22);                      // Startet I2C mit SDA an GPIO 21 und SCL an GPIO 22.
  Wire.setClock(100000);                   // Stellt den I2C-Takt auf 100 Kilohertz.
 
  if (!lcd.begin(16, 2) || !keypad.begin_I2C(0x22)) { // Initialisiert LCD und Keypad und prueft auf Fehler.
    Serial.println("LCD oder Keypad nicht erreichbar!"); // Meldet einen Initialisierungsfehler.
    while (true) {                        // Stoppt den weiteren Start; der Luefter bleibt auf 100 Prozent.
      delay(1000);                        // Wartet in der Endlosschleife jeweils eine Sekunde.
    }                                      // Beendet den Schleifenblock.
  }                                        // Beendet die Fehlerbehandlung.
 
  lcd.setBacklight(HIGH);                  // Schaltet die LCD-Hintergrundbeleuchtung ein.
 
  for (byte i = 0; i < 4; i++) {            // Richtet die vier Zeilen und vier Spalten ein.
    keypad.digitalWrite(zeilen[i], HIGH);  // Bereitet den inaktiven Zeilenpegel vor.
    keypad.pinMode(zeilen[i], OUTPUT);     // Verwendet die Zeile als Ausgang.
    keypad.pinMode(spalten[i], INPUT_PULLUP); // Verwendet die Spalte als Eingang mit Pullup.
  }                                        // Beendet die Einrichtung der Tastenmatrix.
 
  lcd.print("Antlantec GmbH");             // Zeigt den Firmennamen an.
  lcd.setCursor(0, 1);                     // Wechselt in die zweite Zeile.
  lcd.print("System startet");             // Zeigt die Startmeldung an.
  delay(1500);                            // Wartet 1,5 Sekunden.
  lcd.clear();                            // Entfernt die Startmeldung.
}                                          // Beendet die Initialisierung.
 
void loop() {                             // Wiederholt Tastenabfrage, Messung, Regelung und Anzeige.
  static char letzteTaste = 0;             // Merkt sich den zuletzt uebernommenen Tastenzustand.
  static unsigned long tastenZeit = 0;     // Speichert den Zeitpunkt der letzten Tastenzustandsaenderung.
  static unsigned long messZeit = 0;       // Speichert den Zeitpunkt der letzten Temperaturabfrage.
  static unsigned long lcdZeit = 0;        // Speichert den Zeitpunkt der letzten Anzeigeaktualisierung.
 
  char taste = leseTaste();                // Liest die aktuell gedrueckte Taste.
 
  if (taste != letzteTaste && millis() - tastenZeit >= 80) { // Uebernimmt Tastenaenderungen mit mindestens 80 ms Abstand.
    letzteTaste = taste;                   // Speichert den uebernommenen Tastenzustand.
    tastenZeit = millis();                 // Speichert den Zeitpunkt dieser Aenderung.
    if (taste == '+') soll += 0.5;         // Erhoeht die Solltemperatur um 0,5 Grad.
    if (taste == '-') soll -= 0.5;         // Verringert die Solltemperatur um 0,5 Grad.
    soll = constrain(soll, 15.0f, 40.0f);   // Begrenzt die Solltemperatur auf 15 bis 40 Grad.
  }                                        // Beendet die Tastenverarbeitung.
 
  if (millis() - messZeit >= 2000) {        // Fragt die Temperatur alle zwei Sekunden ab.
    messZeit = millis();                   // Speichert den Zeitpunkt der Temperaturabfrage.
    ist = dht.readTemperature();           // Speichert die Temperatur oder NAN bei einem Messfehler.
  }                                        // Beendet die Temperaturabfrage.
 
  regleLuefter();                          // Aktualisiert Anlaufimpuls, Rampe und Mindestansteuerung.
 
  if (millis() - lcdZeit >= 250) {          // Aktualisiert das LCD viermal pro Sekunde.
    lcdZeit = millis();                    // Speichert den Zeitpunkt der Anzeigeaktualisierung.
    zeigeLCD();                            // Zeigt Temperaturen und den aktuellen PWM-Prozentwert an.
  }                                        // Beendet die Anzeigeaktualisierung.
 
  delay(5);                               // Legt eine kurze Pause zwischen den Durchlaeufen ein.
}                                          // Beendet den aktuellen Durchlauf.