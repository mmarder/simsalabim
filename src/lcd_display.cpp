#include "lcd_display.h"
#include "config.h"

#include <Wire.h>
#include <LiquidCrystal_I2C.h>

namespace lcd_display {

namespace {
LiquidCrystal_I2C lcd(LCD_I2C_ADDR, LCD_COLS, LCD_ROWS);
bool present = false;
Screen current = SCREEN_BOOT;

char line1[LCD_COLS + 1] = {0};
char line2[LCD_COLS + 1] = {0};
int updatePercent = 0;

// Probes whether a device ACKs at the configured I2C address.
bool i2cPresent(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

// Writes a string into a fixed 16-char buffer, space-padded, NUL-terminated.
void fmtLine(char* dst, const char* src) {
  int i = 0;
  for (; src[i] && i < LCD_COLS; i++) dst[i] = src[i];
  for (; i < LCD_COLS; i++) dst[i] = ' ';
  dst[LCD_COLS] = '\0';
}

void draw() {
  if (!present) return;
  lcd.setCursor(0, 0);
  lcd.print(line1);
  lcd.setCursor(0, 1);
  lcd.print(line2);
}
}  // namespace

bool begin() {
  Wire.begin(LCD_SDA_PIN, LCD_SCL_PIN);
  present = i2cPresent(LCD_I2C_ADDR);
  if (!present) return false;

  lcd.init();
  lcd.backlight();
  fmtLine(line1, DEVICE_NAME " " FIRMWARE_VERSION);
  fmtLine(line2, "Starte...");
  draw();
  return true;
}

void setScreen(Screen s) {
  current = s;
  if (s == SCREEN_AP) {
    fmtLine(line1, "Setup-Modus");
    fmtLine(line2, WIFI_AP_SSID);
  }
  tick();
}

void setStatus(const String& ip, int rssi) {
  current = SCREEN_STATUS;
  fmtLine(line1, ip.c_str());
  char buf[LCD_COLS + 1];
  snprintf(buf, sizeof(buf), "WiFi %ddBm OK", rssi);
  fmtLine(line2, buf);
}

void setUpdateProgress(int percent) {
  current = SCREEN_UPDATE;
  updatePercent = constrain(percent, 0, 100);
}

void setAlarm(const String& l1, const String& l2) {
  current = SCREEN_ALARM;
  fmtLine(line1, l1.c_str());
  fmtLine(line2, l2.c_str());
}

void tick() {
  if (!present) return;

  if (current == SCREEN_UPDATE) {
    char bar[LCD_COLS + 1];
    snprintf(line1, sizeof(line1), "Update %3d%%     ", updatePercent);
    line1[LCD_COLS] = '\0';
    // 10-cell progress bar.
    int filled = (updatePercent * 10) / 100;
    int n = 0;
    bar[n++] = '[';
    for (int i = 0; i < 10; i++) bar[n++] = (i < filled) ? '=' : ' ';
    bar[n++] = ']';
    bar[n] = '\0';
    fmtLine(line2, bar);
  }

  draw();
}

}  // namespace lcd_display
