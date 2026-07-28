#define BLYNK_TEMPLATE_ID "YourTemplateID"
#define BLYNK_TEMPLATE_NAME "Energy Monitor"
#define BLYNK_PRINT Serial

#include <WiFi.h>
#include <BlynkSimpleEsp32.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <EEPROM.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "EmonLib.h"
#include <WiFiClientSecure.h>
#include <time.h>

// ---------------- WIFI ----------------
char auth[] = "BLYNK_AUTH_TOKEN";
char ssid[] = "YOUR_WIFI_NAME";
char pass[] = "YOUR_WIFI_PASSWORD";

// ---------------- TELEGRAM ----------------
const char* telegramBotToken = "YOUR_BOT_TOKEN";
const char* telegramChatID = "YOUR_CHAT_ID";

WiFiClientSecure client;

// ---------------- LCD ----------------
LiquidCrystal_I2C lcd(0x27, 20, 2);

// ---------------- ENERGY MONITOR ----------------
EnergyMonitor emon;
BlynkTimer timer;

float kWh = 0;
float cost = 0;

const float ratePerkWh = 6.5;
float iCalibration = 1.80;

#define ACTUAL_VOLTAGE 230.0

// EEPROM
#define ADDR_KWH 0
#define ADDR_COST 10

// ---------------- DATE STORAGE ----------------
struct EnergyRecord
{
  String date;
  float energy;
  float cost;
};

EnergyRecord records[100];
int recordIndex = 0;

unsigned long lastTelegramCheck = 0;
int lastUpdateID = 0;

// =====================================================
// STORE DAILY DATA
// =====================================================
void storeData(String date, float energy, float cost)
{
  records[recordIndex].date = date;
  records[recordIndex].energy = energy;
  records[recordIndex].cost = cost;

  recordIndex = (recordIndex + 1) % 100;
}

// =====================================================
// SEND TELEGRAM MESSAGE
// =====================================================
void sendTelegramMessage(String message)
{
  HTTPClient http;

  String url =
    "https://api.telegram.org/bot" +
    String(telegramBotToken) +
    "/sendMessage?chat_id=" +
    String(telegramChatID) +
    "&text=" + message;

  http.begin(client, url);

  int httpCode = http.GET();

  if (httpCode > 0)
  {
    Serial.println("Telegram Message Sent");
  }
  else
  {
    Serial.println("Telegram Failed");
  }

  http.end();
}

// =====================================================
// GET DATA BY DATE RANGE
// =====================================================
void getDataByDate(String startDate, String endDate)
{
  float energySum = 0;
  float costSum = 0;

  for (int i = 0; i < 100; i++)
  {
    if (records[i].date >= startDate &&
        records[i].date <= endDate)
    {
      energySum += records[i].energy;
      costSum += records[i].cost;
    }
  }

  String msg = "⚡ ENERGY REPORT\n\n";

  msg += "From: ";
  msg += startDate;
  msg += "\nTo: ";
  msg += endDate;
  msg += "\n\nEnergy Used: ";
  msg += String(energySum, 2);
  msg += " kWh\n";

  msg += "Total Cost: Rs ";
  msg += String(costSum, 2);

  sendTelegramMessage(msg);
}

// =====================================================
// CHECK TELEGRAM COMMANDS
// =====================================================
void checkTelegramMessages()
{
  HTTPClient http;

  String url =
    "https://api.telegram.org/bot" +
    String(telegramBotToken) +
    "/getUpdates?offset=" +
    String(lastUpdateID + 1);

  http.begin(client, url);

  int httpCode = http.GET();

  if (httpCode == 200)
  {
    String payload = http.getString();

    DynamicJsonDocument doc(4096);
    deserializeJson(doc, payload);

    JsonArray results = doc["result"];

    for (JsonObject result : results)
    {
      lastUpdateID = result["update_id"];

      String text =
        result["message"]["text"].as<String>();

      Serial.println("Received: " + text);

      // -----------------------------------
      // /start COMMAND
      // -----------------------------------
      if (text == "/start")
      {
        String msg = "⚡ SMART ENERGY MONITOR\n\n";

        msg += "Total Energy: ";
        msg += String(kWh, 2);
        msg += " kWh\n";

        msg += "Total Cost: Rs ";
        msg += String(cost, 2);

        sendTelegramMessage(msg);
      }

      // -----------------------------------
      // DATE QUERY COMMAND
      // -----------------------------------
      else if (text.startsWith("/date"))
      {
        int firstSpace = text.indexOf(' ');
        int secondSpace = text.indexOf(' ', firstSpace + 1);

        if (firstSpace > 0 && secondSpace > 0)
        {
          String startDate =
            text.substring(firstSpace + 1, secondSpace);

          String endDate =
            text.substring(secondSpace + 1);

          startDate.trim();
          endDate.trim();

          getDataByDate(startDate, endDate);
        }
        else
        {
          sendTelegramMessage(
            "Use format:\n/date YYYY-MM-DD YYYY-MM-DD"
          );
        }
      }
    }
  }

  http.end();
}

// =====================================================
// READ ENERGY
// =====================================================
void readEnergy()
{
  emon.calcVI(20, 2000);

  float current = emon.Irms;

  float power = ACTUAL_VOLTAGE * current;

  kWh += (power / 1000.0) * (2.0 / 3600.0);

  cost = kWh * ratePerkWh;

  EEPROM.put(ADDR_KWH, kWh);
  EEPROM.put(ADDR_COST, cost);

  EEPROM.commit();

  // ---------------- DATE ----------------
  struct tm timeinfo;
  getLocalTime(&timeinfo);

  char dateStr[20];

  strftime(dateStr,
           sizeof(dateStr),
           "%Y-%m-%d",
           &timeinfo);

  // Store Data
  storeData(String(dateStr), kWh, cost);

  // ---------------- LCD ----------------
  // 20x2 LCD DISPLAY
  // Row 1 -> Power
  // Row 2 -> Energy Consumed

  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print("Power: ");
  lcd.print(power, 1);
  lcd.print(" W");

  lcd.setCursor(0, 1);
  lcd.print("Energy: ");
  lcd.print(kWh, 3);
  lcd.print(" kWh");

  // ---------------- BLYNK ----------------
  Blynk.virtualWrite(V0, power);
  Blynk.virtualWrite(V1, kWh);
  Blynk.virtualWrite(V2, cost);

  Serial.println("========");
  Serial.println(dateStr);
  Serial.println(kWh);
  Serial.println(cost);
}

// =====================================================
// SETUP
// =====================================================
void setup()
{
  Serial.begin(115200);

  lcd.init();
  lcd.backlight();

  EEPROM.begin(50);

  EEPROM.get(ADDR_KWH, kWh);
  EEPROM.get(ADDR_COST, cost);

  WiFi.begin(ssid, pass);

  lcd.setCursor(0, 0);
  lcd.print("Connecting WiFi");

  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }

  Serial.println("WiFi Connected");

  lcd.clear();
  lcd.print("WiFi Connected");

  // Time Server
  configTime(19800, 0, "pool.ntp.org");

  // HTTPS
  client.setInsecure();

  // Blynk
  Blynk.begin(auth, ssid, pass);

  // Sensor
  emon.current(34, iCalibration);

  // Timer
  timer.setInterval(2000L, readEnergy);

  sendTelegramMessage("✅ Energy Monitor Started");
}

// =====================================================
// LOOP
// =====================================================
void loop()
{
  Blynk.run();
  timer.run();

  // Telegram Check
  if (millis() - lastTelegramCheck > 3000)
  {
    checkTelegramMessages();
    lastTelegramCheck = millis();
  }
}