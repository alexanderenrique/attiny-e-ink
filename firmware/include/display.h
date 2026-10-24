#pragma once

#include <stdint.h>

// Landscape logical size (rotation 1). Native panel is 128 x 296.
static constexpr uint16_t EPD_WIDTH = 296;
static constexpr uint16_t EPD_HEIGHT = 128;
static constexpr uint16_t EPD_NATIVE_WIDTH = 128;
static constexpr uint16_t EPD_NATIVE_HEIGHT = 296;

struct DisplayQuote {
  const char* deckLabel;   // red FreeSerif Bold header
  const char* quote;       // black FreeSerif body
  const char* author;      // black FreeSerif, last line (may be empty)
  int batteryPercent;      // -1 to hide; else red corner readout
};

// Power and SPI must already be on for the panel.
bool displayShowQuote(const DisplayQuote& q);

// Red "Battery Low" / black "Please Charge". Power and SPI must already be on.
bool displayShowLowBattery(int batteryPercent);

// Red "No SD card detected". Power and SPI must already be on.
bool displayShowNoSdCard(int batteryPercent);
