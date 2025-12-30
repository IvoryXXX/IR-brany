#pragma once
#include <Arduino.h>

class Gate {
public:
  void begin(uint8_t adcPin);

  // volat pořád
  void update();

  // DIAG
  void setIdle();              // 3× klik
  int16_t getDiff() const;
  int16_t getStrength() const;

  // RUN
  bool isBroken(uint16_t thr) const;

private:
  uint8_t _pin;
  uint16_t _base = 0;
  int16_t  _diff = 0;
  int16_t  _idle = 0;
  bool     _idleSet = false;

  //mala funkce
  public:
  bool hasIdleSet() const { return _idleSet; }



};
