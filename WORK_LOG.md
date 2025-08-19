# kantan_mini 作業ログ

## 2025年8月3日 - KANTANMusic API組み込み完了 🎵

### 完了した作業

#### 1. M5Unified移行
- **変更前**: `M5AtomS3.h` + `FastLED` + `M5AtomS3`ライブラリ依存
- **変更後**: `M5Unified.h`のみ
- **利点**: 
  - 統一されたAPI (`M5.Display`)
  - 軽量化 (Flash: 15.9% → 15.1%)
  - 将来性 (M5Stack推奨ライブラリ)
  - 他のM5デバイスとの互換性

#### 2. 依存関係最適化
- **削除**: `M5AtomS3`ライブラリ (M5Unifiedで代替)
- **削除**: `FastLED`ライブラリ (M5AtomS3内部依存のみで、M5Unifiedでは不要)
- **結果**: よりクリーンな`platformio.ini`

#### 3. KANTANMusic API組み込み
- **ライブラリ**: `kantan-music/` フォルダ内のバイナリライブラリ
- **ライセンス**: KANTAN Music API License (個人・試作用途で無料)
- **機能**: 音楽理論に基づく正確なコード生成
- **ヘッダー**: `KANTANMusic.h`
- **バイナリ**: `esp32/libkantan-music.a`

#### 4. MIDIコントローラー機能進化
- **変更前**: 単純なC4ノート（60番）のOn/Off
- **変更後**: 音楽理論対応コード演奏システム
- **現在の機能**:
  - Cメジャーキー Iコード（C-E-G-C）
  - アルペジオ演奏（50ms間隔）
  - クローズドボイシング採用
  - ディスプレイ表示（キー・コード情報）

### 現在の技術仕様

#### ハードウェア
- **デバイス**: M5AtomS3 (ESP32-S3)
- **MIDI出力**: GROVE端子 (G2=TX, G1=RX)
- **ボーレート**: 31250 (MIDI標準)
- **ディスプレイ**: 128x128 LCD
- **入力**: ボタンA

#### ソフトウェア
- **フレームワーク**: Arduino + PlatformIO
- **ライブラリ**: M5Unified + KANTANMusic API
- **メモリ使用量**: RAM 7.5%, Flash 15.4%
- **ファームウェアサイズ**: 513KB

#### 音楽機能
- **現在のキー**: C (0)
- **現在のコード**: I (1) - Cメジャーコード
- **ボイシング**: Close (ピアノ風)
- **演奏方法**: アルペジオ（順次発音）
- **構成音**: pitch 1-4 (C-E-G-C)

### コード構造

```cpp
// 主要変数
int currentKey = 0;     // 0: C/Am
int currentDegree = 1;  // 1: I (主和音)
bool noteOnSent = false;

// MIDI送信
void sendMidiNoteOn(channel, note, velocity)
void sendMidiNoteOff(channel, note, velocity)

// 表示
void displayChordInfo() // キー・コード情報表示

// メインループ
// ボタン押下 → アルペジオ演奏
// ボタン離し → 全音停止
```

### Git履歴
```
61ec7a7 KANTANMusic API組み込み完了🎵
5f9f0b1 M5Unified移行完了: 不要な依存関係削除  
f99f163 FastLED依存を削除（未使用のため）
```

### 次回作業予定

#### 優先度高
1. **キー変更機能**: ボタン長押し等でC→D→E...と循環
2. **コード進行**: I→IV→V→I等の定番進行
3. **実機テスト**: MIDIユニット接続での音出し確認

#### 優先度中
4. **ボイシング変更**: Guitar, Ukulele等の選択機能
5. **メロディモード**: メジャースケール、ペンタトニック
6. **複数ボタン対応**: キー変更とコード選択の分離

#### 優先度低
7. **USB MIDI**: PCとの直接接続
8. **保存機能**: 設定の不揮発保存
9. **シーケンサー**: 自動演奏機能

### 参考情報

#### GROVE端子ピン配置
- **G1 (黄)**: RX (MIDI IN) - 現在未使用
- **G2 (白)**: TX (MIDI OUT) - 使用中
- **G3**: 3.3V
- **G4**: GND

#### KANTANMusic API関数
```cpp
// メイン関数
uint8_t KANTANMusic_GetMidiNoteNumber(
    int pitch,     // 1-6: 構成音番号
    int degree,    // 1-7: コード番号
    int key,       // 0-11: キー
    const KANTANMusic_GetMidiNoteNumberOptions* options
);

// オプション設定
KANTANMusic_GetMidiNoteNumber_SetDefaultOptions(&options);
options.voicing = KANTANMusic_Voicing_Close;
```

#### デバッグ確認方法
1. **シリアルモニター**: 115200 baud
2. **MIDI出力**: ロジックアナライザー/MIDIモニター
3. **ディスプレイ**: リアルタイム状態表示

---

**「KANTAN Music準拠」** - InstaChord株式会社のKANTAN Music APIを使用
