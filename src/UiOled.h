#pragma once
#include <Arduino.h>

enum class AppMode : uint8_t {
  Run = 0,
  Diag = 1
};

struct UiState {
  AppMode mode = AppMode::Run;

  bool armed = false;         // v RUN
  bool gate1Signal = false;   // v RUN
  bool inIgnore = false;      // v RUN
  uint8_t stage = 0;          // 0..4 (RUN)
  uint32_t interruptedMs = 0; // RUN debug

  // společné / DIAG
  uint8_t selectedGate = 0;   // 0..9 (B1..B10)
  int16_t diff = 0;
  int16_t diffPeak = 0;
  int16_t noise = 0;
};

class UiOled {
public:
  bool begin();
  void draw(const UiState& s, const uint32_t gateCounts[], uint8_t gateCount);

private:
  bool _ok = false;
};
