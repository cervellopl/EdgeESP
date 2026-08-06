#include "ui/Display.h"

// Deliberately slow, deliberately dumb: this runs once at boot before the real
// driver claims the pins, so clarity beats speed.

static const int kData[16] = {
  LCD_D0, LCD_D1, LCD_D2,  LCD_D3,  LCD_D4,  LCD_D5,  LCD_D6,  LCD_D7,
  LCD_D8, LCD_D9, LCD_D10, LCD_D11, LCD_D12, LCD_D13, LCD_D14, LCD_D15};

static void busOut() { for (int p : kData) pinMode(p, OUTPUT); }
static void busIn()  { for (int p : kData) pinMode(p, INPUT); }

static void writeBus(uint16_t v) {
  for (int i = 0; i < 16; i++) digitalWrite(kData[i], (v >> i) & 1);
}

static uint16_t readBus() {
  uint16_t v = 0;
  for (int i = 0; i < 16; i++) if (digitalRead(kData[i])) v |= (1u << i);
  return v;
}

static void writeCmd(uint8_t c) {
  digitalWrite(LCD_RS, LOW);
  busOut();
  writeBus(c);
  digitalWrite(LCD_WR, LOW);  delayMicroseconds(1);
  digitalWrite(LCD_WR, HIGH); delayMicroseconds(1);
  digitalWrite(LCD_RS, HIGH);
}

static uint16_t readData() {
  busIn();
  digitalWrite(LCD_RD, LOW);
  delayMicroseconds(2);                 // panels need a long RD low for reads
  uint16_t v = readBus();
  digitalWrite(LCD_RD, HIGH);
  delayMicroseconds(1);
  return v;
}

static void dumpReg(const char* label, uint8_t reg, int words) {
  digitalWrite(LCD_CS, LOW);
  writeCmd(reg);
  Serial.printf("  %s (0x%02X):", label, reg);
  for (int i = 0; i < words; i++) Serial.printf(" %04X", readData());
  Serial.println();
  digitalWrite(LCD_CS, HIGH);
}

void dumpPanelId() {
  pinMode(LCD_CS, OUTPUT);  digitalWrite(LCD_CS, HIGH);
  pinMode(LCD_RS, OUTPUT);  digitalWrite(LCD_RS, HIGH);
  pinMode(LCD_WR, OUTPUT);  digitalWrite(LCD_WR, HIGH);
  pinMode(LCD_RD, OUTPUT);  digitalWrite(LCD_RD, HIGH);
  pinMode(LCD_RST, OUTPUT);

  digitalWrite(LCD_RST, HIGH); delay(5);
  digitalWrite(LCD_RST, LOW);  delay(20);
  digitalWrite(LCD_RST, HIGH); delay(150);

  Serial.println(F("--- panel ID probe ---"));
  dumpReg("RDDID  ", 0x04, 4);   // ILI9341-style
  dumpReg("RDDID4 ", 0xD3, 4);   // ILI9486/ILI9488 -> .. 00 94 86
  dumpReg("RDID_HX", 0xBF, 6);   // ILI9481 / HX8357 family
  dumpReg("RDDST  ", 0x09, 5);
  Serial.println(F("Set -DPANEL_ILI9486 / _ILI9481 / _R61581 / _HX8357B to match, "
                   "then rebuild."));

  // Hand the pins back in a neutral state for LovyanGFX.
  busIn();
}
