#include "I2cDevices.h"

#include <freertos/semphr.h>

#include "TimeKeeper.h"

namespace {

SemaphoreHandle_t gBusMutex = nullptr;

bool probe(TwoWire& wire, uint8_t address) {
  wire.beginTransmission(address);
  return wire.endTransmission() == 0;
}

uint8_t bcd2bin(uint8_t v) { return (v & 0x0F) + 10 * (v >> 4); }
uint8_t bin2bcd(uint8_t v) { return static_cast<uint8_t>(((v / 10) << 4) | (v % 10)); }

void civilFromDays(int64_t z, int& y, unsigned& m, unsigned& d) {
  z += 719468;
  const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
  const unsigned doe = static_cast<unsigned>(z - era * 146097);
  const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned mp = (5 * doy + 2) / 153;
  d = doy - (153 * mp + 2) / 5 + 1;
  m = mp < 10 ? mp + 3 : mp - 9;
  y = static_cast<int>(yoe) + static_cast<int>(era) * 400 + (m <= 2);
}

uint8_t crc8(const uint8_t* data, size_t len) {
  uint8_t crc = 0xFF;
  for (size_t i = 0; i < len; ++i) {
    crc ^= data[i];
    for (int b = 0; b < 8; ++b) crc = (crc & 0x80) ? static_cast<uint8_t>((crc << 1) ^ 0x31) : static_cast<uint8_t>(crc << 1);
  }
  return crc;
}

}  // namespace

// ------------------------------------------------------------------ lock

void I2cLock::init() {
  if (!gBusMutex) gBusMutex = xSemaphoreCreateRecursiveMutex();
}

I2cLock::I2cLock(uint32_t timeoutMs) {
  ok_ = gBusMutex && xSemaphoreTakeRecursive(gBusMutex, pdMS_TO_TICKS(timeoutMs)) == pdTRUE;
}

I2cLock::~I2cLock() {
  if (ok_) xSemaphoreGiveRecursive(gBusMutex);
}

// ------------------------------------------------------------------ ADS1115

bool Ads1115::begin(TwoWire& wire, uint8_t address) {
  wire_ = &wire;
  address_ = address;
  I2cLock lock;
  present_ = lock.ok() && probe(wire, address);
  return present_;
}

bool Ads1115::writeReg(uint8_t reg, uint16_t value) {
  wire_->beginTransmission(address_);
  wire_->write(reg);
  wire_->write(static_cast<uint8_t>(value >> 8));
  wire_->write(static_cast<uint8_t>(value & 0xFF));
  return wire_->endTransmission() == 0;
}

bool Ads1115::readReg(uint8_t reg, uint16_t& value) {
  wire_->beginTransmission(address_);
  wire_->write(reg);
  if (wire_->endTransmission() != 0) return false;
  if (wire_->requestFrom(static_cast<int>(address_), 2) != 2) return false;
  value = static_cast<uint16_t>(wire_->read() << 8);
  value |= static_cast<uint16_t>(wire_->read());
  return true;
}

bool Ads1115::read(uint8_t input, int16_t& value) {
  if (!wire_ || input > 3) return false;
  I2cLock lock;
  if (!lock.ok()) return false;
  // Start a single-shot conversion: AINx vs GND, PGA +-4.096 V, 860 SPS, comparator off.
  const uint16_t config = 0x8000 | static_cast<uint16_t>((0x4 | input) << 12) | (0x1 << 9) | 0x0100 | (0x7 << 5) | 0x0003;
  if (!writeReg(0x01, config)) {
    present_ = false;
    return false;
  }
  for (int i = 0; i < 5; ++i) {
    delayMicroseconds(1300);
    uint16_t status = 0;
    if (!readReg(0x01, status)) break;
    if (status & 0x8000) {  // conversion done
      uint16_t raw = 0;
      if (!readReg(0x00, raw)) break;
      value = static_cast<int16_t>(raw);
      present_ = true;
      return true;
    }
  }
  present_ = false;
  return false;
}

// ------------------------------------------------------------------ SHT4x

bool Sht4x::begin(TwoWire& wire, uint8_t address) {
  wire_ = &wire;
  address_ = address;
  I2cLock lock;
  present_ = lock.ok() && probe(wire, address);
  return present_;
}

bool Sht4x::read(float& tempC, float& humidityPct) {
  if (!wire_) return false;
  I2cLock lock;
  if (!lock.ok()) return false;
  wire_->beginTransmission(address_);
  wire_->write(0xFD);  // measure, high precision
  if (wire_->endTransmission() != 0) {
    present_ = false;
    return false;
  }
  delay(10);
  uint8_t b[6];
  if (wire_->requestFrom(static_cast<int>(address_), 6) != 6) {
    present_ = false;
    return false;
  }
  for (auto& x : b) x = wire_->read();
  if (crc8(b, 2) != b[2] || crc8(b + 3, 2) != b[5]) return false;
  const uint16_t t = static_cast<uint16_t>((b[0] << 8) | b[1]);
  const uint16_t h = static_cast<uint16_t>((b[3] << 8) | b[4]);
  tempC = -45.0f + 175.0f * t / 65535.0f;
  humidityPct = constrain(-6.0f + 125.0f * h / 65535.0f, 0.0f, 100.0f);
  present_ = true;
  return true;
}

// ------------------------------------------------------------------ BH1750

bool Bh1750::begin(TwoWire& wire, uint8_t address) {
  wire_ = &wire;
  address_ = address;
  I2cLock lock;
  if (!lock.ok()) return false;
  wire.beginTransmission(address);
  wire.write(0x01);  // power on
  present_ = wire.endTransmission() == 0;
  if (present_) {
    wire.beginTransmission(address);
    wire.write(0x10);  // continuous high-resolution mode, 1 lx resolution
    present_ = wire.endTransmission() == 0;
  }
  return present_;
}

bool Bh1750::read(float& lux) {
  if (!wire_) return false;
  I2cLock lock;
  if (!lock.ok()) return false;
  if (wire_->requestFrom(static_cast<int>(address_), 2) != 2) {
    present_ = false;
    return false;
  }
  const uint16_t raw = static_cast<uint16_t>((wire_->read() << 8) | wire_->read());
  lux = raw / 1.2f;
  present_ = true;
  return true;
}

// ------------------------------------------------------------------ DS3231

bool Ds3231::begin(TwoWire& wire, uint8_t address) {
  wire_ = &wire;
  address_ = address;
  I2cLock lock;
  present_ = lock.ok() && probe(wire, address);
  return present_;
}

bool Ds3231::read(int64_t& epoch) {
  if (!wire_ || !present_) return false;
  I2cLock lock;
  if (!lock.ok()) return false;
  wire_->beginTransmission(address_);
  wire_->write(0x00);
  if (wire_->endTransmission() != 0) return false;
  if (wire_->requestFrom(static_cast<int>(address_), 7) != 7) return false;
  uint8_t r[7];
  for (auto& x : r) x = wire_->read();
  // Status register: OSF set means the oscillator stopped and the time is invalid.
  wire_->beginTransmission(address_);
  wire_->write(0x0F);
  if (wire_->endTransmission() != 0 || wire_->requestFrom(static_cast<int>(address_), 1) != 1) return false;
  lostPower_ = (wire_->read() & 0x80) != 0;
  if (lostPower_) return false;

  const int sec = bcd2bin(r[0] & 0x7F);
  const int min = bcd2bin(r[1] & 0x7F);
  const int hour = bcd2bin(r[2] & 0x3F);  // 24-hour mode (we always write it that way)
  const unsigned day = bcd2bin(r[4] & 0x3F);
  const unsigned month = bcd2bin(r[5] & 0x1F);
  const int year = 2000 + bcd2bin(r[6]);
  if (month < 1 || month > 12 || day < 1 || day > 31) return false;
  epoch = daysFromCivil(year, month, day) * 86400 + hour * 3600 + min * 60 + sec;
  return true;
}

bool Ds3231::write(int64_t epoch) {
  if (!wire_ || !present_ || epoch <= 0) return false;
  I2cLock lock;
  if (!lock.ok()) return false;
  int y;
  unsigned m, d;
  const int64_t days = epoch / 86400;
  const int64_t secs = epoch % 86400;
  civilFromDays(days, y, m, d);
  const int dow = static_cast<int>((days + 4) % 7) + 1;  // 1970-01-01 was a Thursday
  wire_->beginTransmission(address_);
  wire_->write(0x00);
  wire_->write(bin2bcd(secs % 60));
  wire_->write(bin2bcd((secs / 60) % 60));
  wire_->write(bin2bcd(secs / 3600));
  wire_->write(bin2bcd(dow));
  wire_->write(bin2bcd(d));
  wire_->write(bin2bcd(m));
  wire_->write(bin2bcd(y - 2000));
  if (wire_->endTransmission() != 0) return false;
  // Clear OSF.
  wire_->beginTransmission(address_);
  wire_->write(0x0F);
  wire_->write(0x00);
  if (wire_->endTransmission() != 0) return false;
  lostPower_ = false;
  return true;
}
