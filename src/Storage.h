#pragma once
#include <Arduino.h>

class Storage {
public:
  void begin();
  void loadCounts(uint32_t gateCounts[], uint8_t gateCount);
  void saveCountsIfNeeded(const uint32_t gateCounts[], uint8_t gateCount, bool force = false);

private:
  uint32_t _lastSaveMs = 0;
  uint32_t _lastSaved[10] = {0};
};
