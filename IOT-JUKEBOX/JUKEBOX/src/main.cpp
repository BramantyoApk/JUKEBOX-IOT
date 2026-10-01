/**
 * ============================================================================
 * Proyek: ESP32 Minecraft Jukebox
 * Sensor: TCS3000 / TCS3200 (8 Pin: S0, S1, S2, S3, OUT, VCC, GND, LED)
 * Media: LittleFS Internal Flash (Partisi 2.8 MB, Tanpa MicroSD Card)
 * Output: Bluetooth Speaker A2DP Source
 * Target Speaker: YMX-868BT (MAC: 12:11:8A:1A:2A:08)
 * Optimasi: FreeRTOS Audio Task (Core 1), Max TX Power (+9dBm),
 *           Pre-buffering, Lightweight Disc Sensor Check (No Audio Starvation).
 * ============================================================================
 */

#include <Arduino.h>
#include <FS.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/ringbuf.h>
#include "esp_bt.h"
#include "BluetoothA2DPSource.h"
#include "MP3DecoderHelix.h"

// ============================================================================
// 1. KONFIGURASI TARGET BLUETOOTH SPEAKER (YMX-868BT)
// ============================================================================
const char* BT_SPEAKER_NAME = "YMX-868BT";
esp_bd_addr_t TARGET_SPEAKER_MAC = {0x12, 0x11, 0x8A, 0x1A, 0x2A, 0x08}; // MAC YMX-868BT

// Set ke 1 untuk Serial Monitor Raw Color Dump saat kalibrasi kaset fisik
#define CALIBRATION_MODE 0

// ============================================================================
// 2. PEMETAAN PIN HARDWARE SENSOR TCS3000 / TCS3200
// ============================================================================
#define S0_PIN    14    // Skala Frekuensi
#define S1_PIN    27    // Skala Frekuensi
#define S2_PIN    26    // Filter Fotodioda
#define S3_PIN    25    // Filter Fotodioda
#define OUT_PIN   32    // Sinyal frekuensi keluaran sensor (Terdeteksi aktif di GPIO 32)
#define LED_PIN   33    // Kontrol lampu LED sensor (GPIO 33)

// Ambang batas durasi pulsa Clear (us) untuk deteksi kaset masuk
// Kaset berjarak dekat: ~7-15 us. Slot kosong / latar belakang: ~80 us / timeout.
// Ambang batas 50 us memisahkan kaset vs slot kosong secara akurat & stabil!
const uint32_t DISC_INSERTED_PULSE_THRESHOLD = 50;

// Penyimpanan NVS untuk Volume & Pemetaan Kaset
Preferences preferences;
String trackYellow = "akudahlupa.mp3";
String trackRed = "whatif.mp3";
String trackBlue = "otherside.mp3";

// Antrian Lagu & Transisi Intro/Eject DVD
String nextTrackToPlay = "";
volatile bool shouldTransitionTrack = false;

// ============================================================================
// 3. DATABASE WARNA KASET DISK MINECRAFT
// ============================================================================
struct DiscColor {
  const char* trackName;
  const char* filePath;
  float normR;
  float normG;
  float normB;
  float tolerance;
};

const DiscColor discDatabase[] = {
  {"13",        "/akudahlupa.mp3", 0.50f, 0.42f, 0.08f, 0.20f}, // Kuning Emas -> aku dah lupa
  {"cat",       "/cat.mp3",       0.20f, 0.65f, 0.15f, 0.12f}, // Hijau Terang
  {"blocks",    "/blocks.mp3",    0.68f, 0.22f, 0.10f, 0.12f}, // Oranye / Merah
  {"chirp",     "/chirp.mp3",     0.65f, 0.18f, 0.17f, 0.12f}, // Merah Tua
  {"far",       "/far.mp3",       0.35f, 0.50f, 0.15f, 0.12f}, // Hijau Muda
  {"mall",      "/mall.mp3",      0.38f, 0.22f, 0.40f, 0.12f}, // Ungu
  {"mellohi",   "/mellohi.mp3",   0.48f, 0.22f, 0.30f, 0.12f}, // Pink / Magenta
  {"stal",      "/stal.mp3",      0.33f, 0.33f, 0.34f, 0.10f}, // Hitam
  {"strad",     "/strad.mp3",     0.38f, 0.38f, 0.24f, 0.10f}, // Putih
  {"ward",      "/ward.mp3",      0.18f, 0.52f, 0.30f, 0.12f}, // Hijau Gelap
  {"11",        "/11.mp3",        0.33f, 0.33f, 0.34f, 0.10f}, // Abu Gelap
  {"wait",      "/wait.mp3",      0.15f, 0.35f, 0.50f, 0.12f}, // Biru Laut
  {"otherside", "/otherside.mp3", 0.12f, 0.50f, 0.38f, 0.12f}, // Biru Teal
  {"pigstep",   "/pigstep.mp3",   0.58f, 0.18f, 0.24f, 0.12f}, // Merah Netherite
  {"relic",     "/relic.mp3",     0.55f, 0.32f, 0.13f, 0.12f}  // Cokelat Tembaga
};
const size_t NUM_DISCS = sizeof(discDatabase) / sizeof(discDatabase[0]);

// ============================================================================
// 4. OBJEK GLOBAL AUDIO & STATUS
// ============================================================================
BluetoothA2DPSource a2dp_source;

// Custom MP3DecoderHelix agar struktur decoder internal (8.7 KB) dan buffer frame/pcm
// tetap persisten di heap dan TIDAK pernah di-free atau di-reallocate saat stop/start musik.
class PersistentMP3Decoder : public libhelix::MP3DecoderHelix {
public:
  bool initOnce() {
    if (decoder == nullptr) {
      decoder = MP3InitDecoder();
    }
    frame_buffer.resize(maxFrameSize());
    pcm_buffer.resize(maxPCMSize());
    if (pcm_buffer.data() != nullptr) memset(pcm_buffer.data(), 0, maxPCMSize());
    if (frame_buffer.data() != nullptr) memset(frame_buffer.data(), 0, maxFrameSize());
    active = true;
    return (decoder != nullptr && frame_buffer.data() != nullptr && pcm_buffer.data() != nullptr);
  }

  bool begin() override {
    frame_buffer.reset();
    frame_counter = 0;
    active = true;
    return true;
  }

  void end() override {
    flush();
    frame_buffer.reset();
    memset(&mp3FrameInfo, 0, sizeof(mp3FrameInfo));
    active = false;
  }
};
PersistentMP3Decoder mp3Decoder;

RingbufHandle_t audioRingBuffer = nullptr;
File currentTrackFile;
TaskHandle_t audioTaskHandle = nullptr;

enum SystemState {
  STATE_WAIT_BLUETOOTH,
  STATE_IDLE_WAIT_DISC,
  STATE_PLAYING
};

volatile SystemState currentState = STATE_WAIT_BLUETOOTH;
volatile int currentPlayingIndex = -1;
volatile bool isDiscPresent = false;
volatile bool isBtConnected = false;
volatile bool isTrackFileEof = false;
int currentVolume = 80; // 0 - 100%

// Ukuran Ring Buffer Audio (24 KB = 6144 stereo sample = ~140ms audio stabil tanpa kehabisan RAM)
const size_t AUDIO_RINGBUF_SIZE = 24576;
const size_t PREBUFFER_MIN_SIZE = 8192;  // 8 KB pre-buffering (~46ms audio di awal)
const size_t MP3_READ_CHUNK_SIZE = 512;  // 512 bytes per baca (max 2 frame MP3 = ~9KB PCM, mencegah buffer overflow & audio patah-patah)
uint8_t mp3ReadBuffer[MP3_READ_CHUNK_SIZE];

unsigned long lastSensorReadTime = 0;
unsigned long lastBtStatusLog = 0;
unsigned long debounceStartTime = 0;

// ============================================================================
// 5. CALLBACK DEKODER MP3 HELIX
// ============================================================================
void mp3DataCallback(MP3FrameInfo &info, short *pcm_buffer, size_t len, void *ref) {
  if (audioRingBuffer == nullptr || len == 0) return;

  if (info.nChans == 2) {
    // Stereo: langsung masukkan ke ringbuffer
    size_t bytesToSend = len * sizeof(short);
    xRingbufferSend(audioRingBuffer, (void*)pcm_buffer, bytesToSend, pdMS_TO_TICKS(50));
  } else if (info.nChans == 1) {
    // Mono: gandakan sample ke L dan R (Stereo Frame)
    Frame stereoTemp[576];
    size_t samplesRemaining = len;
    size_t offset = 0;

    while (samplesRemaining > 0) {
      size_t batch = min((size_t)576, samplesRemaining);
      for (size_t i = 0; i < batch; i++) {
        stereoTemp[i].channel1 = pcm_buffer[offset + i];
        stereoTemp[i].channel2 = pcm_buffer[offset + i];
      }
      xRingbufferSend(audioRingBuffer, (void*)stereoTemp, batch * sizeof(Frame), pdMS_TO_TICKS(50));
      samplesRemaining -= batch;
      offset += batch;
    }
  }
}

// ============================================================================
// 6. FREERTOS TASK UNTUK DEKODER AUDIO (CORE 1)
// ============================================================================
void audioDecodeTask(void *pvParameters) {
  while (true) {
    if (currentState == STATE_PLAYING && currentTrackFile && currentTrackFile.available()) {
      // Pastikan ada ruang cukup di ring buffer sebelum decode chunk berikutnya (minimal 10 KB free).
      // 512 byte MP3 menghasilkan maksimal 2 frame PCM (~9.2 KB).
      // Dengan sisa ruang >= 10.000 byte, xRingbufferSend TIDAK AKAN PERNAH timeout atau membuang frame audio!
      if (audioRingBuffer != nullptr && xRingbufferGetCurFreeSize(audioRingBuffer) >= 10000) {
        size_t bytesRead = currentTrackFile.read(mp3ReadBuffer, sizeof(mp3ReadBuffer));
        if (bytesRead > 0) {
          mp3Decoder.write(mp3ReadBuffer, bytesRead);
        }
        if (!currentTrackFile.available()) {
          isTrackFileEof = true;
        }
        taskYIELD();
      } else {
        // Buffer sudah cukup terisi, istirahat 4ms untuk efisiensi CPU dan biarkan Bluetooth streaming
        vTaskDelay(pdMS_TO_TICKS(4));
      }
    } else {
      // Tidak sedang memutar musik, istirahat 20ms
      vTaskDelay(pdMS_TO_TICKS(20));
    }
  }
}

// ============================================================================
// 7. CALLBACK STREAM AUDIO BLUETOOTH A2DP SOURCE
// ============================================================================
int32_t get_sound_data(Frame *data, int32_t frame_count) {
  size_t bytesNeeded = frame_count * sizeof(Frame);

  if (currentState != STATE_PLAYING || audioRingBuffer == nullptr) {
    memset(data, 0, bytesNeeded);
    return frame_count;
  }

  size_t totalReceived = 0;
  uint8_t *dst = (uint8_t*)data;

  // Tarik data dari ring buffer. Jika data terputus di batas memori ring buffer (wrap-around),
  // lakukan penarikan lanjutan hingga bytesNeeded terpenuhi agar audio tidak patah-patah!
  while (totalReceived < bytesNeeded) {
    size_t chunkReceived = 0;
    uint8_t *item = (uint8_t*) xRingbufferReceiveUpTo(
        audioRingBuffer, &chunkReceived, 0, bytesNeeded - totalReceived);
    if (item != nullptr && chunkReceived > 0) {
      memcpy(dst + totalReceived, item, chunkReceived);
      vRingbufferReturnItem(audioRingBuffer, (void*)item);
      totalReceived += chunkReceived;
    } else {
      break;
    }
  }

  // Jika data di ring buffer benar-benar habis (underrun atau EOF)
  if (totalReceived < bytesNeeded) {
    memset(dst + totalReceived, 0, bytesNeeded - totalReceived);

    // Jika file MP3 sudah habis dibaca dan ring buffer benar-benar kosong
    if (currentState == STATE_PLAYING && isTrackFileEof && totalReceived == 0) {
      if (nextTrackToPlay.length() > 0) {
        // Beritahu main loop untuk transisi dari DVD ke lagu utama
        shouldTransitionTrack = true;
      } else {
        Serial.println("[AUDIO] Lagu / sound selesai diputar.");
        if (currentTrackFile) {
          currentTrackFile.close();
        }
        mp3Decoder.end();
        currentPlayingIndex = -1;
        // JANGAN reset isDiscPresent ke false di sini!
        // Jika kaset fisik masih di slot, isDiscPresent tetap true agar TIDAK memicu pemutaran ulang otomatis!
        currentState = isBtConnected ? STATE_IDLE_WAIT_DISC : STATE_WAIT_BLUETOOTH;
      }
    }
  }

  return frame_count;
}

// ============================================================================
// 8. CALLBACK DISCOVERY & KONEKSI BLUETOOTH
// ============================================================================
bool bt_ssid_callback(const char* ssid, esp_bd_addr_t address, int rssi) {
  char bda_str[18];
  sprintf(bda_str, "%02X:%02X:%02X:%02X:%02X:%02X",
          address[0], address[1], address[2], address[3], address[4], address[5]);
  Serial.printf("[BT SCAN] Ditemukan: \"%s\" [%s] RSSI: %d\n", ssid ? ssid : "(No Name)", bda_str, rssi);

  // Abaikan laptop / HP jika terdeteksi
  if (ssid != nullptr && (strcmp(ssid, "MSI") == 0 || strcasecmp(bda_str, "04:EC:D8:68:15:09") == 0 || strstr(ssid, "OPPO") != nullptr)) {
    return false;
  }

  // Cocokkan MAC target YMX-868BT [12:11:8A:1A:2A:08] atau nama speaker
  if (memcmp(address, TARGET_SPEAKER_MAC, 6) == 0 ||
      (ssid != nullptr && (strstr(ssid, "YMX-868BT") != nullptr || strstr(ssid, "YMX") != nullptr || strstr(ssid, "868BT") != nullptr || strstr(ssid, "JZ-B5W2") != nullptr))) {
    Serial.printf("[BT MATCH] >>> TARGET TERDETEKSI: \"%s\" [%s] (RSSI: %d)! MENGHUBUNGKAN... <<<\n", 
                  ssid ? ssid : "YMX-868BT", bda_str, rssi);
    return true;
  }

  return false;
}

void bt_connection_state_callback(esp_a2d_connection_state_t state, void *) {
  Serial.printf("[BT STATE] Status Koneksi: %s\n", a2dp_source.to_str(state));
  if (state == ESP_A2D_CONNECTION_STATE_CONNECTED) {
    isBtConnected = true;
    if (currentState != STATE_PLAYING) {
      currentState = STATE_IDLE_WAIT_DISC;
    }
    Serial.println("\n**************************************************");
    Serial.println("  [BT SUCCESS] BERHASIL TERHUBUNG KE SPEAKER!    ");
    Serial.printf("  Target: %s                                     \n", BT_SPEAKER_NAME);
    Serial.println("  Jukebox Siap Digunakan. Masukkan Kaset Disc!   ");
    Serial.println("**************************************************\n");
  } else if (state == ESP_A2D_CONNECTION_STATE_DISCONNECTED) {
    isBtConnected = false;
    if (currentState != STATE_PLAYING) {
      currentState = STATE_WAIT_BLUETOOTH;
    }
    Serial.println("[BT STATUS] Koneksi terputus. Mencoba rekoneksi otomatis...");
  }
}

// ============================================================================
// 9. PEMBACAAN SENSOR TCS3000 / TCS3200
// ============================================================================
struct ColorReading {
  uint32_t pulseR;
  uint32_t pulseG;
  uint32_t pulseB;
  uint32_t pulseClear;
  float normR;
  float normG;
  float normB;
  bool isDetected;
};

// Helper pembacaan pulsa yang mencoba LOW dan HIGH
uint32_t readSensorPulse(int pin, uint32_t timeoutUs) {
  uint32_t p = pulseIn(pin, LOW, timeoutUs);
  if (p == 0) {
    p = pulseIn(pin, HIGH, timeoutUs);
  }
  return p;
}

// Pembacaan ringan saat lagu sedang berjalan (HANYA cek pin Clear, timeout 600us super cepat tanpa delay CPU)
bool isDiscStillInserted() {
  digitalWrite(S2_PIN, HIGH);
  digitalWrite(S3_PIN, LOW); // Filter Clear
  delayMicroseconds(20);
  uint32_t pulseClear = readSensorPulse(OUT_PIN, 600);
  // Jika 0 (akibat interupsi transmisi audio Bluetooth), lakukan baca ulang 1x
  if (pulseClear == 0) {
    pulseClear = readSensorPulse(OUT_PIN, 600);
  }
  return (pulseClear > 0 && pulseClear <= DISC_INSERTED_PULSE_THRESHOLD);
}

// Pembacaan penuh saat idle/standby untuk klasifikasi warna
ColorReading readTCS3000() {
  ColorReading c = {0, 0, 0, 0, 0.0f, 0.0f, 0.0f, false};

  // 1. Baca Merah (S2=LOW, S3=LOW)
  digitalWrite(S2_PIN, LOW);
  digitalWrite(S3_PIN, LOW);
  delayMicroseconds(20);
  c.pulseR = readSensorPulse(OUT_PIN, 8000);

  // 2. Baca Hijau (S2=HIGH, S3=HIGH)
  digitalWrite(S2_PIN, HIGH);
  digitalWrite(S3_PIN, HIGH);
  delayMicroseconds(20);
  c.pulseG = readSensorPulse(OUT_PIN, 8000);

  // 3. Baca Biru (S2=LOW, S3=HIGH)
  digitalWrite(S2_PIN, LOW);
  digitalWrite(S3_PIN, HIGH);
  delayMicroseconds(20);
  c.pulseB = readSensorPulse(OUT_PIN, 8000);

  // 4. Baca Clear (S2=HIGH, S3=LOW)
  digitalWrite(S2_PIN, HIGH);
  digitalWrite(S3_PIN, LOW);
  delayMicroseconds(20);
  c.pulseClear = readSensorPulse(OUT_PIN, 8000);

  float intR = (c.pulseR > 0) ? (10000.0f / (float)c.pulseR) : 0.0f;
  float intG = (c.pulseG > 0) ? (10000.0f / (float)c.pulseG) : 0.0f;
  float intB = (c.pulseB > 0) ? (10000.0f / (float)c.pulseB) : 0.0f;

  float sum = intR + intG + intB;
  if (sum > 0.001f) {
    c.normR = intR / sum;
    c.normG = intG / sum;
    c.normB = intB / sum;
  }

  // Deteksi kaset jika ada pantulan cahaya (pulseClear <= DISC_INSERTED_PULSE_THRESHOLD dan > 0)
  if (c.pulseClear > 0 && c.pulseClear <= DISC_INSERTED_PULSE_THRESHOLD) {
    c.isDetected = true;
  }

  return c;
}

enum DetectedColor {
  COLOR_NONE = 0,
  COLOR_YELLOW,
  COLOR_RED,
  COLOR_BLUE
};

// Klasifikasi warna mutual eksklusif (tidak saling tumpang tindih)
DetectedColor classifyColor(float r, float g, float b) {
  // 1. KUNING (Yellow):
  // Syarat: Hijau WAJIB tinggi (g >= 0.32), Merah tinggi (r >= 0.35),
  // Biru rendah (b <= 0.27), dan selisih r dan g seimbang (|r - g| <= 0.12).
  if (g >= 0.32f && r >= 0.35f && b <= 0.27f && fabs(r - g) <= 0.12f) {
    return COLOR_YELLOW;
  }

  // 2. MERAH (Red):
  // Syarat: Merah dominan (r >= 0.43), Hijau rendah (g <= 0.29),
  // selisih r - g signifikan (>= 0.14), dan Merah JELAS lebih tinggi dari Biru (r - b >= 0.08).
  if (r >= 0.43f && g <= 0.29f && (r - g) >= 0.14f && (r - b) >= 0.08f) {
    return COLOR_RED;
  }

  // 3. BIRU (Blue):
  // Syarat: Biru dominan (b >= 0.38), Merah WAJIB rendah (r <= 0.30),
  // Biru jauh lebih tinggi dari Merah (b - r >= 0.10).
  if (b >= 0.38f && r <= 0.30f && (b - r) >= 0.10f) {
    return COLOR_BLUE;
  }

  // Jarak Euclidean sebagai fallback dengan batasan ketat:
  // Kuning HANYA jika g >= 0.32 dan b <= 0.27
  if (g >= 0.32f && b <= 0.27f) {
    float dr = r - 0.42f, dg = g - 0.37f, db = b - 0.21f;
    if (sqrt(dr * dr + dg * dg + db * db) <= 0.14f) return COLOR_YELLOW;
  }
  // Merah HANYA jika r >= 0.43 dan g <= 0.29 dan r > b
  if (r >= 0.43f && g <= 0.29f && r > b) {
    float dr = r - 0.52f, dg = g - 0.22f, db = b - 0.27f;
    if (sqrt(dr * dr + dg * dg + db * db) <= 0.18f) return COLOR_RED;
  }
  // Biru HANYA jika b >= 0.36 dan r <= 0.30
  if (b >= 0.36f && r <= 0.30f) {
    float dr = r - 0.22f, dg = g - 0.32f, db = b - 0.46f;
    if (sqrt(dr * dr + dg * dg + db * db) <= 0.18f) return COLOR_BLUE;
  }

  return COLOR_NONE;
}

bool isYellowColor(float r, float g, float b) {
  return classifyColor(r, g, b) == COLOR_YELLOW;
}

bool isRedColor(float r, float g, float b) {
  return classifyColor(r, g, b) == COLOR_RED;
}

bool isBlueColor(float r, float g, float b) {
  return classifyColor(r, g, b) == COLOR_BLUE;
}

int matchDiscColor(float nr, float ng, float nb) {
  int bestMatch = -1;
  float minDistance = 999.0f;

  for (size_t i = 0; i < NUM_DISCS; i++) {
    float dr = nr - discDatabase[i].normR;
    float dg = ng - discDatabase[i].normG;
    float db = nb - discDatabase[i].normB;
    float dist = sqrt(dr * dr + dg * dg + db * db);

    if (dist < discDatabase[i].tolerance && dist < minDistance) {
      minDistance = dist;
      bestMatch = i;
    }
  }
  return bestMatch;
}

// ============================================================================
// 10. KONTROL AUDIO (PLAY & STOP DENGAN PRE-BUFFERING)
// ============================================================================
void stopTrack() {
  if (currentState == STATE_PLAYING) {
    currentState = isBtConnected ? STATE_IDLE_WAIT_DISC : STATE_WAIT_BLUETOOTH;
    Serial.println("[JUKEBOX] Musik dihentikan.");
    if (currentTrackFile) {
      currentTrackFile.close();
    }
    currentPlayingIndex = -1;
    // Jika kaset fisik masih di slot, pertahankan isDiscPresent = true agar tidak re-trigger otomatis
    isDiscPresent = isDiscStillInserted();
    mp3Decoder.end(); // Bebaskan alokasi heap decoder Helix (8.7 KB)

    // Bersihkan ring buffer
    if (audioRingBuffer != nullptr) {
      size_t dummySize;
      void *dummy;
      while ((dummy = xRingbufferReceiveUpTo(audioRingBuffer, &dummySize, 0, 4096)) != nullptr) {
        vRingbufferReturnItem(audioRingBuffer, dummy);
      }
    }
  }
}

void startTrackByPath(const char* path) {
  String fullPath = path ? path : "";
  fullPath.trim();
  if (fullPath.length() == 0) return;
  if (!fullPath.startsWith("/")) fullPath = "/" + fullPath;
  if (!fullPath.endsWith(".mp3")) fullPath += ".mp3";
  stopTrack();

  Serial.printf("[JUKEBOX] Memutar track audio: %s\n", fullPath.c_str());

  currentTrackFile = LittleFS.open(fullPath.c_str(), "r");
  if (!currentTrackFile) {
    Serial.printf("[ERROR] File %s tidak ditemukan di Flash LittleFS!\n", fullPath.c_str());
    if (nextTrackToPlay.length() > 0) {
      String toPlay = nextTrackToPlay;
      nextTrackToPlay = "";
      Serial.printf("[FALLBACK] Langsung beralih ke lagu target: %s\n", toPlay.c_str());
      startTrackByPath(toPlay.c_str());
    }
    return;
  }

  // Reset status dekoder MP3
  mp3Decoder.begin();
  isTrackFileEof = false;

  // PRE-BUFFERING: Isi buffer audio sebelum audio dialirkan ke speaker
  // Ini mencegah buffer underrun di awal lagu
  size_t prebuffered = 0;
  while (currentTrackFile.available() && prebuffered < PREBUFFER_MIN_SIZE) {
    size_t bytesRead = currentTrackFile.read(mp3ReadBuffer, sizeof(mp3ReadBuffer));
    if (bytesRead > 0) {
      mp3Decoder.write(mp3ReadBuffer, bytesRead);
      prebuffered += bytesRead * 4;
    }
  }

  currentState = STATE_PLAYING;
}

void startTrack(int discIndex) {
  if (discIndex < 0 || discIndex >= (int)NUM_DISCS) return;
  Serial.printf("[JUKEBOX] Kaset: %s\n", discDatabase[discIndex].trackName);
  currentPlayingIndex = discIndex;
  isDiscPresent = true; // Kaset fisik aktif
  startTrackByPath(discDatabase[discIndex].filePath);
}

// ============================================================================
// 11. SETUP SISTEM
// ============================================================================
void setup() {
  // Maksimalkan CPU Clock ke 240 MHz
  setCpuFrequencyMhz(240);

  Serial.begin(115200);
  delay(500);
  Serial.println("\n==================================================");
  Serial.println("  MINECRAFT JUKEBOX ESP32 (AUDIO OPTIMIZED)        ");
  Serial.println("  Target Speaker: YMX-868BT [12:11:8A:1A:2A:08]   ");
  Serial.println("==================================================");

  // Inisialisasi NVS Preferences untuk Volume & Pemetaan Kaset
  preferences.begin("jukebox", false);
  currentVolume = preferences.getInt("vol", 80);
  currentVolume = constrain(currentVolume, 0, 100);
  trackYellow = preferences.getString("t_yellow", "akudahlupa.mp3");
  trackRed = preferences.getString("t_red", "whatif.mp3");
  trackBlue = preferences.getString("t_blue", "otherside.mp3");
  if (trackBlue == "pigstep.mp3") {
    trackBlue = "otherside.mp3";
    preferences.putString("t_blue", trackBlue);
  }
  Serial.printf("[NVS] Konfigurasi Dimuat: Volume=%d%% | Kuning=%s | Merah=%s | Biru=%s\n",
                currentVolume, trackYellow.c_str(), trackRed.c_str(), trackBlue.c_str());

  // 1. Inisialisasi Helix MP3 Decoder terlebih dahulu saat heap masih bersih & contiguous
  mp3Decoder.setDataCallback(mp3DataCallback);
  if (!mp3Decoder.initOnce()) {
    Serial.println("[ERROR] Gagal menginisialisasi Helix MP3 Decoder!");
  } else {
    Serial.println("[OK] Helix MP3 Decoder siap & alokasi persisten di heap.");
  }

  // 2. Inisialisasi Ring Buffer Audio (24 KB)
  audioRingBuffer = xRingbufferCreate(AUDIO_RINGBUF_SIZE, RINGBUF_TYPE_BYTEBUF);
  if (audioRingBuffer == nullptr) {
    Serial.println("[ERROR] Gagal mengalokasikan Audio RingBuffer di RAM!");
  } else {
    Serial.printf("[OK] Audio RingBuffer dialokasikan: %u Bytes.\n", AUDIO_RINGBUF_SIZE);
  }

  // Inisialisasi Pin TCS3000
  pinMode(S0_PIN, OUTPUT);
  pinMode(S1_PIN, OUTPUT);
  pinMode(S2_PIN, OUTPUT);
  pinMode(S3_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  pinMode(OUT_PIN, INPUT_PULLUP);

  digitalWrite(S0_PIN, HIGH);
  digitalWrite(S1_PIN, LOW);   // 20% frequency scale
  digitalWrite(LED_PIN, HIGH);  // Nyalakan LED penerangan sensor
  Serial.println("[OK] Sensor TCS3000 diinisialisasi (LED ON).");

  // Inisialisasi LittleFS Flash Internal
  if (!LittleFS.begin(true)) {
    Serial.println("[ERROR] Gagal me-mount LittleFS!");
  } else {
    Serial.println("[OK] LittleFS Flash Internal siap. Daftar lagu:");
    File root = LittleFS.open("/");
    File file = root.openNextFile();
    while (file) {
      Serial.printf("  * /%s (%u KB)\n", file.name(), (uint32_t)(file.size() / 1024));
      file = root.openNextFile();
    }
  }

  // Konfigurasi Bluetooth A2DP Source
  Serial.println("[BT] Menyiapkan modul Bluetooth Classic A2DP Source...");
  a2dp_source.set_ssp_enabled(true);                        // Secure Simple Pairing
  a2dp_source.set_pin_code("0000", ESP_BT_PIN_TYPE_FIXED); // PIN modul
  a2dp_source.set_auto_reconnect(TARGET_SPEAKER_MAC, 5);    // Auto-reconnect langsung ke MAC YMX-868BT
  a2dp_source.set_ssid_callback(bt_ssid_callback);          // Scan & matching YMX-868BT & JZ-B5W2
  a2dp_source.set_on_connection_state_changed(bt_connection_state_callback);
  a2dp_source.set_volume((currentVolume * 127) / 100);

  Serial.printf("[BT] Mencari dan menghubungkan ke target speaker: %s [12:11:8A:1A:2A:08]...\n", BT_SPEAKER_NAME);
  std::vector<const char*> target_names = {"YMX-868BT", "JZ-B5W2"};
  a2dp_source.start(target_names, get_sound_data);

  // TINGKATKAN DAYA PANCAR RADIO BLUETOOTH KE LEVEL MAKSIMUM (+9 dBm)
  esp_bredr_tx_power_set(ESP_PWR_LVL_P9, ESP_PWR_LVL_P9);
  Serial.println("[BT TX] Daya pancar radio disetel ke level MAKSIMUM (+9 dBm).");

  // BUAT FREERTOS TASK UNTUK AUDIO STREAMING DI CORE 1
  xTaskCreatePinnedToCore(
    audioDecodeTask,
    "AudioTask",
    8192,
    NULL,
    5,              // Prioritas tinggi
    &audioTaskHandle,
    1               // Core 1
  );
  Serial.println("[OK] FreeRTOS Audio Task berjalan di Core 1.");

  Serial.println("[READY] ESP32 Jukebox Boot Selesai!");
}

// ============================================================================
// 12. MAIN LOOP (SENSOR RINGAN & SERIAL COMMAND)
// ============================================================================
void loop() {
  unsigned long now = millis();

  // Transisi track otomatis (misal: selesai /dvd.mp3 langsung memutar lagu utama)
  if (shouldTransitionTrack) {
    shouldTransitionTrack = false;
    if (nextTrackToPlay.length() > 0) {
      String toPlay = nextTrackToPlay;
      nextTrackToPlay = "";
      Serial.printf("\n[JUKEBOX] Intro DVD selesai! Memutar lagu utama: %s\n", toPlay.c_str());
      startTrackByPath(toPlay.c_str());
      isDiscPresent = true;
    }
  }

  // 1. Status log berkala jika Bluetooth belum tersambung
  if (!isBtConnected && (now - lastBtStatusLog >= 4000)) {
    lastBtStatusLog = now;
    Serial.printf("[BT CONNECTING] Menunggu speaker %s... Pastikan speaker menyala & dalam mode pairing!\n", BT_SPEAKER_NAME);
  }

  // 2. Pembacaan Sensor Warna
  static int discEjectCounter = 0;
  if (currentState == STATE_PLAYING) {
    // HANYA cek pencabutan kaset JIKA pemutaran dipicu oleh kaset fisik (isDiscPresent == true)!
    if (isDiscPresent) {
      if (now - lastSensorReadTime >= 350) {
        lastSensorReadTime = now;
        bool stillPresent = isDiscStillInserted();
        if (!stillPresent) {
          discEjectCounter++;
          // Wajib 3 kali berturut-turut membaca KOSONG (3 x 350ms = ~1s stabil)
          // 100% mencegah lagu terhenti di tengah jalan akibat sensor jitter atau interrupt Bluetooth!
          if (discEjectCounter >= 3) {
            discEjectCounter = 0;
            isDiscPresent = false;
            nextTrackToPlay = "";
            shouldTransitionTrack = false;
            Serial.println("\n[SENSOR] Kaset dicabut dari slot saat memutar! Menghentikan lagu dan memutar sound DVD eject...");
            startTrackByPath("/dvd.mp3");
            isDiscPresent = false;
          }
        } else {
          // Masih terdeteksi ada kaset: langsung reset counter false-alarm
          discEjectCounter = 0;
        }
      }
    } else {
      // Jika diputar manual dari Web Dashboard atau sedang memutar sound eject:
      discEjectCounter = 0;
      debounceStartTime = 0;
    }
  } else {
    // SAAT STANDBY (IDLE): BACA SENSOR WARNA LENGKAP UNTUK VERIFIKASI KASET
    discEjectCounter = 0;
    if (now - lastSensorReadTime >= 100) {
      lastSensorReadTime = now;
      ColorReading c = readTCS3000();

      // Log live setiap 1.5 detik agar terlihat di web terminal
      static unsigned long lastSensorLogTime = 0;
      if (now - lastSensorLogTime >= 1500) {
        lastSensorLogTime = now;
        int pinVal = digitalRead(OUT_PIN);
        Serial.printf("[SENSOR LIVE] Clear:%u us | R:%u G:%u B:%u | r=%.2f g=%.2f b=%.2f | OUT Pin:%d | %s\n",
                      c.pulseClear, c.pulseR, c.pulseG, c.pulseB,
                      c.normR, c.normG, c.normB,
                      pinVal,
                      c.isDetected ? "TERDETEKSI ADA OBJEK" : "KOSONG/GELAP (0 pulsa)");
      }

#if CALIBRATION_MODE
      Serial.printf("[CALIB] R:%4u G:%4u B:%4u Clear:%4u us | Norm: r=%.2f g=%.2f b=%.2f | Disc: %s\n",
                    c.pulseR, c.pulseG, c.pulseB, c.pulseClear, c.normR, c.normG, c.normB,
                    c.isDetected ? "TERDETEKSI" : "KOSONG");
      return;
#endif

      // Deteksi kaset jika ada objek
      if (c.isDetected) {
        if (!isDiscPresent) {
          if (debounceStartTime == 0) {
            debounceStartTime = now;
            Serial.printf("\n[SENSOR] Kaset mulai masuk (Clear: %u us). Menunggu kaset pas (jeda 500ms)...\n", c.pulseClear);
          } else if (now - debounceStartTime >= 500) {
            // Ambil 3 sampel cepat (masing-masing jeda 10ms)
            float sumR = 0, sumG = 0, sumB = 0;
            int samples = 0;
            for (int i = 0; i < 3; i++) {
              ColorReading cr = readTCS3000();
              if (cr.isDetected) {
                sumR += cr.normR;
                sumG += cr.normG;
                sumB += cr.normB;
                samples++;
              }
              delay(10);
            }

            if (samples >= 1) {
              float avgR = sumR / samples;
              float avgG = sumG / samples;
              float avgB = sumB / samples;

              Serial.printf("[SENSOR VERIFIKASI] Rata-rata Warna: r=%.2f, g=%.2f, b=%.2f (dari %d sampel)\n",
                            avgR, avgG, avgB, samples);

              DetectedColor dc = classifyColor(avgR, avgG, avgB);
              String selectedTrack = "";

              if (dc == COLOR_RED) {
                selectedTrack = trackRed;
                Serial.println("\n************************************************************");
                Serial.printf("  [VERIFIKASI SUKSES] BENAR WARNA MERAH! (r=%.2f, g=%.2f, b=%.2f)\n", avgR, avgG, avgB);
                Serial.printf("  Target Lagu: %s\n", selectedTrack.c_str());
                Serial.println("************************************************************\n");
              } else if (dc == COLOR_YELLOW) {
                selectedTrack = trackYellow;
                Serial.println("\n************************************************************");
                Serial.printf("  [VERIFIKASI SUKSES] BENAR WARNA KUNING! (r=%.2f, g=%.2f, b=%.2f)\n", avgR, avgG, avgB);
                Serial.printf("  Target Lagu: %s\n", selectedTrack.c_str());
                Serial.println("************************************************************\n");
              } else if (dc == COLOR_BLUE) {
                selectedTrack = trackBlue;
                Serial.println("\n************************************************************");
                Serial.printf("  [VERIFIKASI SUKSES] BENAR WARNA BIRU! (r=%.2f, g=%.2f, b=%.2f)\n", avgR, avgG, avgB);
                Serial.printf("  Target Lagu: %s\n", selectedTrack.c_str());
                Serial.println("************************************************************\n");
              }

              if (selectedTrack.length() > 0) {
                debounceStartTime = 0;
                nextTrackToPlay = selectedTrack;
                Serial.println("[JUKEBOX] Memutar intro DVD (/dvd.mp3) terlebih dahulu sebelum lagu utama...");
                startTrackByPath("/dvd.mp3");
                isDiscPresent = true;
              } else {
                // Jika belum cocok warna (mungkin kaset didorong perlahan), beri waktu retry cepat
                if (now - debounceStartTime < 1500) {
                  debounceStartTime = now - 350; // Coba lagi 150ms berikutnya
                } else {
                  Serial.printf("[SENSOR] BUKAN KUNING / MERAH / BIRU (r=%.2f, g=%.2f, b=%.2f). Lagu tidak dinyalakan.\n", avgR, avgG, avgB);
                  debounceStartTime = 0;
                  isDiscPresent = true; // Kunci agar tidak spam log selagi kaset yang sama masih di dalam
                }
              }
            } else {
              debounceStartTime = now - 350;
            }
          }
        }
      } 
      // Jika kaset dilepas / slot kosong saat standby
      else {
        debounceStartTime = 0;
        if (isDiscPresent) {
          isDiscPresent = false;
          Serial.println("\n[SENSOR] Kaset dicabut dari slot saat standby! Memutar sound DVD eject...");
          startTrackByPath("/dvd.mp3");
          isDiscPresent = false;
        }
      }
    }
  }

  // 3. Pembacaan Serial Command dari Localhost Web Dashboard / Serial Monitor
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    if (cmd.length() > 0) {
      Serial.printf("[CMD] Diterima perintah: %s\n", cmd.c_str());
      if (cmd.equalsIgnoreCase("COLOR") || cmd.equalsIgnoreCase("C") || cmd.equalsIgnoreCase("RAW")) {
        ColorReading c = readTCS3000();
        DetectedColor dc = classifyColor(c.normR, c.normG, c.normB);
        const char* colorName = (dc == COLOR_RED) ? "MERAH" : (dc == COLOR_YELLOW) ? "KUNING" : (dc == COLOR_BLUE) ? "BIRU" : "NONE";
        Serial.printf("[RAW COLOR] Clear:%u us | R:%u G:%u B:%u | norm: r=%.2f g=%.2f b=%.2f | Detected:%d | Color:%s\n",
                      c.pulseClear, c.pulseR, c.pulseG, c.pulseB,
                      c.normR, c.normG, c.normB,
                      c.isDetected ? 1 : 0, colorName);
      } else if (cmd.equalsIgnoreCase("SCAN") || cmd.equalsIgnoreCase("SCANPINS")) {
        Serial.println("[SCAN] Memindai seluruh pin GPIO untuk mendeteksi sinyal pulsa dari sensor...");
        int testPins[] = {2, 4, 12, 13, 15, 16, 17, 18, 19, 21, 22, 23, 32, 33, 34, 35, 36, 39};
        bool foundAny = false;
        for (int pNum : testPins) {
          pinMode(pNum, INPUT_PULLUP);
          uint32_t p = pulseIn(pNum, LOW, 15000);
          if (p == 0) p = pulseIn(pNum, HIGH, 15000);
          int pinVal = digitalRead(pNum);
          if (p > 0) {
            Serial.printf("[SCAN FOUND] >>> GPIO %d MENERIMA PULSA: %u us (Level: %d) <<<\n", pNum, p, pinVal);
            foundAny = true;
          } else {
            Serial.printf("  GPIO %2d: Tidak ada pulsa (Level: %d)\n", pNum, pinVal);
          }
        }
        if (!foundAny) {
          Serial.println("[SCAN RESULT] Tidak ada GPIO yang menerima pulsa! Periksa VCC, GND, dan pastikan lampu LED di sensor menyala.");
        }
      } else if (cmd.equalsIgnoreCase("SCALE")) {
        static bool scale100 = false;
        scale100 = !scale100;
        digitalWrite(S0_PIN, HIGH);
        digitalWrite(S1_PIN, scale100 ? HIGH : LOW);
        Serial.printf("[CMD] Skala frekuensi sensor: %s\n", scale100 ? "100% (S0=H, S1=H)" : "20% (S0=H, S1=L)");
      } else if (cmd.equalsIgnoreCase("LED") || cmd.equalsIgnoreCase("TOGGLELED")) {
        static bool ledState = true;
        ledState = !ledState;
        digitalWrite(LED_PIN, ledState ? HIGH : LOW);
        Serial.printf("[CMD] LED_PIN (GPIO 33) disetel ke: %s\n", ledState ? "HIGH (3.3V)" : "LOW (0V / GND)");
      } else if (cmd.equalsIgnoreCase("PLAY") || cmd.equalsIgnoreCase("P")) {
        isDiscPresent = false; // Pemutaran manual via Web Dashboard
        startTrackByPath(trackYellow.c_str());
      } else if (cmd.startsWith("PLAY:") || cmd.startsWith("play:")) {
        isDiscPresent = false; // Pemutaran manual via Web Dashboard
        String track = cmd.substring(5);
        track.trim();
        startTrackByPath(track.c_str());
      } else if (cmd.equalsIgnoreCase("STOP") || cmd.equalsIgnoreCase("S")) {
        stopTrack();
      } else if (cmd.startsWith("VOL:") || cmd.startsWith("vol:")) {
        int volPercent = cmd.substring(4).toInt();
        currentVolume = constrain(volPercent, 0, 100);
        preferences.putInt("vol", currentVolume);
        int rawVol = (currentVolume * 127) / 100;
        a2dp_source.set_volume(rawVol);
        Serial.printf("[VOLUME] Volume disetel & disimpan ke NVS: %d%% (raw: %d)\n", currentVolume, rawVol);
      } else if (cmd.startsWith("SETCFG:") || cmd.startsWith("setcfg:")) {
        // Format: SETCFG:YELLOW:<lagu.mp3> atau SETCFG:RED:<lagu.mp3> atau SETCFG:BLUE:<lagu.mp3>
        String rest = cmd.substring(7);
        rest.trim();
        int colonIdx = rest.indexOf(':');
        if (colonIdx > 0) {
          String colorKey = rest.substring(0, colonIdx);
          colorKey.toUpperCase();
          String trackVal = rest.substring(colonIdx + 1);
          trackVal.trim();
          if (trackVal.startsWith("/")) trackVal = trackVal.substring(1);
          if (!trackVal.endsWith(".mp3")) trackVal += ".mp3";

          if (colorKey == "YELLOW" || colorKey == "KUNING") {
            trackYellow = trackVal;
            preferences.putString("t_yellow", trackYellow);
            Serial.printf("[CONFIG] Pemetaan KUNING disimpan ke NVS: %s\n", trackYellow.c_str());
          } else if (colorKey == "RED" || colorKey == "MERAH") {
            trackRed = trackVal;
            preferences.putString("t_red", trackRed);
            Serial.printf("[CONFIG] Pemetaan MERAH disimpan ke NVS: %s\n", trackRed.c_str());
          } else if (colorKey == "BLUE" || colorKey == "BIRU") {
            trackBlue = trackVal;
            preferences.putString("t_blue", trackBlue);
            Serial.printf("[CONFIG] Pemetaan BIRU disimpan ke NVS: %s\n", trackBlue.c_str());
          }
        }
      } else if (cmd.equalsIgnoreCase("GETCFG")) {
        Serial.printf("{\"vol\":%d,\"yellow\":\"%s\",\"red\":\"%s\",\"blue\":\"%s\"}\n",
                      currentVolume, trackYellow.c_str(), trackRed.c_str(), trackBlue.c_str());
      } else if (cmd.equalsIgnoreCase("STATUS")) {
        Serial.printf("{\"bt\":%d,\"state\":%d,\"track\":\"%s\",\"eof\":%d,\"vol\":%d,\"yellow\":\"%s\",\"red\":\"%s\",\"blue\":\"%s\"}\n",
                      isBtConnected ? 1 : 0, (int)currentState,
                      currentTrackFile ? currentTrackFile.name() : "NONE",
                      isTrackFileEof ? 1 : 0, currentVolume,
                      trackYellow.c_str(), trackRed.c_str(), trackBlue.c_str());
      }
    }
  }

  delay(10);
}
