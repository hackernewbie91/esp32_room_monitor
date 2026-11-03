#include <WiFi.h>
#include <Wire.h>
#include <SPI.h>
#include <FS.h>
#include <TFT_eSPI.h>
#include <Adafruit_SHT31.h>
#include <time.h>
#include <WiFiManager.h>

// ================= DEFINISI PIN =================
#define SDA_PIN      8
#define SCL_PIN      9
#define DOOR_PIN     4   // MC-38
#define BUZZER_PIN   5   // Active Buzzer

// ================= NTP =================
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 25200; // WIB (UTC+7)
const int   daylightOffset_sec = 0;

// ================= OBJECT & WARNA =================
TFT_eSPI tft = TFT_eSPI();
Adafruit_SHT31 sht31 = Adafruit_SHT31();
WiFiManager wifiManager;

// Warna Kustom
#define BG_COLOR        0x0000 // Hitam
#define HEADER_COLOR    0x0193 // Biru Navy (Lebih elegan dari sebelumnya)
#define SEPARATOR_COLOR 0x7BEF // Abu-abu terang untuk garis pembatas
#define TEXT_LABEL      TFT_WHITE
#define TEXT_TEMP       0x07FF // Cyan
#define TEXT_HUMI       0x001F // Biru
#define TEXT_CLOSED     TFT_GREEN
#define TEXT_OPEN       TFT_RED

// ================= VARIABEL =================
float suhu = 0.0;
float kelembapan = 0.0;
bool pintuTerbuka = false;
unsigned long lastUpdate = 0;
const long intervalUpdate = 1000; 

bool previousWiFiState = false; 
int previousBars = -1; // <-- TAMBAHKAN BARIS INI (Untuk melacak jumlah bar)

void setup() {
  Serial.begin(115200);
  
  pinMode(DOOR_PIN, INPUT_PULLUP); 
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  Wire.begin(SDA_PIN, SCL_PIN);
  if (!sht31.begin(0x44)) {  
    Serial.println("Gagal mendeteksi SHT31! Cek kabel I2C.");
  }

  tft.init();
  tft.setRotation(1); // Landscape
  tft.fillScreen(BG_COLOR);
  drawStaticUI();

  // --- KONEKSI WIFI MENGGUNAKAN WIFIMANAGER ---
  tft.setTextColor(TFT_YELLOW, BG_COLOR);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("Connecting WiFi...", 160, 225, 4);
  
  wifiManager.setConfigPortalTimeout(180); 
  bool res = wifiManager.autoConnect("ServerRoom_Config", "admin1234");

  if (!res) {
    Serial.println("Gagal terhubung ke WiFi.");
    tft.fillRect(0, 210, 320, 30, BG_COLOR);
    tft.setTextColor(TFT_RED, BG_COLOR);
    tft.drawString("WiFi Failed. Restart...", 160, 225, 4);
    delay(3000);
    ESP.restart(); 
  } 

  Serial.println("WiFi Terhubung!");
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  tft.fillRect(0, 210, 320, 30, BG_COLOR); 
  
  previousWiFiState = true;
  drawWiFiIcon(true); 
}

void loop() {
  unsigned long currentMillis = millis();
  
  if (currentMillis - lastUpdate >= intervalUpdate) {
    lastUpdate = currentMillis;

    // --- LOGIKA BARU: BACA KEKUATAN SINYAL WIFI (RSSI) ---
    int currentBars = -1;
    if (WiFi.status() == WL_CONNECTED) {
      long rssi = WiFi.RSSI();
      
      if (rssi >= -50) currentBars = 4;       // Sangat Kuat
      else if (rssi >= -65) currentBars = 3;  // Kuat
      else if (rssi >= -75) currentBars = 2;  // Sedang
      else if (rssi >= -85) currentBars = 1;  // Lemah
      else currentBars = 0;                   // Sangat Lemah
    }

    // Hanya gambar ulang jika jumlah bar berubah (mencegah kedip)
    if (currentBars != previousBars) {
      drawWiFiIcon(currentBars);
      previousBars = currentBars;
    }

    // --- SISA KODE SENSOR TETAP SAMA ---
    float t = sht31.readTemperature();
    float h = sht31.readHumidity();
    if (!isnan(t) && !isnan(h)) {
      suhu = t;
      kelembapan = h;
    }

    int doorState = digitalRead(DOOR_PIN);
    if (doorState == HIGH) { 
      pintuTerbuka = true;
      digitalWrite(BUZZER_PIN, HIGH); 
    } else {                 
      pintuTerbuka = false;
      digitalWrite(BUZZER_PIN, LOW);  
    }

    updateDisplay();
  }
}

// ================= FUNGSI UI =================

void drawWiFiIcon(int bars) {
  int startX = 290; 
  int baseY = 28;   
  
  // Hapus area ikon lama
  tft.fillRect(280, 0, 40, 35, HEADER_COLOR);
  
  uint16_t colorActive = TFT_GREEN;
  uint16_t colorInactive = 0x39E7; // Warna abu-abu gelap untuk batang yang mati
  
  if (bars == -1) {
    // Gambar Silang Merah jika terputus
    uint16_t colorRed = TFT_RED;
    tft.drawLine(startX - 2, baseY - 20, startX + 16, baseY - 4, colorRed);
    tft.drawLine(startX - 2, baseY - 4, startX + 16, baseY - 20, colorRed);
  } else {
    // Gambar 4 batang (menyala jika bars >= index, abu-abu jika kurang)
    tft.fillRoundRect(startX, baseY - 6, 4, 6, 2, (bars >= 1) ? colorActive : colorInactive);
    tft.fillRoundRect(startX + 7, baseY - 12, 4, 12, 2, (bars >= 2) ? colorActive : colorInactive);
    tft.fillRoundRect(startX + 14, baseY - 18, 4, 18, 2, (bars >= 3) ? colorActive : colorInactive);
    tft.fillRoundRect(startX + 21, baseY - 24, 4, 24, 2, (bars >= 4) ? colorActive : colorInactive);
  }
}

void drawStaticUI() {
  // Header (Warna Biru Navy)
  tft.fillRect(0, 0, 320, 35, HEADER_COLOR);
  tft.setTextColor(TFT_WHITE, HEADER_COLOR);
  
  // Judul (Font 4)
  tft.setTextDatum(ML_DATUM); 
  tft.drawString("Hilya Room Monitor", 10, 18, 4); 

  // Garis Pembatas di bawah Header
  tft.drawFastHLine(0, 35, 320, SEPARATOR_COLOR); 

  // Ikon Termometer
  tft.drawRoundRect(10, 55, 10, 24, 5, TFT_WHITE); 
  tft.fillCircle(15, 81, 7, TFT_RED);
  tft.fillRect(12, 65, 6, 15, TFT_RED);

  // Ikon Tetesan Air
  tft.fillTriangle(15, 115, 29, 115, 22, 103, TEXT_HUMI);
  tft.fillCircle(22, 115, 7, TEXT_HUMI);

  // Ikon Pintu
  tft.drawRect(10, 145, 16, 24, TFT_WHITE);
  tft.fillCircle(22, 157, 2, TFT_WHITE);
  tft.drawLine(7, 169, 29, 169, TFT_WHITE);

  // Label Teks
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(TEXT_LABEL, BG_COLOR);
  tft.drawString("Temp", 40, 70, 4);
  tft.drawString("Humidity", 40, 115, 4);
  tft.drawString("Door", 40, 155, 4);

  // Tanda Titik Dua
  tft.drawString(":", 155, 70, 4);   
  tft.drawString(":", 155, 115, 4);  
  tft.drawString(":", 155, 155, 4);  

  // Garis Pembatas Bawah (sebelum Jam)
  tft.drawFastHLine(0, 200, 320, TFT_DARKGREY);
}

void updateDisplay() {
  // --- SUHU ---
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(TEXT_TEMP, BG_COLOR); 
  tft.drawString(String(suhu, 1) + "  ", 170, 70, 4); // Spasi untuk membersihkan sisa angka
  tft.drawString("C", 225, 70, 4);             

  // --- KELEMBAPAN ---
  tft.setTextColor(TEXT_HUMI, BG_COLOR); 
  tft.drawString(String(kelembapan, 1) + "  ", 170, 115, 4); 
  tft.drawString("%", 230, 115, 4);                   

  // --- PINTU ---
  if (pintuTerbuka) {
    tft.setTextColor(TEXT_OPEN, BG_COLOR); 
    tft.drawString("OPEN     ", 170, 155, 4); 
  } else {
    tft.setTextColor(TEXT_CLOSED, BG_COLOR); 
    tft.drawString("CLOSED   ", 170, 155, 4); 
  }

  // --- JAM & TANGGAL ---
  if (WiFi.status() == WL_CONNECTED) {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) return;
    
    char timeString[20];
    char dateString[20];
    strftime(timeString, sizeof(timeString), "%H:%M:%S", &timeinfo); 
    strftime(dateString, sizeof(dateString), "%d %b %Y", &timeinfo);

    tft.setTextColor(TFT_WHITE, BG_COLOR); 
    
    // Tanggal (Rata Kiri)
    tft.setTextDatum(ML_DATUM); 
    tft.drawString(String(dateString), 10, 225, 4);

    // Jam (Rata Kanan)
    tft.setTextDatum(MR_DATUM); 
    tft.drawString(String(timeString) + "  ", 310, 225, 4); 
  }
}