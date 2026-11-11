// Desk button -> door sign.
// Press the button and the ESP32-S3 sends the other sign to the e-paper tag
// over Bluetooth. Protocol follows the ATC1441 BLE E-Paper Uploader:
//   service FEF0, FEF1 = commands + notifications, FEF2 = image data.

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <Preferences.h>
#include <string>

#include "signs.h"

// ---- Settings you might change ----------------------------------------
static const char *TAG_DIGITS = "99830221";  // from the tag's barcode
static const int BUTTON_PIN = 4;             // wire a button from here to GND
static const int BOOT_BUTTON_PIN = 0;        // built-in BOOT button also works
static const int LED_PIN = 5;                // drives the LED inside the button
static const uint32_t COOLDOWN_MS = 30000;   // let the screen finish refreshing
static const int MAX_ATTEMPTS = 3;
// ------------------------------------------------------------------------

static const NimBLEUUID SERVICE_UUID((uint16_t)0xFEF0);
static const NimBLEUUID COMMAND_UUID((uint16_t)0xFEF1);
static const NimBLEUUID IMAGE_UUID((uint16_t)0xFEF2);

enum Sign : uint8_t { SIGN_IS_OPEN = 0, SIGN_IS_DND = 1 };

struct Note {
  uint8_t len;
  uint8_t data[16];
};

static QueueHandle_t notes;
static Preferences prefs;
static Sign currentSign = SIGN_IS_OPEN;

// The button's LED: on = Do Not Disturb, off = Open,
// slow blink = sending, fast blink = send failed.
enum LedMode : uint8_t { LED_OFF, LED_SOLID, LED_SENDING, LED_FAILED };
static volatile LedMode ledMode = LED_OFF;

static void ledTask(void *) {
  bool lit = false;
  while (true) {
    const LedMode mode = ledMode;
    if (mode == LED_SENDING || mode == LED_FAILED) {
      lit = !lit;
      digitalWrite(LED_PIN, lit);
      vTaskDelay(pdMS_TO_TICKS(mode == LED_SENDING ? 400 : 100));
    } else {
      lit = mode == LED_SOLID;
      digitalWrite(LED_PIN, lit);
      vTaskDelay(pdMS_TO_TICKS(50));
    }
  }
}

static void showSign() {
  ledMode = currentSign == SIGN_IS_DND ? LED_SOLID : LED_OFF;
}

static void blinkFailure() {
  ledMode = LED_FAILED;
  delay(3000);
}

static void onNotify(NimBLERemoteCharacteristic *, uint8_t *data, size_t len, bool) {
  Note n{};
  n.len = len > sizeof(n.data) ? sizeof(n.data) : len;
  memcpy(n.data, data, n.len);
  xQueueSend(notes, &n, 0);
}

static bool waitNote(Note &n, uint32_t ms = 10000) {
  return xQueueReceive(notes, &n, pdMS_TO_TICKS(ms)) == pdTRUE;
}

// Wait for a reply of one type, skipping anything else (like a repeat reply).
static bool waitFor(uint8_t type, Note &n, uint32_t ms) {
  uint32_t until = millis() + ms;
  while ((int32_t)(until - millis()) > 0) {
    if (!waitNote(n, until - millis())) return false;
    if (n.len >= 1 && n.data[0] == type) return true;
  }
  return false;
}

static uint32_t readLe32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void writeLe32(uint8_t *p, uint32_t v) {
  p[0] = v;
  p[1] = v >> 8;
  p[2] = v >> 16;
  p[3] = v >> 24;
}

static std::string upper(std::string s) {
  for (auto &c : s) c = toupper((unsigned char)c);
  return s;
}

static std::string stripColons(std::string s) {
  std::string out;
  for (char c : s) {
    if (c != ':') out += c;
  }
  return out;
}

// Find the tag. Trust, in order: its digits in the name or address, the
// address saved after an earlier success, then a lone tag-like device.
// Two unidentified tags in range is an error, so we never write to the wrong one.
static bool findTag(NimBLEAddress &found) {
  std::string saved = prefs.isKey("addr") ? prefs.getString("addr").c_str() : "";
  uint8_t savedType = prefs.getUChar("addrType", 0);

  NimBLEScan *scan = NimBLEDevice::getScan();
  scan->setActiveScan(true);
  scan->setInterval(100);
  scan->setWindow(99);
  scan->clearResults();
  Serial.println("Scanning for the tag...");
  NimBLEScanResults results = scan->getResults(8000, false);

  int candidates = 0;
  NimBLEAddress lone;
  bool sawSaved = false;
  NimBLEAddress savedAddr;

  for (int i = 0; i < results.getCount(); i++) {
    const NimBLEAdvertisedDevice *d = results.getDevice(i);
    std::string name = upper(d->getName());
    std::string addr = d->getAddress().toString();
    bool tagLike = d->isAdvertisingService(SERVICE_UUID) || name.find("NEMR") != std::string::npos ||
                   name.find("PICKSMART") != std::string::npos;
    if (tagLike) {
      Serial.printf("  tag-like: '%s' %s rssi %d\n", d->getName().c_str(), addr.c_str(), d->getRSSI());
    }
    bool digitsMatch = name.find(TAG_DIGITS) != std::string::npos ||
                       stripColons(addr).find(TAG_DIGITS) != std::string::npos;
    if (digitsMatch) {
      found = d->getAddress();
      return true;
    }
    if (!saved.empty() && addr == saved) {
      sawSaved = true;
      savedAddr = d->getAddress();
    }
    if (tagLike) {
      candidates++;
      lone = d->getAddress();
    }
  }

  if (sawSaved) {
    found = savedAddr;
    return true;
  }
  if (candidates == 1) {
    found = lone;
    return true;
  }
  if (!saved.empty()) {
    // Not heard this time, but the tag may still accept a direct connection.
    found = NimBLEAddress(saved, savedType);
    return true;
  }
  Serial.printf("Found %d tag-like devices, can't tell which is the sign.\n", candidates);
  return false;
}

static bool upload(NimBLEClient *client, const uint8_t *image, uint32_t imageLen) {
  NimBLERemoteService *service = client->getService(SERVICE_UUID);
  if (!service) {
    Serial.println("Tag service not found.");
    return false;
  }
  NimBLERemoteCharacteristic *command = service->getCharacteristic(COMMAND_UUID);
  NimBLERemoteCharacteristic *imageChar = service->getCharacteristic(IMAGE_UUID);
  if (!command || !imageChar || !command->subscribe(true, onNotify)) {
    Serial.println("Tag characteristics not usable.");
    return false;
  }
  const bool commandResponse = command->canWrite();
  const bool imageResponse = imageChar->canWrite();
  xQueueReset(notes);

  // Step 1: ask how big each image part may be.
  // The tag sometimes misses this right after connecting, so ask again.
  delay(500);
  uint8_t start = 0x01;
  Note n;
  bool started = false;
  for (int i = 1; i <= 5 && !started; i++) {
    if (!command->writeValue(&start, 1, commandResponse)) return false;
    started = waitFor(0x01, n, 3000) && n.len >= 3;
    if (!started) Serial.printf("No reply to start command, try %d.\n", i);
  }
  if (!started) return false;
  const uint16_t messageSize = n.data[1] | (n.data[2] << 8);
  if (messageSize <= 4 || messageSize > 512 || messageSize > client->getMTU() - 3) {
    Serial.printf("Tag wants %u-byte parts, link MTU is %u.\n", messageSize, client->getMTU());
    return false;
  }
  const uint32_t partSize = messageSize - 4;

  // Step 2: announce the image length.
  uint8_t size[8] = {0x02};
  writeLe32(size + 1, imageLen);
  if (!command->writeValue(size, sizeof(size), commandResponse)) return false;
  if (!waitFor(0x02, n, 10000)) {
    Serial.println("No reply to size command.");
    return false;
  }

  // Step 3: the tag asks for parts by number until it reports it's finished.
  uint8_t begin = 0x03;
  if (!command->writeValue(&begin, 1, commandResponse)) return false;

  static uint8_t message[512];
  while (true) {
    if (!waitNote(n)) {
      Serial.println("Tag stopped responding during upload.");
      return false;
    }
    if (n.len < 2 || n.data[0] != 0x05) continue;
    if (n.data[1] == 0x08) {
      Serial.println("Upload complete.");
      return true;
    }
    if (n.data[1] != 0x00 || n.len < 6) {
      Serial.printf("Tag reported upload error 0x%02x.\n", n.data[1]);
      return false;
    }
    const uint32_t part = readLe32(n.data + 2);
    const uint32_t offset = part * partSize;
    if (offset >= imageLen) continue;  // everything sent, wait for 05 08
    const uint32_t count = min(partSize, imageLen - offset);
    writeLe32(message, part);
    memcpy(message + 4, image + offset, count);
    if (!imageChar->writeValue(message, count + 4, imageResponse)) {
      Serial.println("Write failed.");
      return false;
    }
  }
}

static bool sendSign(Sign sign, bool useSavedAddress) {
  const uint8_t *image = sign == SIGN_IS_DND ? SIGN_DND : SIGN_OPEN;
  const uint32_t imageLen = sign == SIGN_IS_DND ? sizeof(SIGN_DND) : sizeof(SIGN_OPEN);

  // Connecting straight to the saved address skips an 8-second scan.
  NimBLEAddress address;
  if (useSavedAddress && prefs.isKey("addr")) {
    address = NimBLEAddress(prefs.getString("addr").c_str(), prefs.getUChar("addrType", 0));
  } else if (!findTag(address)) {
    return false;
  }

  NimBLEClient *client = NimBLEDevice::createClient();
  client->setConnectTimeout(15000);
  Serial.printf("Connecting to %s...\n", address.toString().c_str());
  bool ok = false;
  if (client->connect(address)) {
    Serial.printf("Connected, MTU %u.\n", client->getMTU());
    uint32_t started = millis();
    ok = upload(client, image, imageLen);
    Serial.printf("Upload took %lu ms.\n", (unsigned long)(millis() - started));
    if (client->isConnected()) client->disconnect();
  } else {
    Serial.println("Could not connect.");
  }
  NimBLEDevice::deleteClient(client);

  if (ok) {
    prefs.putString("addr", address.toString().c_str());
    prefs.putUChar("addrType", address.getType());
  }
  return ok;
}

static bool buttonDown() {
  return digitalRead(BUTTON_PIN) == LOW || digitalRead(BOOT_BUTTON_PIN) == LOW;
}

void setup() {
  Serial.begin(115200);
  Serial.setTxTimeoutMs(0);  // don't stall when no computer is attached
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);

  notes = xQueueCreate(8, sizeof(Note));
  prefs.begin("door-sign", false);
  currentSign = prefs.getUChar("sign", SIGN_IS_OPEN) == SIGN_IS_DND ? SIGN_IS_DND : SIGN_IS_OPEN;

  NimBLEDevice::init("door-sign-button");
  NimBLEDevice::setMTU(247);
  showSign();
  xTaskCreate(ledTask, "led", 2048, nullptr, 1, nullptr);
  Serial.println("Ready. Press the button to switch signs.");
}

void loop() {
  if (!buttonDown()) {
    delay(10);
    return;
  }
  delay(30);  // debounce
  if (!buttonDown()) return;

  const Sign next = currentSign == SIGN_IS_DND ? SIGN_IS_OPEN : SIGN_IS_DND;
  Serial.printf("Button pressed, sending %s.\n", next == SIGN_IS_DND ? "DO NOT DISTURB" : "OPEN");
  ledMode = LED_SENDING;

  bool ok = false;
  for (int attempt = 1; attempt <= MAX_ATTEMPTS && !ok; attempt++) {
    Serial.printf("Attempt %d of %d.\n", attempt, MAX_ATTEMPTS);
    ok = sendSign(next, attempt == 1);
    if (!ok && attempt < MAX_ATTEMPTS) delay(2000);
  }

  if (ok) {
    currentSign = next;
    prefs.putUChar("sign", currentSign);
    showSign();
    // Ignore presses while the screen refreshes, to protect the panel.
    uint32_t until = millis() + COOLDOWN_MS;
    while ((int32_t)(until - millis()) > 0) delay(50);
  } else {
    blinkFailure();
    showSign();
  }

  while (buttonDown()) delay(10);  // wait for release
}
