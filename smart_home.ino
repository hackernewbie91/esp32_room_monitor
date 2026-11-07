#include <WiFi.h>
#include <Wire.h>
#include <SPI.h>
#include <FS.h>
#include <TFT_eSPI.h>
#include <Adafruit_SHT31.h>
#include <time.h>
#include <WiFiManager.h>
#include <PubSubClient.h> // <-- TAMBAHAN UNTUK MQTT

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

// ================= SETUP MQTT =================
// GANTI DENGAN IP ADDRESS RASPBERRY PI ANDA
const char* mqtt_server = "192.168.2.10"; 
WiFiClient espClient;
PubSubClient client(espClient);
const char* topic_sensor = "home/server/data";
const char* topic_door   = "home/server/door";

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
bool lastDoorState = false; 

// Batas suhu panas (Silakan ubah angkanya sesuai selera)
#define TEMP_THRESHOLD 30.0 

// Variabel untuk melacak status panas/dingin
bool lastHotState = false; 
unsigned long lastUpdate = 0;
const long intervalUpdate = 1000; 

bool previousWiFiState = false; 
int previousBars = -1; 

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
  
  // Inisialisasi Server MQTT
  client.setServer(mqtt_server, 1883);
  
  previousWiFiState = true;
  drawWiFiIcon(true); 
}

void loop() {
  unsigned long currentMillis = millis();
  
  // ==========================================
  // --- LOGIKA MQTT (BERJALAN SETIAP SAAT) ---
  // ==========================================
  if (!client.connected()) {
    // Parameter LWT ditambahkan langsung di dalam fungsi connect
    // Format: connect(clientID, username, password, willTopic, willQos, willRetain, willMessage)
    if (client.connect("ESP32-S3-ServerRoom", NULL, NULL, "home/server/status", 1, true, "OFFLINE")) {
      Serial.println("MQTT Terhubung!");
      
      // Kirim status ONLINE setelah berhasil terhubung
      client.publish("home/server/status", "ONLINE", true); 
    }
  }
  client.loop();

  // Kirim data ke Home Assistant setiap 5 detik
  static unsigned long lastMsg = 0;
  if (currentMillis - lastMsg > 5000) {
    lastMsg = currentMillis;
    
    // Format data JSON
    String payload = "{\"temperature\":" + String(suhu, 1) + ",\"humidity\":" + String(kelembapan, 1) + "}";
    
    // Publish ke MQTT
    client.publish(topic_sensor, payload.c_str());
    client.publish(topic_door, pintuTerbuka ? "OPEN" : "CLOSED");
    
    Serial.println("Data terkirim ke MQTT: " + payload);
  }
  // ==========================================

  // --- LOGIKA PEMBARUAN LAYAR TFT (SETIAP 1 DETIK) ---
  if (currentMillis - lastUpdate >= intervalUpdate) {
    lastUpdate = currentMillis;

    // 1. BACA KEKUATAN SINYAL WIFI (RSSI) & GAMBAR IKON
    int currentRSSI = WiFi.RSSI(); 
    drawWiFiIcon(currentRSSI);     

    // 2. BACA SENSOR
    float t = sht31.readTemperature();
    float h = sht31.readHumidity();
    if (!isnan(t) && !isnan(h)) {
      suhu = t;
      kelembapan = h;
    }

    // 3. BACA SENSOR PINTU
    int doorState = digitalRead(DOOR_PIN);
    if (doorState == HIGH) { 
      pintuTerbuka = true;
      digitalWrite(BUZZER_PIN, HIGH); 
    } else {                 
      pintuTerbuka = false;
      digitalWrite(BUZZER_PIN, LOW);  
    }

    // 4. UPDATE LAYAR UTAMA
    updateDisplay();
  }
}

void drawWiFiIcon(int rssi) {
  // Posisi diatur ulang
  int startX = 294;  // Posisi batang sinyal (paling kanan)
  int textX = 288;   // Posisi ujung kanan teks (di sebelah kiri batang)
  int baseY = 28;    
  
  // Bersihkan area header kanan (diperlebar ke kiri agar teks dBm muat)
  tft.fillRect(220, 0, 100, 35, HEADER_COLOR);
  
  if (WiFi.status() != WL_CONNECTED) {
    tft.setTextColor(TFT_RED, HEADER_COLOR);
    tft.setTextDatum(MR_DATUM);
    tft.drawString("OFF", textX, 18, 2);
    
    tft.drawLine(startX, baseY - 15, startX + 15, baseY, TFT_RED);
    tft.drawLine(startX, baseY, startX + 15, baseY - 15, TFT_RED);
    return;
  }

  uint16_t color;
  if (rssi >= -60) color = TFT_GREEN;
  else if (rssi >= -70) color = TFT_YELLOW;
  else if (rssi >= -80) color = TFT_ORANGE;
  else color = TFT_RED;

  // Gambar Teks RSSI (Menambahkan " dBm")
  tft.setTextColor(color, HEADER_COLOR);
  tft.setTextDatum(MR_DATUM); // Rata Kanan
  tft.drawString(String(rssi) + " dBm", textX, 18, 2); 

  // Gambar Batang Sinyal
  int bars = 0;
  if (rssi >= -55) bars = 4;
  else if (rssi >= -65) bars = 3;
  else if (rssi >= -75) bars = 2;
  else if (rssi >= -85) bars = 1;
  
  for (int i = 0; i < 4; i++) {
    int barHeight = 6 + (i * 4);
    int barY = baseY - barHeight;
    uint16_t barColor = (i < bars) ? color : 0x39E7; 
    tft.fillRoundRect(startX + (i * 6), barY, 4, barHeight, 1, barColor);
  }
}

void drawStaticUI() {
  // Header (Warna Biru Navy)
  tft.fillRect(0, 0, 320, 35, HEADER_COLOR);
  tft.setTextColor(TFT_WHITE, HEADER_COLOR);
  
  // Judul (Font 4)
  tft.setTextDatum(ML_DATUM); 
  tft.drawString("Home Monitor", 5, 18, 4); 

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
  tft.drawString("Suhu", 40, 70, 4);
  tft.drawString("Humidity", 40, 115, 4);
  tft.drawString("Pintu", 40, 155, 4);

  // Tanda Titik Dua
  tft.drawString(":", 155, 70, 4);   
  tft.drawString(":", 155, 115, 4);  
  tft.drawString(":", 155, 155, 4);  

  // Garis Pembatas Bawah (sebelum Jam)
  tft.drawFastHLine(0, 200, 320, TFT_DARKGREY);
}

void updateDisplay() {
  // --- SUHU ---
  // Cek apakah suhu melewati batas (30.0°C)
  bool currentHotState = (suhu >= TEMP_THRESHOLD);
  
  // Tentukan warna: Merah jika panas, Cyan jika normal
  uint16_t tempColor = currentHotState ? TFT_RED : TEXT_TEMP;

  // Jika status berubah dari panas ke dingin (atau sebaliknya), gambar ulang label "Temp"
  if (currentHotState != lastHotState) {
    // Hapus area label "Temp" (X=35, Y=55, Lebar=95, Tinggi=30)
    tft.fillRect(35, 55, 95, 30, BG_COLOR); 
    tft.setTextDatum(ML_DATUM);
    tft.setTextColor(tempColor, BG_COLOR);
    tft.drawString("Suhu", 40, 70, 4); // Gambar ulang label dengan warna baru
    
    lastHotState = currentHotState; // Simpan status
  }

  // Selalu perbarui angka suhu dan satuan C
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(tempColor, BG_COLOR); 
  tft.drawString(String(suhu, 1) + "  ", 170, 70, 4); 
  tft.drawString("C", 225, 70, 4);                        

  // --- KELEMBAPAN ---
  tft.setTextColor(TEXT_HUMI, BG_COLOR); 
  tft.drawString(String(kelembapan, 1) + "  ", 170, 115, 4); 
  tft.drawString("%", 230, 115, 4);                   

  // --- PINTU ---
  if (pintuTerbuka != lastDoorState) {
    // Hapus area pintu (X=170, Y=140, Lebar=150, Tinggi=30)
    tft.fillRect(170, 140, 150, 30, BG_COLOR); 
    
    if (pintuTerbuka) {
      tft.setTextColor(TEXT_OPEN, BG_COLOR);
      tft.drawString("TERBUKA", 170, 155, 4); 
    } else {
      tft.setTextColor(TEXT_CLOSED, BG_COLOR);
      tft.drawString("TERTUTUP", 170, 155, 4); 
    }
    lastDoorState = pintuTerbuka; // Simpan status agar tidak digambar berulang kali
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