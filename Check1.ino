#include <GxEPD2_BW.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

// --- FONTS ---
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include <Fonts/FreeSansBold24pt7b.h>

// ================================================================
// 1. CẤU HÌNH (Sửa lại WiFi của bạn)
// ================================================================
const char* ssid = "EDABK 2.4GHz";
const char* password = "1234567890@";
const char* mqtt_server = "broker.hivemq.com";
const char* mqtt_topic = "priceTag/data";

WiFiClient espClient;
PubSubClient client(espClient);

// ================================================================
// 2. CẤU HÌNH CHÂN (ESP32)
// ================================================================
#define PIN_CS    5
#define PIN_DC    17
#define PIN_RST   16
#define PIN_BUSY  4

GxEPD2_BW<GxEPD2_270, GxEPD2_270::HEIGHT> display(GxEPD2_270(PIN_CS, PIN_DC, PIN_RST, PIN_BUSY));

// Biến dữ liệu
String p_name = "Waiting...";
String p_old_price = ""; 
String p_new_price = ""; 
String p_sale = "";
String p_code = "000000";

String current_code = "";
bool needUpdate = false;

// ================================================================
// 3. CÁC HÀM VẼ (HELPER FUNCTIONS)
// ================================================================

// --- A. BARCODE CODE 11 ---
const uint8_t code11_patterns[11] = {
  0x01, 0x11, 0x09, 0x18, 0x05, 0x14, 0x0C, 0x03, 0x12, 0x10, 0x04 
};
const uint8_t code11_start_stop = 0x06; 

int calculateCode11Checksum(String code) {
  int weight = 1; int sum = 0;
  for (int i = code.length() - 1; i >= 0; i--) {
    int val = -1;
    if (code[i] == '-') val = 10; else if (isDigit(code[i])) val = code[i] - '0';
    if (val != -1) { sum += val * weight; weight++; if (weight > 10) weight = 1; }
  }
  return sum % 11;
}

String padCode(String rawCode) {
  String cleanCode = "";
  for (char c : rawCode) if (isDigit(c) || c == '-') cleanCode += c;
  if (cleanCode.length() > 10) cleanCode = cleanCode.substring(0, 10);
  return cleanCode;
}

void drawCode11Char(uint8_t pattern, int &x, int y, int h, int narrow, int wide, int gap) {
  for (int i = 0; i < 5; i++) {
    bool isWide = (pattern >> (4 - i)) & 1;
    int width = isWide ? wide : narrow;
    if (i % 2 == 0) display.fillRect(x, y, width, h, GxEPD_BLACK);
    x += width;
  }
  x += gap;
}

void drawBarcode(String code, int x, int y, int h) {
  int narrow = 2; int wide = 4; int gap = 3; int currentX = x;
  drawCode11Char(code11_start_stop, currentX, y, h, narrow, wide, gap);
  for (unsigned int i = 0; i < code.length(); i++) {
    char c = code[i];
    if (isdigit(c)) drawCode11Char(code11_patterns[c - '0'], currentX, y, h, narrow, wide, gap);
    else if (c == '-') drawCode11Char(code11_patterns[10], currentX, y, h, narrow, wide, gap);
  }
  int checksum = calculateCode11Checksum(code);
  drawCode11Char(code11_patterns[checksum == 10 ? 10 : checksum], currentX, y, h, narrow, wide, gap);
  drawCode11Char(code11_start_stop, currentX, y, h, narrow, wide, gap);
}

// --- B. VẼ PIN ---
void drawBattery(int x, int y) {
  display.drawRect(x, y, 20, 10, GxEPD_BLACK);
  display.fillRect(x + 20, y + 2, 2, 6, GxEPD_BLACK); 
  int percentage = 70; 
  int width = map(percentage, 0, 100, 0, 16);
  display.fillRect(x + 2, y + 2, width, 6, GxEPD_BLACK);
}

// --- C. VẼ WIFI ---
void drawRSSI(int x, int y) {
  long rssi = WiFi.RSSI(); 
  int bars = 0;
  if (rssi > -55) bars = 3; else if (rssi > -75) bars = 2; else if (rssi > -90) bars = 1;
  if (bars >= 1) display.fillRect(x, y + 8, 3, 4, GxEPD_BLACK); else display.drawRect(x, y + 8, 3, 4, GxEPD_BLACK);
  if (bars >= 2) display.fillRect(x + 5, y + 4, 3, 8, GxEPD_BLACK); else display.drawRect(x + 5, y + 4, 3, 8, GxEPD_BLACK);
  if (bars >= 3) display.fillRect(x + 10, y, 3, 12, GxEPD_BLACK); else display.drawRect(x + 10, y, 3, 12, GxEPD_BLACK);
}

// ================================================================
// 4. GIAO DIỆN HIỂN THỊ CHÍNH
// ================================================================
void updateDisplay() {
  Serial.println(">>> Updating UI (HUST Market Header)...");
  display.init(115200);
  display.setRotation(1); 
  
  // Tăng chiều cao Header lên 25px để chứa chữ đẹp hơn
  int headerH = 25;
  int midY = 95; // Đẩy nội dung xuống một chút
  int midX = 132; 
  
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    
    // --- 1. HEADER (PIN - HUST Market - WIFI) ---
    
    // Vẽ Pin (Góc trái) - Căn giữa theo chiều dọc header (y=7)
    drawBattery(5, 7); 
    
    // Vẽ Wifi (Góc phải)
    drawRSSI(245, 5);  

    // Vẽ chữ "HUST Market" ở giữa
    display.setTextColor(GxEPD_BLACK);
    display.setFont(&FreeSansBold9pt7b); // Font nhỏ đậm
    
    int16_t tbx, tby; uint16_t tbw, tbh;
    display.getTextBounds("HUST Market", 0, 0, &tbx, &tby, &tbw, &tbh);
    
    // Tính toán căn giữa: (Chiều rộng màn hình - Chiều rộng chữ) / 2
    int xHeader = (264 - tbw) / 2;
    int yHeader = 18; // Căn chỉnh dòng cơ sở cho vừa mắt

    display.setCursor(xHeader, yHeader);
    display.print("HUST Market");

    // Kẻ đường ngăn cách Header
    display.drawLine(0, headerH, 264, headerH, GxEPD_BLACK);

    // --- 2. KHUNG NỘI DUNG ---
    display.drawLine(0, midY, 264, midY, GxEPD_BLACK);      
    display.drawLine(midX, midY, midX, 176, GxEPD_BLACK);   

    // --- 3. TÊN SẢN PHẨM ---
    // Tự động chỉnh font chữ
    if (p_name.length() <= 10) {
        display.setFont(&FreeSansBold18pt7b);
        display.setCursor(5, 65); display.print(p_name);
    } else if (p_name.length() <= 18) {
        display.setFont(&FreeSansBold12pt7b);
        display.setCursor(5, 60); display.print(p_name);
    } else {
        display.setFont(&FreeSansBold9pt7b);
        display.setCursor(5, 45); display.print(p_name.substring(0, 20));
        display.setCursor(5, 65); display.print(p_name.substring(20));
    }

    // --- 4. GIÁ TIỀN & DISCOUNT ---
    if (p_sale != "" && p_sale != "0%") {
        // Giá cũ
        display.setFont(&FreeSansBold9pt7b);
        int16_t x1, y1; uint16_t w1, h1;
        display.getTextBounds(p_old_price, 0, 0, &x1, &y1, &w1, &h1);
        int xOld = 5; int yOld = midY + 25;
        
        display.setCursor(xOld, yOld); display.print(p_old_price);
        display.drawLine(xOld, yOld-5, xOld+w1, yOld-h1+5, GxEPD_BLACK); 
        
        display.setCursor(xOld + w1 + 5, yOld); display.print("-"); display.print(p_sale);
        
        // Giá mới
        display.setFont(&FreeSansBold18pt7b);
        int16_t x2, y2; uint16_t w2, h2;
        display.getTextBounds(p_new_price, 0, 0, &x2, &y2, &w2, &h2);
        display.setCursor((132 - w2)/2, 165); display.print(p_new_price);
    } else {
        display.setFont(&FreeSansBold18pt7b);
        int16_t x, y; uint16_t w, h;
        display.getTextBounds(p_new_price, 0, 0, &x, &y, &w, &h);
        display.setCursor((132 - w)/2, 150); display.print(p_new_price);
    }

    // --- 5. BARCODE ---
    String cleanID = padCode(p_code);
    drawBarcode(cleanID, 150, midY + 15, 40);

    display.setFont(&FreeSansBold9pt7b);
    int16_t tx, ty; uint16_t tw, th;
    display.getTextBounds(cleanID, 0, 0, &tx, &ty, &tw, &th);
    display.setCursor(132 + (132 - tw)/2, 165); 
    display.print(cleanID);

  } while (display.nextPage());
  display.powerOff();
}

// ================================================================
// 5. LOGIC & SETUP
// ================================================================

void callback(char* topic, byte* payload, unsigned int length) {
  String msg; for (int i=0; i<length; i++) msg+=(char)payload[i];
  JsonDocument doc; deserializeJson(doc, msg);
  
  String n_name = doc["name"]|"";
  String n_old  = doc["old_price"]|"$0";
  String n_new  = doc["new_price"]|"$0";
  String n_sale = doc["sale"]|"";
  String n_code = doc["code"]|"000000";

  if (n_code != current_code || n_new != p_new_price || n_sale != p_sale || n_name != p_name) {
     p_name = n_name; p_old_price = n_old; p_new_price = n_new;
     p_sale = n_sale; p_code = n_code;
     current_code = n_code;
     needUpdate = true;
  }
}

void reconnect() {
  while (!client.connected()) {
    if (client.connect(("ESP32-"+String(random(0xffff))).c_str())) client.subscribe(mqtt_topic);
    else delay(5000);
  }
}

void setup() {
  Serial.begin(115200); WiFi.begin(ssid, password);
  while (WiFi.status()!=WL_CONNECTED) delay(500);
  client.setServer(mqtt_server, 1883); client.setCallback(callback);
  display.init(115200);
}

void loop() {
  if (!client.connected()) reconnect();
  client.loop();
  if (needUpdate) { updateDisplay(); needUpdate = false; }
}