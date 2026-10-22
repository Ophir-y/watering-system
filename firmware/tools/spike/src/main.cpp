// Plant-node bench spike. Type a letter in the serial monitor:
//
//   s  soil sensor: power it from a GPIO, wait, median of 9 readings (mV and raw x8)
//   b  battery voltage through the 1 MΩ / 1 MΩ divider
//   p  run the pump for 5 s and print its current every 100 ms (1 Ω shunt)
//   l  light-sleep test: 10 cycles of "sleep 10 s, wake, send one ESP-NOW frame"
//   d  deep-sleep for 30 s (to compare the sleep current)
//
// The `receiver` build runs on the ESP32 DevKit and counts the frames it hears,
// for the range test (see README.md).
#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_sleep.h>
#include <esp_wifi.h>

#include <algorithm>

namespace {

constexpr uint8_t kChannel = 1;
const uint8_t kBroadcast[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

struct Frame {
  uint32_t magic = 0x53504b31;  // "SPK1"
  uint32_t counter = 0;
  uint16_t batteryMv = 0;
};

}  // namespace

#ifdef SPIKE_RECEIVER

namespace {
volatile uint32_t received = 0;
volatile uint32_t lastCounter = 0;
volatile uint32_t firstCounter = 0;

void onRecv(const uint8_t* mac, const uint8_t* data, int len) {
  if (len != sizeof(Frame)) return;
  Frame f;
  memcpy(&f, data, sizeof f);
  if (f.magic != 0x53504b31) return;
  if (received == 0 || f.counter < lastCounter) firstCounter = f.counter;
  lastCounter = f.counter;
  received = received + 1;
}
}  // namespace

void setup() {
  Serial.begin(115200);
  WiFi.mode(WIFI_STA);
  esp_wifi_set_channel(kChannel, WIFI_SECOND_CHAN_NONE);
  esp_now_init();
  esp_now_register_recv_cb(onRecv);
  Serial.printf("Receiver on channel %u, MAC %s\n", kChannel, WiFi.macAddress().c_str());
}

void loop() {
  static uint32_t shown = 0;
  if (received != shown) {
    shown = received;
    const uint32_t sent = lastCounter - firstCounter + 1;
    Serial.printf("frame %lu: heard %lu of %lu sent (%.0f%%)\n", static_cast<unsigned long>(lastCounter),
                  static_cast<unsigned long>(shown), static_cast<unsigned long>(sent), 100.0 * shown / sent);
  }
  delay(50);
}

#else  // plant node

namespace pins {
#if defined(BOARD_SUPERMINI_C3)
constexpr int kSoil = 0, kPumpSense = 1, kBattery = 3, kSoilPower = 4, kPump = 10, kButton = 9, kLed = 8;
constexpr bool kLedActiveLow = true;
#elif defined(BOARD_XIAO_C3)
constexpr int kBattery = 2, kSoil = 3, kPumpSense = 4, kSoilPower = 5, kPump = 10, kButton = 9, kLed = -1;
constexpr bool kLedActiveLow = true;
#elif defined(BOARD_XIAO_S3)
constexpr int kSoil = 1, kPumpSense = 2, kBattery = 4, kSoilPower = 5, kPump = 6, kButton = 0, kLed = 21;
constexpr bool kLedActiveLow = true;
#else
#error "Build one of the board environments in platformio.ini"
#endif
}  // namespace pins

namespace {

RTC_DATA_ATTR uint32_t counter = 0;

uint32_t medianMv(int pin, int n) {
  uint32_t v[15];
  n = std::min(n, 15);
  for (int i = 0; i < n; ++i) v[i] = analogReadMilliVolts(pin);
  std::sort(v, v + n);
  return v[n / 2];
}

uint32_t batteryMv() { return medianMv(pins::kBattery, 9) * 2; }  // 1 MΩ / 1 MΩ divider

void led(bool on) {
  if (pins::kLed >= 0) digitalWrite(pins::kLed, on != pins::kLedActiveLow ? HIGH : LOW);
}

void radioOn() {
  WiFi.mode(WIFI_STA);
#if defined(BOARD_SUPERMINI_C3)
  WiFi.setTxPower(WIFI_POWER_8_5dBm);  // common SuperMini antenna fix
#endif
  esp_wifi_set_channel(kChannel, WIFI_SECOND_CHAN_NONE);
  esp_now_init();
  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, kBroadcast, 6);
  peer.channel = kChannel;
  esp_now_add_peer(&peer);
}

volatile bool sendDone = false;
volatile bool sendOk = false;
void onSent(const uint8_t*, esp_now_send_status_t status) {
  sendOk = status == ESP_NOW_SEND_SUCCESS;
  sendDone = true;
}

bool sendFrame() {
  Frame f;
  f.counter = ++counter;
  f.batteryMv = static_cast<uint16_t>(batteryMv());
  sendDone = false;
  if (esp_now_send(kBroadcast, reinterpret_cast<const uint8_t*>(&f), sizeof f) != ESP_OK) return false;
  const uint32_t t0 = millis();
  while (!sendDone && millis() - t0 < 100) delay(1);
  return sendDone && sendOk;
}

void soil() {
  digitalWrite(pins::kSoilPower, HIGH);
  delay(300);
  const uint32_t mv = medianMv(pins::kSoil, 9);
  digitalWrite(pins::kSoilPower, LOW);
  Serial.printf("soil: %lu mV (raw %lu). Note it in dry soil, wet soil, air and a glass of water.\n",
                static_cast<unsigned long>(mv), static_cast<unsigned long>(mv * 8));
}

void pump() {
  Serial.println("pump: 5 s run. Do it once with the pump under water, once lifted out (dry).");
  const uint32_t restMv = batteryMv();
  digitalWrite(pins::kPump, HIGH);
  const uint32_t t0 = millis();
  double sum = 0;
  int n = 0;
  uint32_t loadMv = 0;
  while (millis() - t0 < 5000) {
    const uint32_t ma = medianMv(pins::kPumpSense, 5);  // 1 Ω: mV == mA
    const uint32_t t = millis() - t0;
    if (t >= 500) {
      sum += ma;
      n++;
    }
    if (t >= 2500 && loadMv == 0) loadMv = batteryMv();
    Serial.printf("  %4lu ms  %4lu mA\n", static_cast<unsigned long>(t), static_cast<unsigned long>(ma));
    delay(100);
  }
  digitalWrite(pins::kPump, LOW);
  const double avg = n ? sum / n : 0;
  Serial.printf("pump: average %.0f mA after inrush; battery %lu mV at rest, %lu mV under load; %.1f mA per volt\n",
                avg, static_cast<unsigned long>(restMv), static_cast<unsigned long>(loadMv),
                loadMv ? avg / (loadMv / 1000.0) : 0);
}

void lightSleepTest() {
  Serial.println("light sleep: 10 cycles of 10 s. Measure the battery current with a meter in series.");
  Serial.flush();
  radioOn();
  esp_now_register_send_cb(onSent);
  for (int i = 0; i < 10; ++i) {
    esp_sleep_enable_timer_wakeup(10ULL * 1000 * 1000);
    led(false);
    esp_light_sleep_start();
    const uint32_t t0 = micros();
    const bool ok = sendFrame();
    const uint32_t awakeUs = micros() - t0;
    led(true);
    delay(20);
    led(false);
    Serial.printf("  wake %d: send %s, %lu us\n", i + 1, ok ? "ok" : "FAILED", static_cast<unsigned long>(awakeUs));
    Serial.flush();
  }
  esp_now_deinit();
  WiFi.mode(WIFI_OFF);
  Serial.println("light sleep: done. ESP-NOW must work after every wake.");
}

void deepSleepTest() {
  Serial.println("deep sleep: 30 s, then the board restarts. Measure the current meanwhile.");
  Serial.flush();
  esp_sleep_enable_timer_wakeup(30ULL * 1000 * 1000);
  esp_deep_sleep_start();
}

}  // namespace

void setup() {
  pinMode(pins::kPump, OUTPUT);
  digitalWrite(pins::kPump, LOW);  // pump off before anything else
  pinMode(pins::kSoilPower, OUTPUT);
  digitalWrite(pins::kSoilPower, LOW);
  if (pins::kLed >= 0) pinMode(pins::kLed, OUTPUT);
  led(false);
  pinMode(pins::kButton, INPUT_PULLUP);
  analogReadResolution(12);
  analogSetPinAttenuation(pins::kSoil, ADC_11db);
  analogSetPinAttenuation(pins::kBattery, ADC_11db);
  analogSetPinAttenuation(pins::kPumpSense, ADC_0db);  // small shunt voltage: best resolution
  Serial.begin(115200);
  delay(1500);
  Serial.println("\nPlant-node spike. Keys: s soil, b battery, p pump, l light sleep, d deep sleep");
}

void loop() {
  if (!Serial.available()) {
    delay(10);
    return;
  }
  switch (Serial.read()) {
    case 's': soil(); break;
    case 'b': Serial.printf("battery: %lu mV\n", static_cast<unsigned long>(batteryMv())); break;
    case 'p': pump(); break;
    case 'l': lightSleepTest(); break;
    case 'd': deepSleepTest(); break;
    default: break;
  }
}

#endif
