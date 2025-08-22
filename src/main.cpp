// --- M5Stack/M5StampS3関連 ---
//#include <M5Unified.h>
#include "KANTANMusic.h"
// FreeRTOS (ESP32)
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/timers.h"

// --- unit_byte I2C関連 ---
#include <Wire.h>
#include "unit_byte.hpp"

// --- LEDアレイ関連 ---
#include <FastLED.h>

// unit_byte I2Cアドレスとピン設定
#define SCL_PIN 15             // I2C SCLピン番号
#define SDA_PIN 13             // I2C SDAピン番号
UnitByte buttonDevice1; // UnitByteデバイス(表面)
UnitByte buttonDevice2; // UnitByteデバイス(裏面)
#define BUTTON_DEVICE_1_ADDR 0x53
#define BUTTON_DEVICE_2_ADDR 0x4F

// LED設定
#define LED_PIN 7              // 外部LEDアレイ用
#define NUM_LEDS 14
#define LED_BRIGHTNESS 100
// LEDアレイ
CRGB leds[NUM_LEDS];

// WS2812 LED設定（G21）
#define WS2812_LED_PIN 21      // G21のWS2812 LEDストリップ
#define WS2812_NUM_LEDS 1      // LEDの個数（適宜変更）
CRGB ws2812_leds[WS2812_NUM_LEDS];

// ボタン状態管理
bool prevButtonStates1[8] = {false}; // 前回のボタン状態（デバイス1）
bool prevButtonStates2[8] = {false}; // 前回のボタン状態（デバイス2）

// デバウンス設定
#define DEBOUNCE_TIME_MS 20  // デバウンス時間（ミリ秒）
unsigned long lastButtonTime1[8] = {0}; // 最後にボタン状態が変化した時刻（デバイス1）
unsigned long lastButtonTime2[8] = {0}; // 最後にボタン状態が変化した時刻（デバイス2）
bool rawButtonStates1[8] = {false}; // 生のボタン状態（デバイス1）
bool rawButtonStates2[8] = {false}; // 生のボタン状態（デバイス2）
bool stableButtonStates1[8] = {false}; // 安定したボタン状態（デバイス1）
bool stableButtonStates2[8] = {false}; // 安定したボタン状態（デバイス2）

// ボタン状態の定義
enum ButtonState {
  BUTTON_RELEASED = 0,    // 押されてない
  BUTTON_PRESSED = 1,     // 押された（今回押された瞬間）
  BUTTON_HELD = 2,        // 押しっぱなし
  BUTTON_RELEASED_NOW = 3 // 離された（今回離された瞬間）
};

// --- Forward Declarations (arpeggioTaskで使用するため) ---
void sendControlChange(uint8_t channel, uint8_t control, uint8_t value);
void sendProgramChange(uint8_t channel, uint8_t program);
void sendMidiNoteOn(uint8_t channel, uint8_t note, uint8_t velocity);
void sendMidiNoteOff(uint8_t channel, uint8_t note, uint8_t velocity);
void updateByteButtonLEDs();
void updateByteButton2LEDs();
void updateRandomLEDs();

// M5StampS3のGROVE端子ピン配置を確認
#define GROVE_TX 2   // Grove TX (白線)
#define GROVE_RX 1   // Grove RX (黄線)

// アナログデバイス設定
#define ANALOG_SLIDER_PIN 9
#define BATTERY_ADC_PIN 8

// バッテリー監視設定
#define BATTERY_SAMPLES 10           // 移動平均のサンプル数
#define BATTERY_UPDATE_INTERVAL 30000 // LED更新間隔（30秒）
static float batteryVoltageSamples[BATTERY_SAMPLES] = {0.0};
static int batterySampleIndex = 0;
static unsigned long lastBatteryLEDUpdate = 0;
static float displayedBatteryVoltage = 4.2; // 表示用バッテリー電圧
static bool batteryInitialized = false; // 初期化フラグ

// --- MIDIコントローラー状態変数 ---
bool noteOnSent = false; // ノートオンが送信済みかを記録
int currentKey = 0;     // 現在のキー (0: C/Am)
int currentDegree = 1;  // 現在のコード (1: I)
int currentSemitone = 0; // 現在の半音(-1:flat)
bool currentSwap = false; // 現在のスワップ状態
KANTANMusic_Modifier currentModifier = KANTANMusic_Modifier_None;
KANTANMusic_GetMidiNoteNumberOptions options; // KANTAN Music APIのオプション設定

int pastKey = 0; // 直前のキー（押されたキー）
int pastDegree = 1; // 直前のコード（押されたコード番号）
KANTANMusic_GetMidiNoteNumberOptions pastOptions; // 直前のオプション設定

int currentNoteTone = 0;
int reserveNoteTone = 0;

int strokeTime = 5;


// Analog GPIO6 → noteTone の6分割マッピング
// 要件: 0～5の6値に分割し、外側(0と5)の帯域を内側(1～4)の1.5倍に設定
// 総レンジ4096(0～4095)を 1.5w + 4w + 1.5w = 7w で分割
// w ≒ 4096/7 ≒ 585、外側帯域 ≒ 878 とし、以下のしきい値を採用
const int NOTE_TONE_EDGE0 = 3218; // >= で 0  (上側878幅)
const int NOTE_TONE_EDGE1 = 2633; // >= で 1  (585幅)
const int NOTE_TONE_EDGE2 = 2048; // >= で 2  (585幅)
const int NOTE_TONE_EDGE3 = 1463; // >= で 3  (585幅)
const int NOTE_TONE_EDGE4 = 878;  // >= で 4  (585幅) → 未満は5(下側878幅)

const int HYSTERESIS = 32; // 例: 32以上変化したら更新
static int lastAnalogValue = 0;

// --- 非同期アルペジオ（FreeRTOSタスク） ---
typedef struct {
  int degree;
  int key;
  KANTANMusic_GetMidiNoteNumberOptions options; // オプションはスナップショット
  bool fourVoice;                                // 4音色かどうか
  int strokeTimeMs;                              // ms単位
  uint32_t cancelId;                             // キャンセル用世代ID
} ArpJob;

static QueueHandle_t g_arpQueue = nullptr;
static TaskHandle_t g_arpTask = nullptr;
static volatile uint32_t g_arpCancelId = 0;

// --- Note Off を遅延送出するワンショットタイマー ---
typedef struct {
  uint8_t channel;
  uint8_t note;
  uint32_t cancelId; // スケジュール時の世代ID（現在と一致する場合のみ送出）
} NoteOffEvent;

static void noteOffTimerCallback(TimerHandle_t xTimer) {
  NoteOffEvent* ev = (NoteOffEvent*) pvTimerGetTimerID(xTimer);
  if (ev) {
    if (ev->cancelId == g_arpCancelId && ev->note != 0) {
      sendMidiNoteOff(ev->channel, ev->note, 0);
    }
  }
}

// ピッチ固定（6本）のタイマーとイベント領域
static TimerHandle_t g_noteOffTimers[6] = { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };
static NoteOffEvent g_noteOffEvents[6];

static void arpeggioTask(void* pv) {
  ArpJob job;
  for (;;) {
    if (xQueueReceive(g_arpQueue, &job, portMAX_DELAY) == pdTRUE) {
      for (int pitch = 1; pitch <= 6; ++pitch) {
        if (job.cancelId != g_arpCancelId) break; // 新規ジョブでキャンセル
        if (job.fourVoice && (pitch == 2 || pitch == 3)) continue; // 4音色の場合は2,3はスキップ
        uint8_t note = KANTANMusic_GetMidiNoteNumber(pitch, job.degree, job.key, &job.options);
        if (note != 0) {
          sendMidiNoteOn(1, note, 127);
          // ピッチ固定タイマーを再設定
          int idx = pitch - 1; // 0..5
          if (idx >= 0 && idx < 6 && g_noteOffTimers[idx]) {
            g_noteOffEvents[idx].channel = 1;
            g_noteOffEvents[idx].note = note;
            g_noteOffEvents[idx].cancelId = job.cancelId;
            xTimerStop(g_noteOffTimers[idx], 0);
            // ChangePeriodは停止中でも有効で、その後タイマーを再始動する
            xTimerChangePeriod(g_noteOffTimers[idx], pdMS_TO_TICKS(6000), 0);
          }
        }
        vTaskDelay(pdMS_TO_TICKS(job.strokeTimeMs));
      }
      // 必要ならここで flush してもよい（現状はkantanPlay側方針を維持）
    }
  }
}


// キー名の表示
const char* keyNames[] = {"C", "Db", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"};
// コード番号の表示（ローマ数字風）
const char* degreeNames[] = {"", "I", "II", "III", "IV", "V", "VI", "VII"};

struct GMProgram {
    int program_number;     // MIDI用（0〜127）
    const char name[11];    // 10文字 + 終端null = 11
    KANTANMusic_Voicing voicing;
    bool fourVoice;
};

// 6音色の定義（名称は手動で10文字以内に短縮済）
const GMProgram gm_programs[6] = {
    { 25,  "SteelGtr",    KANTANMusic_Voicing::KANTANMusic_Voicing_Guitar ,false},  // Steel String Guitar (GM#26)
    { 26,  "JazzGtr",     KANTANMusic_Voicing::KANTANMusic_Voicing_Guitar ,false},  // Jazz Guitar (GM#27)
    {  0,  "Piano",       KANTANMusic_Voicing::KANTANMusic_Voicing_Static ,false},  // Acoustic Grand Piano (GM#1)
    {  4,  "EPiano1",     KANTANMusic_Voicing::KANTANMusic_Voicing_Static ,false},  // Electric Piano 1 (GM#5)
    { 19,  "ChurchOrg",   KANTANMusic_Voicing::KANTANMusic_Voicing_Static ,true},  // Church Organ (GM#20)
    { 48,  "Strings",     KANTANMusic_Voicing::KANTANMusic_Voicing_Static ,true}   // String Ensemble 1 (GM#49)

};


// --- MIDI Control Change送信用関数 ---
// 指定チャンネル・コントロール番号・値でMIDI Control Changeメッセージを送信
void sendControlChange(uint8_t channel, uint8_t control, uint8_t value) {
  uint8_t msg[3];
  msg[0] = 0xB0 | ((channel - 1) & 0x0F); // Control Change, channel
  msg[1] = control & 0x7F;
  msg[2] = value & 0x7F;
  Serial1.write(msg, 3);
}
// プログラムチェンジ送信用関数
void sendProgramChange(uint8_t channel, uint8_t program) {
  uint8_t msg[2];
  msg[0] = 0xC0 | ((channel - 1) & 0x0F); // Program Change, channel
  msg[1] = program & 0x7F;
  Serial1.write(msg, 2);
}
// 指定チャンネル・ノート番号・ベロシティでMIDI Note Onメッセージを送信
void sendMidiNoteOn(uint8_t channel, uint8_t note, uint8_t velocity) {
  if (note == 0) return; // ミュート音（0）は送信しない
  Serial.printf("Sending MIDI Note On: Ch=%d, Note=%d, Vel=%d\n", channel, note, velocity);
  uint8_t msg[3];
  msg[0] = 0x90 | ((channel - 1) & 0x0F); // Note On, channel
  msg[1] = note & 0x7F;
  msg[2] = velocity & 0x7F;
  Serial1.write(msg, 3);
}

// 指定チャンネル・ノート番号・ベロシティでMIDI Note Offメッセージを送信
void sendMidiNoteOff(uint8_t channel, uint8_t note, uint8_t velocity) {
  if (note == 0) return; // ミュート音（0）は送信しない
  Serial.printf("Sending MIDI Note Off: Ch=%d, Note=%d, Vel=%d\n", channel, note, velocity);
  uint8_t msg[3];
  msg[0] = 0x80 | ((channel - 1) & 0x0F); // Note Off, channel
  msg[1] = note & 0x7F;
  msg[2] = velocity & 0x7F;
  Serial1.write(msg, 3);
}



void kantanNoteOn(int degree, int Key, KANTANMusic_GetMidiNoteNumberOptions* options, bool fourVoice){
  // 非同期実行: ジョブをキューに投入して即時return
  if (!g_arpQueue) return;

  // 進行中のジョブをキャンセル（世代IDを更新）
  uint32_t newId = g_arpCancelId + 1;
  g_arpCancelId = newId;

  ArpJob job;
  job.degree = degree;
  job.key = Key;
  job.fourVoice = fourVoice;           // 4音色かどうか
  job.options = *options;           // オプションをスナップショット
  job.strokeTimeMs = strokeTime;    // 現在のストローク時間をスナップショット
  job.cancelId = newId;

  // 古い未処理ジョブは破棄して最新のみを送る
  xQueueReset(g_arpQueue);
  xQueueSend(g_arpQueue, &job, 0);
}

void kantanNoteOff(int degree, int Key, KANTANMusic_GetMidiNoteNumberOptions* options){
    Serial.println("Button A released - stopping chord");
    // 全ての音をオフ
    for (int pitch = 1; pitch <= 6; pitch++) {
      uint8_t note = KANTANMusic_GetMidiNoteNumber(pitch, degree, Key, options);
      if (note != 0) {
        sendMidiNoteOff(1, note, 0);
      }
    }
}

void allNotesOff() {
  // 全ノートオフ（MIDI CC#123: All Notes Off）
  sendControlChange(1, 123, 0);
}

// ByteButton1のLED制御
void updateByteButtonLEDs() {
  // currentKeyに基づいてLEDを設定
  // currentKey: 0,1,2,3,4,5,6,7,8,9,10,11
  // LED点灯:    7,6,6,5,5,4,3,3,2,2,1,1
  const int keyToLED[12] = {7, 6, 6, 5, 5, 4, 3, 3, 2, 2, 1, 1};
  
  // 全LEDを初期化（消灯）
  for (int i = 0; i < 8; i++) {
    buttonDevice1.setRGB888(i, 0x000000); // 消灯
  }
  
  // currentKeyに対応するLEDを点灯
  int ledToLight = keyToLED[currentKey % 12];
  if (ledToLight >= 1 && ledToLight <= 7) {
    if (currentSwap) {
      // swapがtrueの場合、元のLEDは緑のまま、他のLEDを赤に点灯
      buttonDevice1.setRGB888(ledToLight, 0x00FF00); // 元のLEDは緑色のまま
      for (int i = 1; i <= 7; i++) {
        if (i != ledToLight) {
          buttonDevice1.setRGB888(i, 0xFF0000); // 他のLEDは赤色
        }
      }
    } else {
      // swapがfalseの場合、対応するLEDのみ点灯
      buttonDevice1.setRGB888(ledToLight, 0x00FF00); // 緑色
    }
  }
  
  // LED0の制御（currentKeyが1,3,6,8,10の時に黄色）
  if (currentKey == 1 || currentKey == 3 || currentKey == 6 || currentKey == 8 || currentKey == 10) {
    buttonDevice1.setRGB888(0, 0xFFFF00); // 黄色
  } else {
    buttonDevice1.setRGB888(0, 0x000000); // 消灯
  }
}

// ByteButton2のLED制御
void updateByteButton2LEDs() {
  // 全LEDを初期化（消灯）
  for (int i = 0; i < 8; i++) {
    buttonDevice2.setRGB888(i, 0x000000); // 消灯
  }
  
  // reserveNoteTone (0-5) に対応するLEDを青色で点灯
  if (reserveNoteTone >= 0 && reserveNoteTone <= 5) {
    buttonDevice2.setRGB888(reserveNoteTone, 0x0000FF); // 青色
  }
  
  // LED6の制御（currentSemitoneに基づく）
  if (currentSemitone == -1) {
    buttonDevice2.setRGB888(6, 0xFFFF00); // 黄色
  } else if (currentSemitone == 0) {
    buttonDevice2.setRGB888(6, 0x000000); // 消灯
  } else if (currentSemitone == 1) {
    buttonDevice2.setRGB888(6, 0xFF0000); // 赤色
  }
  
  // LED7の制御（currentModifierに基づく）
  if (currentModifier == KANTANMusic_Modifier_None) {
    buttonDevice2.setRGB888(7, 0x000000); // 消灯
  } else {
    buttonDevice2.setRGB888(7, 0x00FF00); // 緑色
  }
}

// 7個のLEDをランダムカラーで点灯（0-6）
void updateRandomLEDs() {
  for (int i = 0; i < 7; i++) {
    // ランダムな色相（0-255）、高い彩度（255）、適度な明度（150）
    leds[i] = CHSV(random(256), 255, 150);
  }
  FastLED.show();
}

// 7個のLEDを消灯（0-6）
void clearRandomLEDs() {
  for (int i = 0; i < 7; i++) {
    leds[i] = CRGB::Black;
  }
  FastLED.show();
}

// バッテリーインジケータ（7-13）
void updateBatteryLEDs() {
  // G8からバッテリー電圧を読み取り
  int batteryValue = analogRead(BATTERY_ADC_PIN);
  float Vfs_cal = 2.3096859379622803; // キャリブ済み定数

  // 1) ADC → 電圧
  float voltage = (batteryValue / 4095.0) * Vfs_cal / (51.0 / (100.0 + 51.0)); // V

  // バッテリー電圧をシリアルに出力（高頻度）
  Serial.printf("Battery ADC: %d, voltage: %.2fV\n", batteryValue, voltage);

  // 初回の場合は全サンプルを現在の電圧で初期化
  if (!batteryInitialized) {
    for (int i = 0; i < BATTERY_SAMPLES; i++) {
      batteryVoltageSamples[i] = voltage;
    }
    batteryInitialized = true;
    displayedBatteryVoltage = voltage; // 初回は即座に表示更新
    lastBatteryLEDUpdate = millis();
    Serial.printf("Battery initialized with %.2fV\n", voltage);
  } else {
    // 移動平均に追加
    batteryVoltageSamples[batterySampleIndex] = voltage;
    batterySampleIndex = (batterySampleIndex + 1) % BATTERY_SAMPLES;
    
    // 移動平均を計算
    float sum = 0.0;
    for (int i = 0; i < BATTERY_SAMPLES; i++) {
      sum += batteryVoltageSamples[i];
    }
    float averageVoltage = sum / BATTERY_SAMPLES;
    
    // 30秒間隔でLED表示を更新
    unsigned long currentTime = millis();
    if (currentTime - lastBatteryLEDUpdate >= BATTERY_UPDATE_INTERVAL) {
      displayedBatteryVoltage = averageVoltage;
      lastBatteryLEDUpdate = currentTime;
      Serial.printf("LED Battery Update: %.2fV (avg)\n", displayedBatteryVoltage);
    }
  }

  // バッテリー容量（7-13）のLEDを消灯
  for (int i = 7; i < NUM_LEDS; i++) {
    leds[i] = CRGB::Black;
  }
 
  // 表示用バッテリー電圧に応じてLED7-13のどれか一つを点灯
  int ledIndex = -1;
  CRGB ledColor = CRGB::Green;
  
  if (displayedBatteryVoltage >= 4.05) {
    ledIndex = 13; // 4.05V以上
  } else if (displayedBatteryVoltage >= 3.90) {
    ledIndex = 12; // 3.90～4.05V
  } else if (displayedBatteryVoltage >= 3.75) {
    ledIndex = 11; // 3.75～3.90V
  } else if (displayedBatteryVoltage >= 3.60) {
    ledIndex = 10; // 3.60～3.75V
  } else if (displayedBatteryVoltage >= 3.45) {
    ledIndex = 9;  // 3.45～3.60V
  } else if (displayedBatteryVoltage >= 3.30) {
    ledIndex = 8;  // 3.30～3.45V
  } else if (displayedBatteryVoltage >= 3.15) {
    ledIndex = 7;  // 3.15～3.30V
  } else if (displayedBatteryVoltage >= 3.0) {
    ledIndex = 7;  // 3.0～3.15V
    ledColor = CRGB::Red; // 赤色で警告
  } else {
    // 3.0V以下は強制シャットダウン（deep sleep）
    Serial.println("Critical battery level! Entering deep sleep...");
    
    // 全ての音をオフ
    allNotesOff();
    Serial1.flush();
    
    // LEDを全消灯
    for (int i = 0; i < NUM_LEDS; i++) {
      leds[i] = CRGB::Black;
    }
    FastLED.show();
    
    // ByteButtonのLEDも消灯
    for (int i = 0; i < 8; i++) {
      buttonDevice1.setRGB888(i, 0x000000);
      buttonDevice2.setRGB888(i, 0x000000);
    }
    
    // WS2812も消灯
    ws2812_leds[0] = CRGB::Black;
    FastLED.show();
    
    delay(1000); // シャットダウンメッセージ表示時間
    
    // Deep sleepに入る（電源ボタンや外部割り込みで復帰可能）
    esp_deep_sleep_start();
  }
  
  if (ledIndex >= 0) {
    leds[ledIndex] = ledColor;
  }
  
  FastLED.show();
}

void kantanPlay(){
  // 直前のコードをオフ
  kantanNoteOff(pastDegree, pastKey, &pastOptions);

  KANTANMusic_GetMidiNoteNumber_SetDefaultOptions(&options);
  options.voicing = gm_programs[currentNoteTone].voicing; // Voicingを設定
  options.semitone_shift = currentSemitone; // 半音シフトを適用
  options.minor_swap = currentSwap; // マイナー・スワップを適用
  options.modifier = currentModifier; // モディファイアを適用
  kantanNoteOn(currentDegree, currentKey, &options,
               gm_programs[currentNoteTone].fourVoice); // 4音色かどうかを指定

  // コード演奏開始時にランダムLED点灯
  updateRandomLEDs();

  pastDegree = currentDegree; // 現在のコードを記録
  pastKey = currentKey;       // 現在のキーを記録
  pastOptions = options;     // 現在のオプションを記録

  // まとめて送ったMIDIをここで一度だけフラッシュしてレイテンシを抑える
  Serial1.flush();
}

// 初期化処理
void setup() {
  //auto cfg = M5.config();
  //M5.begin(cfg);
  Serial.begin(115200); 
  
  // USB CDCが準備できるまで待機（最大3秒でタイムアウト）
  unsigned long start = millis();
  while (!Serial && (millis() - start < 3000)) {
    delay(10);
  }
  
  delay(500); // 安定化のための待機

  // Grove端子用のSerial1を初期化する前にピンの確認
  Serial.println("M5StampS3 KANTAN Music MIDI Controller Start!");
  Serial.printf("GROVE TX Pin: %d\n", GROVE_TX);
  Serial.printf("GROVE RX Pin: %d\n", GROVE_RX);

  // アナログピン初期化
  pinMode(ANALOG_SLIDER_PIN, INPUT);
  pinMode(BATTERY_ADC_PIN, INPUT);

  // MIDI用UART1初期化（MIDI標準ボーレート）
  Serial1.begin(31250, SERIAL_8N1, GROVE_RX, GROVE_TX);
  Serial.println("Serial1 initialized for MIDI");

   // M5Unit-ByteButtonI2C初期化（SDA/SCLピン指定）
  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(100000); // 100kHz

  // M5Unit-ByteButton初期化
  bool device1_ok = buttonDevice1.begin(&Wire, BUTTON_DEVICE_1_ADDR, SDA_PIN, SCL_PIN, 100000);
  bool device2_ok = buttonDevice2.begin(&Wire, BUTTON_DEVICE_2_ADDR, SDA_PIN, SCL_PIN, 100000);

  // FastLED初期化（G7のLEDアレイ）
  FastLED.addLeds<NEOPIXEL, LED_PIN>(leds, NUM_LEDS);
  FastLED.setBrightness(LED_BRIGHTNESS);
  FastLED.clear();
  FastLED.show();

  // WS2812初期化（G21）
  FastLED.addLeds<WS2812, WS2812_LED_PIN, GRB>(ws2812_leds, WS2812_NUM_LEDS);
  
  // 起動時G21のWS2812テスト
  Serial.println("Starting G21 WS2812 LED test...");
  ws2812_leds[0] = CRGB::Red;
  FastLED.show();
  delay(500);
  ws2812_leds[0] = CRGB::Green;
  FastLED.show();
  delay(500);
  ws2812_leds[0] = CRGB::Blue;
  FastLED.show();
  delay(500);
  ws2812_leds[0] = CRGB::Black;
  FastLED.show();
  Serial.println("G21 WS2812 LED test complete.");

  // --- 起動時にMIDIへマスターボリュームと全チャンネルボリューム最大を送信 ---
  // マスターボリュームRPN (RPN 7 = Master Volume)
  sendControlChange(1, 99, 55); // RPN MSB (99) = 55
  sendControlChange(1, 98, 7);  // RPN LSB (98) = 7 (Master Volume)
  sendControlChange(1, 6, 100); // Data Entry MSB (6) = 100 (最大127)

  // アコースティックギター（例: プログラム番号26）に設定
  sendProgramChange(1, gm_programs[currentNoteTone].program_number); // プログラム番号26（0始まりなので25）
  // 各チャンネルのボリューム最大化
  for (uint8_t ch = 1; ch <= 16; ++ch) {
    sendControlChange(ch, 7, 127); // Channel Volume (CC#7)
  }

  // 非同期アルペジオの初期化
  g_arpQueue = xQueueCreate(2, sizeof(ArpJob));
  if (g_arpQueue) {
    xTaskCreate(
      arpeggioTask,
      "arpTask",
      4096,      // stack size
      nullptr,
      1,         // priority（低め）
      &g_arpTask
    );
  }

  // ピッチ固定のNote Offタイマーを生成（6本）
  for (int i = 0; i < 6; ++i) {
    g_noteOffEvents[i] = {1, 0, g_arpCancelId};
    g_noteOffTimers[i] = xTimerCreate("noteOffP", pdMS_TO_TICKS(6000), pdFALSE, &g_noteOffEvents[i], noteOffTimerCallback);
  }

  // 起動時初期メッセージ送出の完了を待つ（以後は逐次flushしない）
  Serial1.flush();
}

// メインループ処理
void loop() {
  //M5.update();

  // アナログ入力更新
  bool isAnalogChanged = false;

  int analogValue = analogRead(ANALOG_SLIDER_PIN); // GPIO8のアナログ値を取得
  if (abs(analogValue - lastAnalogValue) > HYSTERESIS) {
    lastAnalogValue = analogValue;
    strokeTime = map(analogValue, 0, 4095, 5, 125); // 0～4095を5～125にマッピング
  }

  uint8_t buttonStatus1 = buttonDevice1.getSwitchStatus();
  uint8_t buttonStatus2 = buttonDevice2.getSwitchStatus();
  unsigned long currentTime = millis();

  // 生のボタン状態を取得（ライブラリでは0がON、1がOFF - 論理反転）
  for (int i = 0; i < 8; i++) {
    bool rawState1 = !(buttonStatus1 & (1 << i)); // ビット反転
    bool rawState2 = !(buttonStatus2 & (1 << i)); // ビット反転

    // デバイス1のデバウンス処理
    if (rawState1 != rawButtonStates1[i]) {
      // 状態が変化した場合、時刻を記録
      lastButtonTime1[i] = currentTime;
      rawButtonStates1[i] = rawState1;
    } else if (currentTime - lastButtonTime1[i] >= DEBOUNCE_TIME_MS) {
      // 一定時間同じ状態が続いた場合、安定した状態として認識
      stableButtonStates1[i] = rawState1;
    }

    // デバイス2のデバウンス処理
    if (rawState2 != rawButtonStates2[i]) {
      // 状態が変化した場合、時刻を記録
      lastButtonTime2[i] = currentTime;
      rawButtonStates2[i] = rawState2;
    } else if (currentTime - lastButtonTime2[i] >= DEBOUNCE_TIME_MS) {
      // 一定時間同じ状態が続いた場合、安定した状態として認識
      stableButtonStates2[i] = rawState2;
    }
  }

  // 各ボタンの状態を判定（デバイス1）
  ButtonState buttonStates1Enum[8];
  for (int i = 0; i < 8; i++) {
    if (stableButtonStates1[i] && !prevButtonStates1[i]) {
      buttonStates1Enum[i] = BUTTON_PRESSED;     // 押された瞬間
    } else if (stableButtonStates1[i] && prevButtonStates1[i]) {
      buttonStates1Enum[i] = BUTTON_HELD;        // 押しっぱなし
    } else if (!stableButtonStates1[i] && prevButtonStates1[i]) {
      buttonStates1Enum[i] = BUTTON_RELEASED_NOW; // 離された瞬間
    } else {
      buttonStates1Enum[i] = BUTTON_RELEASED;    // 押されてない
    }
    prevButtonStates1[i] = stableButtonStates1[i]; // 状態を更新
  }

  // 各ボタンの状態を判定（デバイス2）
  ButtonState buttonStates2Enum[8];
  for (int i = 0; i < 8; i++) {
    if (stableButtonStates2[i] && !prevButtonStates2[i]) {
      buttonStates2Enum[i] = BUTTON_PRESSED;     // 押された瞬間
    } else if (stableButtonStates2[i] && prevButtonStates2[i]) {
      buttonStates2Enum[i] = BUTTON_HELD;        // 押しっぱなし
    } else if (!stableButtonStates2[i] && prevButtonStates2[i]) {
      buttonStates2Enum[i] = BUTTON_RELEASED_NOW; // 離された瞬間
    } else {
      buttonStates2Enum[i] = BUTTON_RELEASED;    // 押されてない
    }
    prevButtonStates2[i] = stableButtonStates2[i]; // 状態を更新
  }

  // ByteButton1(Front)
  // 7:I 6:II 5:III 4:IV 3:V 2:VI 1:VII 0:Swap
  // ByteButton2(Back)
  // 0:7th 1:flat 2:Dim 3:sus4 4:PC- 5:PC+ 6:Key- 7:Key+
  // いったん同時押しは考えない
  bool noteOn = false;
  bool partSemtone = false;
  if(buttonStates1Enum[7] == BUTTON_PRESSED) {
    currentDegree = 1;
    noteOn = true;
  }
  if(buttonStates1Enum[6] == BUTTON_PRESSED) {
    currentDegree = 2;
    noteOn = true;
  }
  if(buttonStates1Enum[5] == BUTTON_PRESSED) {
    currentDegree = 3;
    noteOn = true;
  }
  if(buttonStates1Enum[4] == BUTTON_PRESSED) {
    currentDegree = 4;
    noteOn = true;
  }
  if(buttonStates1Enum[3] == BUTTON_PRESSED) {
    currentDegree = 5;
    noteOn = true;
  }
  if(buttonStates1Enum[2] == BUTTON_PRESSED) {
    currentDegree = 6;
    noteOn = true;
  }
  if(buttonStates1Enum[1] == BUTTON_PRESSED) {
    currentDegree = 7;
    noteOn = true;
  }

  // ボタン7～1が離された時の処理
  bool noteOff = false;
  if(buttonStates1Enum[7] == BUTTON_RELEASED_NOW ||
     buttonStates1Enum[6] == BUTTON_RELEASED_NOW ||
     buttonStates1Enum[5] == BUTTON_RELEASED_NOW ||
     buttonStates1Enum[4] == BUTTON_RELEASED_NOW ||
     buttonStates1Enum[3] == BUTTON_RELEASED_NOW ||
     buttonStates1Enum[2] == BUTTON_RELEASED_NOW ||
     buttonStates1Enum[1] == BUTTON_RELEASED_NOW) {
    noteOff = true;
  }

  if(buttonStates1Enum[0] == BUTTON_HELD) {
    currentSwap = true;
  }else{
    currentSwap = false; 
  }

  if(buttonStates2Enum[1] == BUTTON_HELD) {
    currentSemitone = -1;
  }else{
    currentSemitone = 0;
  }

  currentModifier = KANTANMusic_Modifier_None;
  if(buttonStates2Enum[0] == BUTTON_HELD) {
    if(buttonStates2Enum[3] == BUTTON_HELD){
      currentModifier = KANTANMusic_Modifier_7sus4;
    }else if(buttonStates2Enum[2] == BUTTON_HELD) {
      currentModifier = KANTANMusic_Modifier_dim7;
    }else{
      currentModifier = KANTANMusic_Modifier_7;
    }
  }else{
    if(buttonStates2Enum[3] == BUTTON_HELD) {
      currentModifier = KANTANMusic_Modifier_sus4;
    } else if(buttonStates2Enum[2] == BUTTON_HELD) {
      currentModifier = KANTANMusic_Modifier_dim;
    }
  }

  if(buttonStates2Enum[4] == BUTTON_PRESSED) {
    reserveNoteTone = (reserveNoteTone - 1 + 6) % 6;
  }

  if(buttonStates2Enum[5] == BUTTON_PRESSED) {
    reserveNoteTone = (reserveNoteTone + 1) % 6;
  }

  if(buttonStates2Enum[6] == BUTTON_PRESSED) {
    currentKey = (currentKey - 1 + 12) % 12;
  }

  if(buttonStates2Enum[7] == BUTTON_PRESSED) {
    currentKey = (currentKey + 1) % 12;
  }
  
  if (noteOn) {
    // 音色変更時はオールノートオフ呼びたいので。(キーが押されるまでは変えない)
    if (reserveNoteTone != currentNoteTone) {
      allNotesOff();
      currentNoteTone = reserveNoteTone;
      sendProgramChange(1, gm_programs[currentNoteTone].program_number);
    } 
    // コード演奏
    kantanPlay();
  }
  
  // ボタンが離された時にLEDを消灯
  if (noteOff) {
    clearRandomLEDs();
  }
  
  // ByteButton1とByteButton2のLEDを更新
  updateByteButtonLEDs();
  updateByteButton2LEDs();
  
  // バッテリーインジケータを更新
  updateBatteryLEDs();
  
  delay(10); // ループ間隔
}
