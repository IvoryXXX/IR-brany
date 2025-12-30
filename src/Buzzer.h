#pragma once
#include <Arduino.h>

enum class SoundMode : uint8_t {
  Off = 0,
  GateInterruptedStage1,
  GateInterruptedStage2,
  GateInterruptedStage3,
  GateInterruptedSiren,
};

class Buzzer {
public:
  void begin(uint8_t pinA, uint8_t pinB);

  // Jednorázové pípnutí (používej pro boot/confirm)
  void beepMs(uint16_t hz, uint16_t ms);

  // 1x krátké “klik” potvrzení
  void click();

  void off();

  // RUN alarm tick (chunk)
  void tick(SoundMode mode, uint32_t nowMs);

  // DIAG “geiger” tick – pípá rychleji podle diff
  void tickDiagMeter(uint32_t nowMs, int16_t diff);

private:
  uint8_t _a = 255, _b = 255;

  // pro DIAG plánování pípnutí
  uint32_t _nextDiagBeepMs = 0;

  inline void toneStep(uint32_t halfPeriodUs);
  void toneChunk(uint16_t hz, uint16_t steps = 60);
  uint16_t sirenFreq(uint32_t tMs);
};
