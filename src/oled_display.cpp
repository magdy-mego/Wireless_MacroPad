#include "oled_display.h"

#include <U8g2lib.h>

namespace {
U8G2_SSD1306_128X64_NONAME_F_HW_I2C gDisplay(U8G2_R0, U8X8_PIN_NONE);
}  // namespace

namespace oled_display {

void begin() {
  gDisplay.begin();
  gDisplay.clearBuffer();
  gDisplay.setFont(u8g2_font_5x8_tf);
  gDisplay.drawStr(0, 8, "Macropad booting...");
  gDisplay.sendBuffer();
}

void renderStatus(const config::RuntimeStatus& status) {
  gDisplay.clearBuffer();
  gDisplay.setFont(u8g2_font_5x8_tf);

  char profileBuf[32];
  char bleBuf[32];
  char wifiBuf[32];
  char battBuf[32];
  char chargingBuf[32];

  const char* profileName = status.activeProfileName.isEmpty() ? "N/A" : status.activeProfileName.c_str();
  snprintf(profileBuf, sizeof(profileBuf), "P:%s", profileName);
  snprintf(bleBuf, sizeof(bleBuf), "BLE:%s", status.bleConnected ? "Connected" : "Waiting");
  const char* wifiStatus = status.apEnabled ? (status.wifiClientConnected ? "Client" : "On") : "Off";
  snprintf(wifiBuf, sizeof(wifiBuf), "AP:%s", wifiStatus);
  snprintf(battBuf, sizeof(battBuf), "BAT:%d%% %.2fV", status.batteryPercent, status.batteryVoltage);
  snprintf(chargingBuf, sizeof(chargingBuf), "CHG:%s", status.charging ? "Yes" : "No");

  String lowBattery = status.lowBattery ? "LOW BATTERY - SAVE NOW" : "ENC:Turn=Media Hold=AP";

  gDisplay.drawStr(0, 8, profileBuf);
  gDisplay.drawStr(0, 18, bleBuf);
  gDisplay.drawStr(0, 28, wifiBuf);
  gDisplay.drawStr(0, 38, battBuf);
  gDisplay.drawStr(0, 48, chargingBuf);
  gDisplay.drawStr(0, 58, lowBattery.c_str());

  gDisplay.sendBuffer();
}

}  // namespace oled_display
