# m5stickc-temperature-alert

M5StickC で気温を監視し、閾値を超えたら Slack へアラートメッセージを送信

## 必要なもの

- M5StickC
- M5Stack用環境センサユニット ver.2 (ENV II) または M5Stack用温湿度気圧センサユニット Ver.3 (ENV III)

## 機能

- 10秒ごとに温度を測定し、閾値を超えたら Slack へアラートメッセージを送信 (測定間隔は変更可)
- 1時間1回記録した温度を1日1回決められた時刻に Slack へ送信
- 停電等によりバッテリーでの稼働に切り替わった場合 Slack へメッセージを送信
  (通信経路となる機器も UPS 等で電源が確保されている必要があります。)

## secrets.h の作成

secrets.h という名前でファイルを作成し
Wi-Fi の SSID とパスフレーズ、Slack のエンドポイントのパスを
書き込んでおく必要があります。

    // Wi-FiのSSID
    const char* WIFI_SSID = "Wi-FiのSSID";
    // Wi-Fiのパスフレーズ
    const char* WIFI_PASSPHRASE = "Wi-Fiのパスフレーズ";
    // Incoming Webhook URLのパス
    const char* WEBHOOK_PATH = "/services/xxxxx/xxxxx/xxxxx";
