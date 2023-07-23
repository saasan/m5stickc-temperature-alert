#include <algorithm>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>
#include <M5StickC.h>
#include <SHT3X.h>
#include <WiFiClientSecure.h>
#include "encodeURIComponent.h"
#include "secrets.h"

// -----------------------------------------------------------------------------
// 設定
// -----------------------------------------------------------------------------
// 温度を測定する間隔(秒) ※60秒以上にしないこと
const uint32_t INTERVAL = 10;
// アラートメッセージを送信する気温
const float TEMP_THRESHOLD = 27.00;
// グラフを送信する時刻の時
const std::vector<int> SEND_HOUR = { 0, 6, 12, 18 };
// グラフを送信する時刻の分
const int SEND_MIN = 0;

// -----------------------------------------------------------------------------
// 構造体
// -----------------------------------------------------------------------------

struct dataset {
    float temp;
    float hum;
    std::string date;
};

// -----------------------------------------------------------------------------
// 定数
// -----------------------------------------------------------------------------
// Incoming Webhook URLのホスト名
const char* WEBHOOK_HOST = "hooks.slack.com";
// Incoming Webhookのポート番号
const uint16_t WEBHOOK_PORT = 443;
// グラフのURL
const char* CHART_URL = "https://quickchart.io/chart?w=720&h=360&bkg=%23FFFFFF&v=4&c=";
// グラフのJSON
const char* CHART_JSON[] = {
    R"({"type":"line","data":{"labels":[)",
    R"(],"datasets":[{"label":"温度","yAxisID":"t","borderColor":"#FF9F40","backgroundColor":"#FF9F40","data":[)",
    R"(]},{"label":"湿度","yAxisID":"h","borderColor":"#36A2EB","backgroundColor":"#36A2EB","data":[)",
    R"(,]}]},"options":{"scales":{"t":{"title":{"text":"温度","display":true},"position":"left","suggestedMin":20,"suggestedMax":40},"h":{"title":{"text":"湿度","display":true},"position":"left","suggestedMin":0,"suggestedMax":100}}}})"
};
// GMTからの時間差(秒)
const long JST = 9 * 60 * 60;
// NTPサーバ
const char* NTP_SERVER = "ntp.nict.jp";
// 電源ボタンが1秒未満押された
const uint8_t AXP_WAS_PRESSED = 2;

// -----------------------------------------------------------------------------
// 変数
// -----------------------------------------------------------------------------
// デジタル温湿度センサーSHT30
SHT3X sht30;
// 温度
float temp = 0.0;
// 湿度
float hum = 0.0;
// グラフのデータセット
std::vector<dataset> datasets;

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

// 無線LANが切れていたら再接続
void reconnectWiFi(const char* ssid, const char* passphrase) {
    if (WiFi.status() == WL_CONNECTED) return;
    connectWiFi(ssid, passphrase);
}

// Slackへ送信する
void postToSlack(const std::string& payload) {
    WiFiClientSecure client;

    // HTTPS接続時に証明書による検証を行わない。
    // ボードマネージャで表示されるM5Stackのバージョンが1.0.9の場合は
    // client.setInsecure();をコメントアウトする必要がある。
    // (setInsecureが存在しないためコンパイルエラーとなる)
    client.setInsecure();

    Serial.println("\nStarting connection to server...");
    if (!client.connect(WEBHOOK_HOST, WEBHOOK_PORT)) {
        Serial.println("Connection failed!");
    }
    else {
        Serial.println("Connected to server!");

        // リクエストを作成
        std::ostringstream request;
        request << "POST " << WEBHOOK_PATH << " HTTP/1.1\r\n"
                << "Host: " << WEBHOOK_HOST << "\r\n"
                << "User-Agent: WiFiClientSecure\r\n"
                << "Content-Type: application/x-www-form-urlencoded\r\n"
                << "Content-Length: " << payload.length() << "\r\n\r\n"
                << payload;

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
        while (client.available()) {
            char c = client.read();
            Serial.write(c);
        }

        client.stop();
    }
}

// Slackへメッセージを送信する
void postMessage(const char* message) {
    std::ostringstream encoded, payload;
    encodeURIComponent(message, encoded);
    payload << R"(payload={"text": ")" << encoded.str() << R"("})";

    postToSlack(payload.str());
}

// Slackへ画像を送信する
void postImage(const char* image_url) {
    std::ostringstream encoded, payload;
    encodeURIComponent(image_url, encoded);
    payload << R"(payload={"blocks":[{"type":"image","image_url":")"
            << encoded.str()
            << R"(","alt_text":"chart"}]})";

    postToSlack(payload.str());
}

// tmを MM/dd HH:mm 形式の文字列へ変換
std::string tm2str(struct tm& t) {
    std::ostringstream ss;
    ss << std::setfill('0') << std::right
       << std::setw(2) << (t.tm_mon + 1) << "/"
       << std::setw(2) << t.tm_mday << " "
       << std::setw(2) << t.tm_hour << ":"
       << std::setw(2) << t.tm_min;

    return ss.str();
}

// std::vector<dataset>をJSON形式の文字列へ変換
void dataset2json(const std::vector<dataset>& v, std::ostringstream& json) {
    std::ostringstream ss_date, ss_temp, ss_hum;

    for (const auto& i: v) {
        ss_date << "\"" <<  i.date << "\",";
        // 小数点以下2桁
        ss_temp << std::fixed << std::setprecision(2) <<  i.temp << ",";
        ss_hum << std::fixed << std::setprecision(2) <<  i.hum << ",";
    }

    json << CHART_JSON[0] << ss_date.str()
         << CHART_JSON[1] << ss_temp.str()
         << CHART_JSON[2] << ss_hum.str()
         << CHART_JSON[3];
}

// 温湿度を取得
void getTempHum() {
    if (sht30.get() == 0) {
        temp = sht30.cTemp;
        hum = sht30.humidity;
        Serial.printf("Temperature: %2.2f*C, Humidity: %2.2f%%\r\n", temp, hum);
    }
    else {
        Serial.println("sht30.get() failed");
    }
}

// 現在の温湿度を送信
void sendCurrentTempHum() {
    std::ostringstream message;
    message << "現在の温度は"
            << std::fixed << std::setprecision(2) << temp
            << "度、湿度は"
            << std::fixed << std::setprecision(2) << hum
            << "%です。";
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

// グラフを送信
void sendChart(const std::vector<dataset>& datasets) {
    // グラフのURL
    std::ostringstream url;
    // グラフのJSON
    std::ostringstream json;

    // グラフのJSONを作成
    dataset2json(datasets, json);

    // グラフのURLを作成
    url << CHART_URL;
    encodeURIComponent(json.str().c_str(), url);

    // メッセージを送信
    postImage(url.str().c_str());
}

// 1時間1回グラフのデータセットを追加
void addDatasetHourly() {
    // 1時間1回のデータを追加済みならtrue
    static bool hourly_processed = false;
    // 現在時刻
    struct tm now;

    // 現在時刻を取得
    getLocalTime(&now);

    if (now.tm_min == 0) {
        // 1時間1回のデータを追加していなければ追加
        if (!hourly_processed) {
            // 25時間分ある場合は削除
            if (datasets.size() >= 25) datasets.erase(datasets.begin());

            dataset new_data { temp, hum, tm2str(now) };
            datasets.push_back(new_data);

            hourly_processed = true;
        }
    }
    else {
        hourly_processed = false;
    }
}

// 設定時刻の場合にグラフを送信
void sendChartAtSpecificTime() {
    // グラフを送信済みならtrue
    static bool chart_sent = false;
    // 現在時刻
    struct tm now;

    // 現在時刻を取得
    getLocalTime(&now);

    if (now.tm_min == SEND_MIN && std::find(SEND_HOUR.begin(), SEND_HOUR.end(), now.tm_hour) != SEND_HOUR.end()) {
        // グラフを送信していなければ送信
        if (!chart_sent) {
            sendChart(datasets);

            chart_sent = true;
        }
    }
    else {
        chart_sent = false;
    }
}

// バッテリーの状態を送信
void sendBatteryMessage() {
    // バッテリー稼働での警告を送信済みならtrue
    static bool battery_sent = false;
    // 送信するメッセージ
    std::ostringstream message;
    std::ostringstream message_current;

    // バッテリーの充放電電流を取得
    float bat_current = M5.Axp.GetBatCurrent();
    // USB電源の電流を取得
    float vbus_current = M5.Axp.GetVBusCurrent();

    message_current << "バッテリー電流: "
                    << std::fixed << std::setprecision(2) << bat_current << " mA\\n"
                    << "USB電源電流: "
                    << std::fixed << std::setprecision(2) << vbus_current << " mA";
    Serial.println(message_current.str().c_str());

    if (bat_current < 0 && vbus_current <= 0) {
        if (!battery_sent) {
            message << "<!here> バッテリーでの稼働に切り替わりました。電源を確認してください。\\n"
                    << message_current.str();
            postMessage(message.str().c_str());
            battery_sent = true;
        }
    }
    else {
        if (battery_sent) {
            message << "電源に接続されました。\\n"
                    << message_current.str();
            postMessage(message.str().c_str());
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

    // 温湿度を取得
    getTempHum();

    // 起動メッセージを送信
    std::ostringstream message;
    message << "起動しました。アラートの閾値は"
            << std::fixed << std::setprecision(2) << TEMP_THRESHOLD
            << "度、現在の温度は"
            << std::fixed << std::setprecision(2) << temp
            << "度、湿度は"
            << std::fixed << std::setprecision(2) << hum
            << "%です。";
    postMessage(message.str().c_str());
}

// loop
void loop() {
    M5.update();

    // 電源ボタンが押されたらリセット
    if (M5.Axp.GetBtnPress() == AXP_WAS_PRESSED) {
        esp_restart();
    }

    // 無線LANが切れていたら再接続
    reconnectWiFi(WIFI_SSID, WIFI_PASSPHRASE);

    // 温湿度を取得
    getTempHum();

    M5.Lcd.fillScreen(TFT_BLACK);
    M5.Lcd.setCursor(5, 30);
    M5.Lcd.setTextFont(4);
    M5.Lcd.printf("Temp: %2.2f", temp);

    if (M5.BtnA.wasPressed()) {
        // ボタンAが押されたら温湿度を送信
        Serial.println("M5.BtnA.wasPressed");
        sendCurrentTempHum();
    }

    // 1時間1回グラフのデータセットを追加
    addDatasetHourly();
    // 設定時刻の場合にグラフを送信
    sendChartAtSpecificTime();
    // 現在の温度が閾値を超えていればメッセージを送信
    sendAlertMessage();
    // バッテリーの状態を送信
    sendBatteryMessage();

    delay(INTERVAL * 1000);
}
