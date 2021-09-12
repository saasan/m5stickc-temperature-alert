#include <string>
#include <sstream>
#include <iomanip>
#include <M5StickC.h>
#include <Wire.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include "SHT3X.h"
#include "secrets.h"

// -----------------------------------------------------------------------------
// 定数
// -----------------------------------------------------------------------------
// 温度を測定する間隔(秒)
const uint32_t INTERVAL = 10;
// アラートメッセージを送信する気温
const float TEMP_THRESHOLD = 27.00;
// Incoming Webhook URLのホスト名
const char* WEBHOOK_HOST = "hooks.slack.com";
// Incoming Webhookのポート番号
const uint16_t WEBHOOK_PORT = 443;
// GMTからの時間差(秒)
const long JST = 9 * 60 * 60;
// NTPサーバ
const char* NTP_SERVER = "ntp.nict.jp";
// 1日1回送信する時刻の時間
const int DAILY_HOUR = 10;
// 1日1回送信する時刻の分
const int DAILY_MIN = 0;
// 電源ボタンが1秒未満押された
const uint8_t AXP_WAS_PRESSED = 2;

// -----------------------------------------------------------------------------
// 変数
// -----------------------------------------------------------------------------
// デジタル温湿度センサーSHT30
SHT3X sht30;
// 温度
float temp = 0.0;

// -----------------------------------------------------------------------------
// 関数
// -----------------------------------------------------------------------------
// 無線LAN接続
void connectWiFi(const char* ssid, const char* passphrase) {
    Serial.print("Attempting to connect to SSID: ");
    Serial.println(WIFI_SSID);
    WiFi.begin(ssid, passphrase);

    while (WiFi.status() != WL_CONNECTED) {
        Serial.print(".");
        delay(1000);
    }

    Serial.print("Connected to ");
    Serial.println(WIFI_SSID);
}

// Slackへメッセージを送信する
void postMessage(const char* message) {
    WiFiClientSecure client;
    // HTTPS接続時に証明書による検証を行わない
    client.setInsecure();

    Serial.println("\nStarting connection to server...");
    if (!client.connect(WEBHOOK_HOST, WEBHOOK_PORT)) {
        Serial.println("Connection failed!");
    }
    else {
        Serial.println("Connected to server!");

        // リクエストを作成
        std::ostringstream payload, request;
        payload << "payload={\"text\": \"" << message << "\"}";
        request << "POST " << WEBHOOK_PATH << " HTTP/1.1\r\n"
                << "Host: " << WEBHOOK_HOST << "\r\n"
                << "User-Agent: WiFiClientSecure\r\n"
                << "Content-Type: application/x-www-form-urlencoded\r\n"
                << "Content-Length: " << payload.str().length() << "\r\n\r\n"
                << payload.str();

        Serial.println(request.str().c_str());

        // リクエストを送信
        client.print(request.str().c_str());
        client.println();

        while (client.connected()) {
            String line = client.readStringUntil('\n');
            if (line == "\r") {
                Serial.println("headers received");
                break;
            }
        }

        client.stop();
    }
}

// 温度を取得
void getTemp() {
    if (sht30.get() == 0) {
        temp = sht30.cTemp;
        Serial.printf("Temperature: %2.2f*C\r\n", temp);
    }
    else {
        Serial.println("sht30.get() failed");
    }
}

// 現在の温度を送信
void sendCurrentTemp() {
    std::ostringstream message;
    message << "現在の温度は"
            << std::fixed << std::setprecision(2) << temp
            << "度です。";
    postMessage(message.str().c_str());
}

// 現在の温度が閾値を超えていればメッセージを送信
void sendAlertMessage() {
    // メッセージ送信済みならtrue
    static bool message_sent = false;

    if (TEMP_THRESHOLD <= temp) {
        // 温度がTEMP_THRESHOLD以上でメッセージ未送信であれば送信
        if (!message_sent) {
            std::ostringstream message;
            message << "<!here> 温度が閾値を超えました！現在の温度は"
                    << std::fixed << std::setprecision(2) << temp
                    << "度です。";
            postMessage(message.str().c_str());

            message_sent = true;
        }
    }
    else {
        if (message_sent) {
            Serial.println("message_sent = false");

            std::ostringstream message;
            message << "温度が正常値に戻りました。現在の温度は"
                    << std::fixed << std::setprecision(2) << temp
                    << "度です。";
            postMessage(message.str().c_str());

            message_sent = false;
        }
    }
}

// 1日1回温度を送信
void sendDailyMessage() {
    // 1日1回送信するメッセージ
    static std::ostringstream message;
    // 1日1回送信済みならtrue
    static bool daily_sent = false;
    // 1時間1回のメッセージ作成済みならtrue
    static bool hourly_processed = false;
    // 現在時刻
    struct tm now;

    // 現在時刻を取得
    getLocalTime(&now);

    if (now.tm_min == 0) {
        // 1時間1回のメッセージを作成していなければ作成
        if (!hourly_processed) {
            message << (now.tm_year + 1900) << "-" << std::setfill('0')
                    << std::setw(2) << (now.tm_mon + 1) << "-"
                    << std::setw(2) << now.tm_mday << " "
                    << std::setw(2) << now.tm_hour << ":"
                    << std::setw(2) << now.tm_min << " "
                    << std::fixed << std::setprecision(2) << temp << "\\n";

            Serial.println(message.str().c_str());
            hourly_processed = true;
        }
    }
    else {
        hourly_processed = false;
    }

    if (now.tm_hour == DAILY_HOUR && now.tm_min == DAILY_MIN) {
        // 1日1回送信していなければメッセージを送信
        if (!daily_sent) {
            postMessage(message.str().c_str());

            // messageをクリア
            message.str("");
            message.clear(std::stringstream::goodbit);

            daily_sent = true;
        }
    }
    else {
        daily_sent = false;
    }
}

// バッテリーの状態を送信
void sendBatteryMessage() {
    // バッテリー稼働での警告を送信済みならtrue
    static bool battery_sent = false;

    // バッテリーの充放電電流を取得
    float bat_current = M5.Axp.GetBatCurrent();

    if (bat_current < 0) {
        if (!battery_sent) {
            postMessage("<!here> バッテリーでの稼働に切り替わりました。電源を確認してください。");
            battery_sent = true;
        }
    }
    else {
        if (battery_sent) {
            postMessage("電源に接続されました。");
            battery_sent = false;
        }
    }
}

// setup
void setup() {
    M5.begin();
    M5.Lcd.setRotation(1);
    Wire.begin(32, 33);

    // 画面の輝度を下げる
    M5.Axp.ScreenBreath(8);
    // CPUの動作周波数を80MHzに設定 (80MHz未満ではWi-Fi使用不可)
    setCpuFrequencyMhz(80);

    delay(100);

    // 無線LANへ接続
    connectWiFi(WIFI_SSID, WIFI_PASSPHRASE);
    // NTPの設定
    configTime(JST, 0, NTP_SERVER);

    // 温度を取得
    getTemp();

    // 起動メッセージを送信
    std::ostringstream message;
    message << "起動しました。アラートの閾値は"
            << std::fixed << std::setprecision(2) << TEMP_THRESHOLD
            << "度、現在の温度は"
            << std::fixed << std::setprecision(2) << temp
            << "度です。";
    postMessage(message.str().c_str());
}

// loop
void loop() {
    M5.update();

    // 電源ボタンが押されたらリセット
    if (M5.Axp.GetBtnPress() == AXP_WAS_PRESSED) {
        esp_restart();
    }

    // 温度を取得
    getTemp();

    M5.Lcd.fillScreen(TFT_BLACK);
    M5.Lcd.setCursor(5, 30);
    M5.Lcd.setTextFont(4);
    M5.Lcd.printf("Temp: %2.2f", temp);

    if (M5.BtnA.wasPressed()) {
        // ボタンAが押されたら温度を送信
        Serial.println("M5.BtnA.wasPressed");
        sendCurrentTemp();
    }

    // 1日1回温度を送信
    sendDailyMessage();
    // 現在の温度が閾値を超えていればメッセージを送信
    sendAlertMessage();
    // バッテリーの状態を送信
    sendBatteryMessage();

    delay(INTERVAL * 1000);
}
