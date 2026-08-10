#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <ArduinoJson.h>
#include "HX711.h"

// ================= 1. SETTING WIFI & BOT =================
const char* ssid = "Adistian";
const char* password = "90909090";
#define BOT_TOKEN "8807777701:AAFpMilHjau5MvPNxahbsPs0hsJQfaUw00A"

WiFiClientSecure secured_client;
UniversalTelegramBot bot(BOT_TOKEN, secured_client);

// ================= 2. PENGATURAN KEAMANAN (WHITELIST) =================
const int jumlah_staf = 2; 
// Masukkan Chat ID kamu dan staf produksi EverBeads di sini
String daftar_akses[jumlah_staf] = {
  "7923202399","8850110470"
};

// Fungsi pengecekan izin akses
bool cekIzinAkses(String id_pengirim) {
  for (int i = 0; i < jumlah_staf; i++) {
    if (id_pengirim == daftar_akses[i]) return true;
  }
  return false;
}

// Fungsi pengiriman pesan ke seluruh staf
void broadcastNotifikasi(String pesan) {
  for (int i = 0; i < jumlah_staf; i++) {
    bot.sendMessage(daftar_akses[i], pesan, "Markdown");
  }
}

// ================= 3. STRUKTUR DATA & KALIBRASI =================
const int SCK_PIN = 4;
const float berat_per_tali = 31.0; // Disesuaikan: 27 gram per tali
const int tali_per_pack = 50;
int batas_minimum_tali = 10; 

// Blueprint untuk objek Laci
struct MaterialLaci {
  String namaMaterial;
  int pinDT;
  float calibrationFactor;
  HX711 scale;
  bool statusAman;
};

const int JUMLAH_LACI = 2; 

// Data terpusat Laci EverBeads
MaterialLaci rakEverBeads[JUMLAH_LACI] = {
  {"Laci Atas", 18, -208.3, HX711(), true},   // Laci 0
  {"Laci Bawah", 19, 202, HX711(), true}    // Laci 1
};

// ================= 4. VARIABEL SISTEM & TIMER =================
unsigned long waktuTerakhirCek = 0;
const long jedaCek = 1000; 

unsigned long waktuTerakhirKoneksi = 0;
const long jedaKoneksiUlang = 10000; // Coba reconnect setiap 10 detik

// Timer untuk Serial Monitor (BARU)
unsigned long waktuTerakhirSerial = 0;
const long jedaSerial = 2000; // Tampilkan di Serial tiap 2 detik

// ================= 5. FUNGSI TELEGRAM BOT =================
void sendMainMenu(String chat_id) {
  String welcome = "✨ *EVERBEADS SMART INVENTORY* ✨\n";
  welcome += "Sistem Dinamis Aktif. Pilih menu di bawah:";

  // Membuat tombol Tare secara dinamis
  String dynamicTareMenu = "[";
  for(int i = 0; i < JUMLAH_LACI; i++) {
    dynamicTareMenu += "\"⚖️ Tare " + rakEverBeads[i].namaMaterial + "\"";
    if(i < JUMLAH_LACI - 1) dynamicTareMenu += ", ";
  }
  dynamicTareMenu += "]";

  String keyboardJson = "[[\"📊 Cek Stok Real-time\"], " + dynamicTareMenu + ", [\"⚙️ Status & Pengaturan\"]]";
  bot.sendMessageWithReplyKeyboard(chat_id, welcome, "Markdown", keyboardJson, true);
}

void handleNewMessages(int numNewMessages) {
  for (int i = 0; i < numNewMessages; i++) {
    String chat_id = String(bot.messages[i].chat_id);
    String text = bot.messages[i].text;

    // Filter Keamanan
    if (!cekIzinAkses(chat_id)) {
      bot.sendMessage(chat_id, "⛔ *AKSES DITOLAK*\nMaaf, Anda tidak terdaftar sebagai staf internal EverBeads.", "Markdown");
      continue; 
    }

    if (text == "/start" || text == "Menu Utama") {
      sendMainMenu(chat_id);
    }

    else if (text == "📊 Cek Stok Real-time") {
      String stat = "📋 *LAPORAN STOK MATERIAL*\n\n";
      
      for(int j = 0; j < JUMLAH_LACI; j++) {
        float berat = rakEverBeads[j].scale.get_units(10);
        if (berat < 0.5 && berat > -0.5) berat = 0; // Filter fluktuasi
        int totalTali = berat / berat_per_tali;
        
        stat += "📦 *" + rakEverBeads[j].namaMaterial + "*\n";
        stat += "└ Berat: `" + String(berat, 1) + " g`\n";
        stat += "└ Total: `" + String(totalTali / tali_per_pack) + " Pack, " + String(totalTali % tali_per_pack) + " Tali`\n\n";
      }
      stat += "✅ _Status: Monitoring Aktif_";
      bot.sendMessage(chat_id, stat, "Markdown");
    }

    // Logika Tare
    else if (text.startsWith("⚖️ Tare ")) {
      bool ditemukan = false;
      for(int j = 0; j < JUMLAH_LACI; j++) {
        if(text.indexOf(rakEverBeads[j].namaMaterial) != -1) {
          String targetMaterial = rakEverBeads[j].namaMaterial;
          bot.sendMessage(chat_id, "⏳ Mengosongkan " + targetMaterial + "...", "");
          rakEverBeads[j].scale.tare();
          bot.sendMessage(chat_id, "✅ *" + targetMaterial + "* berhasil di-Tare (0.0g).", "Markdown");
          ditemukan = true;
          break; 
        }
      }
      if(!ditemukan) bot.sendMessage(chat_id, "❌ Laci tidak ditemukan.");
    }

    else if (text == "⚙️ Status & Pengaturan") {
      long rssi = WiFi.RSSI();
      String info = "🛠️ *PENGATURAN SISTEM*\n\n";
      info += "• Mikrokontroler: `ESP32 DevKit V1`\n";
      info += "• Sinyal WiFi: `" + String(rssi) + " dBm`\n";
      info += "• Batas Peringatan: `" + String(batas_minimum_tali) + " Tali`\n\n";
      info += "💡 *Cara Mengubah Batas Minimum:*\n";
      info += "Ketik `/batas [angka]`\n";
      info += "Contoh: `/batas 20`";
      bot.sendMessage(chat_id, info, "Markdown");
    }

    else if (text.startsWith("/batas ")) {
      String angkaString = text.substring(7); 
      int batas_baru = angkaString.toInt();
      
      if (batas_baru > 0) {
        batas_minimum_tali = batas_baru;
        broadcastNotifikasi("⚙️ *INFO SISTEM*\nBatas minimum stok diubah menjadi *" + String(batas_minimum_tali) + " tali*.");
      } else {
        bot.sendMessage(chat_id, "❌ Format salah. Contoh: `/batas 15`", "Markdown");
      }
    }
  }
}

// ================= 6. SETUP UTAMA =================
void setup() {
  Serial.begin(115200);
  WiFi.begin(ssid, password);
  secured_client.setCACert(TELEGRAM_CERTIFICATE_ROOT);
  
  Serial.print("Menghubungkan ke WiFi...");
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  Serial.println("\nWiFi Terhubung.");
  
  // Inisialisasi Sensor secara dinamis
  for(int i = 0; i < JUMLAH_LACI; i++) {
    rakEverBeads[i].scale.begin(rakEverBeads[i].pinDT, SCK_PIN);
    rakEverBeads[i].scale.set_scale(rakEverBeads[i].calibrationFactor);
    rakEverBeads[i].scale.tare();
  }

  // === PANDUAN KALIBRASI SERIAL MONITOR ===
  Serial.println("\n==============================================");
  Serial.println("✅ Sistem Siap! Panduan Kalibrasi Manual:");
  Serial.println("Ketik karakter di bawah lalu tekan Enter/Send:");
  Serial.println(" [q] -> Tambah Faktor Laci Atas (+5)");
  Serial.println(" [a] -> Kurangi Faktor Laci Atas (-5)");
  Serial.println(" [w] -> Tambah Faktor Laci Bawah (+5)");
  Serial.println(" [s] -> Kurangi Faktor Laci Bawah (-5)");
  Serial.println(" [t] -> Tare (Nol-kan) Semua Laci");
  Serial.println("==============================================\n");

  broadcastNotifikasi("🤖 *Sistem Inventori Aktif*\nMemantau " + String(JUMLAH_LACI) + " laci Manik-manik. Ketik /start untuk memunculkan menu.");
}

// ================= 7. LOOPING UTAMA =================
void loop() {
  
  // --- A. BACA INPUT DARI SERIAL MONITOR (BARU) ---
  if (Serial.available()) {
    char input = Serial.read();
    
    // Laci 0 (Manik Ungu)
    if (input == 'q') {
      rakEverBeads[0].calibrationFactor += 5.0;
      rakEverBeads[0].scale.set_scale(rakEverBeads[0].calibrationFactor);
    } 
    else if (input == 'a') {
      rakEverBeads[0].calibrationFactor -= 5.0;
      rakEverBeads[0].scale.set_scale(rakEverBeads[0].calibrationFactor);
    } 
    // Laci 1 (Manik Perak)
    else if (input == 'w') {
      rakEverBeads[1].calibrationFactor += 5.0;
      rakEverBeads[1].scale.set_scale(rakEverBeads[1].calibrationFactor);
    } 
    else if (input == 's') {
      rakEverBeads[1].calibrationFactor -= 5.0;
      rakEverBeads[1].scale.set_scale(rakEverBeads[1].calibrationFactor);
    } 
    // Tare Semua
    else if (input == 't') {
      Serial.println("⏳ Mengkalibrasi ulang ke titik Nol (Tare)...");
      for(int i = 0; i < JUMLAH_LACI; i++) {
        rakEverBeads[i].scale.tare();
      }
      Serial.println("✅ Semua Timbangan berhasil di-Nol-kan!");
    }
  }

  // --- B. TAMPILKAN KE SERIAL MONITOR (BARU) ---
  if (millis() - waktuTerakhirSerial >= jedaSerial) {
    Serial.println("📊 --- MONITORING REAL-TIME ---");
    for(int i = 0; i < JUMLAH_LACI; i++) {
      float berat = rakEverBeads[i].scale.get_units(5);
      if (berat < 0.5 && berat > -0.5) berat = 0.0;
      
      Serial.print("📦 "); 
      Serial.print(rakEverBeads[i].namaMaterial);
      Serial.print(" \t| Berat: "); 
      Serial.print(berat, 1);
      Serial.print("g \t| Faktor: ");
      Serial.println(rakEverBeads[i].calibrationFactor, 0);
    }
    Serial.println("-------------------------------");
    waktuTerakhirSerial = millis();
  }

  // --- C. Penanganan Error: Auto-Reconnect WiFi ---
  if (WiFi.status() != WL_CONNECTED) {
    if (millis() - waktuTerakhirKoneksi >= jedaKoneksiUlang) {
      Serial.println("⚠️ Koneksi terputus! Mencoba reconnect WiFi...");
      WiFi.disconnect();
      WiFi.reconnect();
      waktuTerakhirKoneksi = millis();
    }
  } 
  // --- D. Proses Telegram (Hanya berjalan jika WiFi terhubung) ---
  else {
    if (millis() > waktuTerakhirCek + jedaCek) {
      int numNewMessages = bot.getUpdates(bot.last_message_received + 1);
      while (numNewMessages) {
        handleNewMessages(numNewMessages);
        numNewMessages = bot.getUpdates(bot.last_message_received + 1);
      }
      waktuTerakhirCek = millis();
    }
  }

  // --- E. Continuous Monitoring: Baca Sensor & Cek Peringatan ---
  for(int i = 0; i < JUMLAH_LACI; i++) {
    float berat = rakEverBeads[i].scale.get_units(1); // Gunakan (1) saja di sini agar loop cepat, rata-rata (5) sudah dipakai di bot & serial
    int totalTali = berat / berat_per_tali;
    
    if (totalTali < batas_minimum_tali) {
      if (rakEverBeads[i].statusAman == true) {
        if (WiFi.status() == WL_CONNECTED) {
          broadcastNotifikasi("🚨 *STOK KRITIS: " + rakEverBeads[i].namaMaterial + "*\nSisa: `" + String(totalTali) + " tali`\nBatas minimum: " + String(batas_minimum_tali));
          rakEverBeads[i].statusAman = false; 
        }
      }
    } 
    else { 
      if (rakEverBeads[i].statusAman == false) {
        if (WiFi.status() == WL_CONNECTED) {
          broadcastNotifikasi("✅ *STOK KEMBALI AMAN: " + rakEverBeads[i].namaMaterial + "*\nTelah di-restock. Sisa: `" + String(totalTali) + " tali`");
          rakEverBeads[i].statusAman = true; 
        }
      }
    }
  }
}