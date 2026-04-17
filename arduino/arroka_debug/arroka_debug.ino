/**
 * Arroka Pro 90 — Sketch de diagnostic RS485
 * 
 * Permet de sniffer le bus et d'envoyer des commandes
 * via le moniteur série Arduino (réglé sur "Both NL & CR").
 * 
 * Câblage :
 *   ESP32 GPIO16 (RX2) → MAX485 RO
 *   ESP32 GPIO17 (TX2) → MAX485 DI
 *   ESP32 GPIO4        → MAX485 DE+RE
 *   ESP32 GND          → MAX485 GND
 *   ESP32 U5 (5V)      → MAX485 VCC
 *   MAX485 A           → PAC CN8 A
 *   MAX485 B           → PAC CN8 B
 *   MAX485 GND         → PAC CN8 GND
 * 
 * Commandes disponibles :
 *   ON       → Allume la PAC
 *   OFF      → Éteint la PAC
 *   HEAT     → Mode chauffage
 *   COOL     → Mode refroidissement
 *   SET 28   → Consigne 28°C (15 à 32)
 *   STATUS   → Affiche l'état actuel
 */

#include <HardwareSerial.h>

HardwareSerial RS485Serial(2);
#define DE_RE_PIN 4

// ── État PAC ──────────────────────────────────────────────────
float   water_temp = 0, air_temp = 0, setpoint = 0;
bool    pac_on   = false;
bool    pac_heat = true;

// ── Dernière trame CC reçue ───────────────────────────────────
uint8_t lastCC[13] = {0xCC,0x19,0x1C,0x2D,0x07,0x0D,0xA0,
                      0x2C,0x19,0x02,0x00,0x7F,0xB9};
bool    lastCCvalid = false;

// ── Commande en attente ───────────────────────────────────────
bool    pendingCommand  = false;
bool    pendingOn       = false;
bool    pendingHeat     = true;
uint8_t pendingSP       = 28;

// ── Buffer RS485 ──────────────────────────────────────────────
uint8_t buf[64];
int     bufLen = 0;
uint32_t lastByte = 0;
String  cmdBuffer = "";

// ── Calcul byte[07] MODE/FLAG ─────────────────────────────────
uint8_t modeFlag(bool on, bool heat) {
  return 0x0C | (on ? 0x40 : 0x00) | (heat ? 0x20 : 0x00);
}

// ── Envoi RS485 ───────────────────────────────────────────────
void sendRS485(uint8_t* frame) {
  digitalWrite(DE_RE_PIN, HIGH);
  delay(2);
  RS485Serial.write(frame, 13);
  RS485Serial.flush();
  delay(2);
  digitalWrite(DE_RE_PIN, LOW);
}

// ── Construction et envoi d'une commande ─────────────────────
void sendCommand(bool on, bool heat, uint8_t sp) {
  if (!lastCCvalid) {
    Serial.println("ERREUR: pas encore de trame CC reçue");
    return;
  }
  uint8_t frame[13];
  memcpy(frame, lastCC, 13);
  frame[0]  = 0xCD;
  frame[2]  = sp;
  frame[7]  = modeFlag(on, heat);

  uint8_t x = 0;
  for (int i = 0; i < 12; i++) x ^= frame[i];
  frame[12] = x ^ 0xBD;

  Serial.print(">>> TX: ");
  for (int i = 0; i < 13; i++) {
    if (frame[i] < 0x10) Serial.print("0");
    Serial.print(frame[i], HEX);
    Serial.print(" ");
  }
  Serial.printf(" | PAC=%s MODE=%s SP=%d°C\n",
    on ? "ON" : "OFF", heat ? "HEAT" : "COOL", sp);

  sendRS485(frame);
}

// ── Décodage trame reçue ──────────────────────────────────────
void decodeFrame(uint8_t* d) {
  if (d[0] == 0xDD) {
    water_temp = d[1];
    air_temp   = d[5];
    pac_on     = (d[8] != 0x00);
  }
  if (d[0] == 0xCC) {
    setpoint  = d[2];
    pac_heat  = (d[7] & 0x20) != 0;
    pac_on    = (d[7] & 0x40) != 0;
    memcpy(lastCC, d, 13);
    lastCCvalid = true;
  }
}

// ── Affichage état ────────────────────────────────────────────
void printStatus() {
  Serial.printf("T_eau=%.0f°C  T_air=%.0f°C  Consigne=%.0f°C  PAC=%s  MODE=%s\n",
    water_temp, air_temp, setpoint,
    pac_on ? "ON" : "OFF",
    pac_heat ? "HEAT" : "COOL");
}

void setup() {
  Serial.begin(115200);
  pinMode(DE_RE_PIN, OUTPUT);
  digitalWrite(DE_RE_PIN, LOW);
  RS485Serial.begin(9600, SERIAL_8N1, 16, 17);
  Serial.println("=== Arroka Pro 90 — Diagnostic RS485 ===");
  Serial.println("Commandes: ON / OFF / HEAT / COOL / SET 28 / STATUS");
  Serial.println("Réglez le moniteur série sur 'Both NL & CR'");
}

void loop() {
  // ── Lecture RS485 ─────────────────────────────────────────
  if (RS485Serial.available()) {
    buf[bufLen++] = RS485Serial.read();
    lastByte = millis();

    if (bufLen == 13) {
      decodeFrame(buf);

      // Injection commande après trame CC
      if (buf[0] == 0xCC && pendingCommand) {
        delay(3);
        sendCommand(pendingOn, pendingHeat, pendingSP);
        pendingCommand = false;
      }
      bufLen = 0;
    }
  }
  if (bufLen > 0 && millis() - lastByte > 20) bufLen = 0;

  // ── Commandes série ───────────────────────────────────────
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      cmdBuffer.trim();
      if (cmdBuffer.length() > 0) {
        Serial.print("CMD: ["); Serial.print(cmdBuffer); Serial.println("]");

        if (cmdBuffer.equalsIgnoreCase("ON")) {
          pendingCommand = true; pendingOn = true;
          pendingHeat = pac_heat; pendingSP = (uint8_t)setpoint;
          Serial.println(">> ON...");

        } else if (cmdBuffer.equalsIgnoreCase("OFF")) {
          pendingCommand = true; pendingOn = false;
          pendingHeat = pac_heat; pendingSP = (uint8_t)setpoint;
          Serial.println(">> OFF...");

        } else if (cmdBuffer.equalsIgnoreCase("HEAT")) {
          pendingCommand = true; pendingOn = pac_on;
          pendingHeat = true; pendingSP = (uint8_t)setpoint;
          Serial.println(">> Mode HEAT...");

        } else if (cmdBuffer.equalsIgnoreCase("COOL")) {
          pendingCommand = true; pendingOn = pac_on;
          pendingHeat = false; pendingSP = (uint8_t)setpoint;
          Serial.println(">> Mode COOL...");

        } else if (cmdBuffer.startsWith("SET ") || cmdBuffer.startsWith("set ")) {
          int sp = cmdBuffer.substring(4).toInt();
          if (sp >= 15 && sp <= 32) {
            pendingCommand = true; pendingOn = pac_on;
            pendingHeat = pac_heat; pendingSP = (uint8_t)sp;
            Serial.printf(">> Consigne %d°C...\n", sp);
          } else {
            Serial.println("Consigne invalide (15-32°C)");
          }

        } else if (cmdBuffer.equalsIgnoreCase("STATUS")) {
          printStatus();
        }
        cmdBuffer = "";
      }
    } else {
      cmdBuffer += c;
    }
  }
}
