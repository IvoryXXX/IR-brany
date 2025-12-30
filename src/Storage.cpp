#include "Storage.h"
#include "config.h"
#include <EEPROM.h>

static const int EEPROM_BASE_ADDR = 0;

void Storage::begin() {
  EEPROM.begin();
}

void Storage::loadCounts(uint32_t gateCounts[], uint8_t gateCount) {
  for (uint8_t i = 0; i < gateCount && i < 10; i++) {
    uint32_t v = 0xFFFFFFFFUL;
    EEPROM.get(EEPROM_BASE_ADDR + (int)i * (int)sizeof(uint32_t), v);
    if (v == 0xFFFFFFFFUL) v = 0;
    gateCounts[i] = v;
    _lastSaved[i] = v;
  }
}

void Storage::saveCountsIfNeeded(const uint32_t gateCounts[], uint8_t gateCount, bool force) {
  uint32_t now = millis();

  bool anyChange = force;
  if (!force) {
    for (uint8_t i = 0; i < gateCount && i < 10; i++) {
      if (gateCounts[i] != _lastSaved[i]) { anyChange = true; break; }
    }
  }
  if (!anyChange) return;
  if (!force && (now - _lastSaveMs) < SAVE_EVERY_MS) return;

  for (uint8_t i = 0; i < gateCount && i < 10; i++) {
    if (force || gateCounts[i] != _lastSaved[i]) {
      EEPROM.put(EEPROM_BASE_ADDR + (int)i * (int)sizeof(uint32_t), gateCounts[i]);
      _lastSaved[i] = gateCounts[i];
    }
  }
  _lastSaveMs = now;
}
