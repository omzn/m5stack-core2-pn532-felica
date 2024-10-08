#include <M5Core2.h>

#define LGFX_M5STACK_CORE2
#define LGFX_USE_V1

#include <SPI.h>
// #include <Wire.h>

#include <PN532/PN532/PN532.h>
#include <PN532/PN532/PN532_debug.h>
// #include <PN532/PN532_SPI/PN532_SPI.h>
#include <PN532/PN532_I2C/PN532_I2C.h>

/* 配線図
  PN532      M5Stack Core2

   SDA        32
   SCL        33
   VCC        3.3V
   GND        GND
   IRQ        19
*/

/* I2Cは sda 32 scl 33 に接続．
   m5stack core2のデフォルトで Wire は 32, 33 が利用される．

    通信速度を50kHzにしないと失敗する．
    Wire.setClock(50000UL);
*/

#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include <DNSServer.h>
#include <HTTPClient.h>
#include <NimBLEDevice.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include <WiFiClient.h>
// #include <WiFiManager.h>  // https://github.com/tzapu/WiFiManager

#include <LGFX_AUTODETECT.hpp>
#include <LovyanGFX.hpp>

/*
#include "Map.h"
*/
#include "Character.h"
#include "Map.h"
#include "bitmaps.h"
#include "sound.h"

#define CONFIG_I2S_BCK_PIN 12
#define CONFIG_I2S_LRCK_PIN 0
#define CONFIG_I2S_DATA_PIN 2
#define CONFIG_I2S_DATA_IN_PIN 34

#define SPEAKER_I2S_NUMBER I2S_NUM_0

#define MODE_MIC 0
#define MODE_SPK 1

#define PIN_SPI_SS (27)
#define PIN_PN532_IRQ (19)

/* PN53X variables */

// PN532_SPI pn532spi(SPI, PIN_SPI_SS);
// PN532 nfc(pn532spi);
PN532_I2C pn532i2c(Wire);
PN532 nfc(pn532i2c);

uint8_t _prevIDm[8];
uint32_t nfcscan_timer = 0;

uint16_t systemCode = 0xFE00;
uint8_t requestCode = 0x01;  // System Code request
uint8_t idm[8];
uint8_t pmm[8];
uint16_t systemCodeResponse;
volatile boolean irq = false;

int message_timer = 0;
uint32_t wifi_connect_timer = 0;

/* LCD variables */

// static LGFX *lcd = &(M5.Lcd);
static LGFX *lcd = new LGFX();
LGFX_Sprite *sp320x64;

Character aquatan = Character(lcd, (unsigned char (*)[4][2048])aqua_bmp);
Map bg = Map(lcd, 10, 8, (unsigned char (*)[2048])bgimg);

uint8_t bgmap[8][10] = {
    { 2, 2, 2, 2, 2, 2, 2, 2, 2, 2},
    { 2, 2, 2, 2, 2, 2, 2, 2, 2, 2},
    { 5, 6, 6, 7, 1, 1, 5, 6, 6, 7},
    { 4, 21, 4, 4, 0, 0, 4, 21,  4, 4},
    {21, 22,21, 16, 0, 0, 4, 22, 21, 4},
    {12, 12, 9, 10, 13, 13, 11, 8, 12, 12},
    {23, 23, 14, 10, 13, 13, 11, 15, 23, 3},
    {14, 3, 15, 8, 12, 12, 9, 3, 3, 3}};

uint8_t collisionmap[8][10] = {
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {1, 1, 1, 1, 1, 1, 1, 1, 1, 1},
    {1, 1, 1, 1, 0, 0, 1, 1, 1, 1},
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 1},
    {0, 1, 0, 0, 0, 0, 0, 1, 1, 1}};

uint32_t move_timer = 0;
boolean touch = false;
boolean BtnA, BtnB, BtnC;

/* WiFi & net variables*/

HTTPClient http;
WiFiClient client;
WebServer webServer(80);
DNSServer dnsServer;
const IPAddress apIP(192, 168, 1, 1);
const char *apSSID = "NFC_LOCK";
boolean wifi_ap_mode = false;

/* Preferences */

Preferences prefs;
String hostname = "lock302";
String lock_mac = "c9:bb:1a:73:5d:0f";
String ssid = "hogehoge";
String wifipass = "foobar";
String room_name = "8-302";
String url_apiserver = "http://192.168.3.192:3001";
String on_unlock_devs = "";
String off_unlock_devs = "";
String on_lock_devs = "";
String off_lock_devs = "";
boolean use_beaconinfo = false;
int use_postdb = 1;

String str_student_id = "0";

/* BLE variables */

NimBLEScan *pBLEScan;

int lock_state = -1; // 000 Locked, 001 Unlocked
int prev_lock_state = 0;
String strLockState[8] = {"Locked", "Unlocked", "Locking", "Unlocking", "LockingStop", "UnlockingStop", "NotFullyLocked"};

/* Sounds */

enum { SE_MAC = 0,
       SE_PI,
       SE_PU,
       SE_PO,
       SE_WARN };
const unsigned char *wav_list[] = {macstartup, se_pi, se_pu, se_po, se_warn};
const size_t wav_size[] = {sizeof(macstartup), sizeof(se_pi), sizeof(se_pu), sizeof(se_po), sizeof(se_warn)};

void InitI2SSpeakerOrMic(int mode) {
    esp_err_t err = ESP_OK;

    i2s_driver_uninstall(SPEAKER_I2S_NUMBER);
    i2s_config_t i2s_config = {.mode = (i2s_mode_t)(I2S_MODE_MASTER),
                               .sample_rate = 16000,
                               .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
                               .channel_format = I2S_CHANNEL_FMT_ALL_RIGHT,
                               .communication_format = I2S_COMM_FORMAT_STAND_I2S,
                               .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
                               .dma_buf_count = 6,
                               .dma_buf_len = 60,
                               .use_apll = false,
                               .tx_desc_auto_clear = true,
                               .fixed_mclk = 0};
    if (mode == MODE_MIC) {
        i2s_config.mode =
            (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_PDM);
    } else {
        i2s_config.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
    }

    err += i2s_driver_install(SPEAKER_I2S_NUMBER, &i2s_config, 0, NULL);

    i2s_pin_config_t tx_pin_config = {
        .bck_io_num = CONFIG_I2S_BCK_PIN,
        .ws_io_num = CONFIG_I2S_LRCK_PIN,
        .data_out_num = CONFIG_I2S_DATA_PIN,
        .data_in_num = CONFIG_I2S_DATA_IN_PIN,
    };
    err += i2s_set_pin(SPEAKER_I2S_NUMBER, &tx_pin_config);

    if (mode != MODE_MIC) {
        err += i2s_set_clk(SPEAKER_I2S_NUMBER, 16000, I2S_BITS_PER_SAMPLE_16BIT,
                           I2S_CHANNEL_MONO);
    }

    i2s_zero_dma_buffer(SPEAKER_I2S_NUMBER);
}

void playSound(int type) {
    size_t bytes_written;
    M5.Axp.SetSpkEnable(true);
    InitI2SSpeakerOrMic(MODE_SPK);

    // Write Speaker
    i2s_write(SPEAKER_I2S_NUMBER, wav_list[type], wav_size[type], &bytes_written,
              portMAX_DELAY);
    i2s_zero_dma_buffer(SPEAKER_I2S_NUMBER);

    // Set Mic Mode
    InitI2SSpeakerOrMic(MODE_MIC);
    M5.Axp.SetSpkEnable(false);
}

// LockのBLE advertisement dataを取得して，鍵の状態を知る．
class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice *advertisedDevice) {
        // Serial.printf("%d: Found %s with RSSI %d \n", millis() -lastAdv, advertisedDevice->getAddress().toString().c_str(), advertisedDevice->getRSSI());
        if (advertisedDevice->haveManufacturerData() == true) {
            std::string strManufacturerData = advertisedDevice->getManufacturerData();
            uint8_t cManufacturerData[100];
            strManufacturerData.copy((char *)cManufacturerData, strManufacturerData.length(), 0);
            if (strManufacturerData[0] == 0x69 && strManufacturerData[1] == 0x09) {
                //                Serial.printf("strManufacturerData: %d ", strManufacturerData.length());
                lock_state = strManufacturerData[9] >> 4 & 0x07;
                // 000 : Locked   001: unlocked
                //                Serial.printf("lock_state = %s\n", strLockState[lock_state]);
            }
        }
    }
};

uint32_t drawBitmap16(unsigned char *data, int16_t x, int16_t y, int16_t w,
                      int16_t h) {
    uint16_t row, col, buffidx = 0;
    lcd->startWrite();
    for (col = 0; col < w; col++) {  // For each scanline...
        for (row = 0; row < h; row++) {
            uint16_t c = pgm_read_word(data + buffidx);
            c = ((c >> 8) & 0x00ff) | ((c << 8) & 0xff00);  // swap back and fore
            lcd->drawPixel(col + x, row + y, c);
            buffidx += 2;
        }  // end pixel
    }
    lcd->endWrite();
    return 0;
}

// 指定した範囲のmapを描画する．
void drawMap(uint8_t x, uint8_t y, uint8_t w, uint8_t h) {
    lcd->startWrite();
    for (int i = y; i < y + h; i++) {
        for (int j = x; j < x + w; j++) {
            drawBitmap16((unsigned char *)bgimg[bgmap[i][j]], j * 32, i * 32, 32, 32);
        }
    }
    lcd->endWrite();
}

void messageBox(int x, int y, String message, uint16_t fgcolor = TFT_WHITE, uint16_t bgcolor = TFT_NAVY) {
    sp320x64->fillSprite(TFT_TRANSPARENT);

    sp320x64->fillRoundRect(0, 0, 320, 64, 5, bgcolor);
    sp320x64->drawRoundRect(0, 0, 320, 64, 5, TFT_WHITE);
    sp320x64->drawRoundRect(1, 1, 318, 62, 5, TFT_WHITE);
    sp320x64->setFont(&fonts::Font4);
    sp320x64->setTextColor(fgcolor);

    sp320x64->drawString(message, sp320x64->width() / 2 - sp320x64->textWidth(message) / 2, sp320x64->height() / 2 - sp320x64->fontHeight() / 2);

    sp320x64->pushSprite(x, y, TFT_TRANSPARENT);
}

void handleStatus() {
    String message = "", argname, argv;
    DynamicJsonBuffer jsonBuffer;
    JsonObject &json = jsonBuffer.createObject();

    json["lock_state"] = lock_state >= 0 ? strLockState[lock_state] : "NotFound";
    json.printTo(message);
    webServer.send(200, "application/json", message);
}

void handleConfig() {
    String message, argname, argv;
    DynamicJsonBuffer jsonBuffer;
    JsonObject &json = jsonBuffer.createObject();

    for (int i = 0; i < webServer.args(); i++) {
        argname = webServer.argName(i);
        argv = webServer.arg(i);

        if (argname == "hostname") {
            hostname = argv;
            prefs.putString("hostname", hostname);
            WiFi.setHostname(hostname.c_str());
        } else if (argname == "lock_mac") {
            NimBLEAddress old_lock_addr(lock_mac.c_str(), 1);
            NimBLEDevice::whiteListRemove(old_lock_addr);
            lock_mac = argv;
            prefs.putString("lock_mac", lock_mac);
            NimBLEAddress new_lock_addr(lock_mac.c_str(), 1);
            NimBLEDevice::whiteListAdd(new_lock_addr);
        } else if (argname == "ssid") {
            ssid = argv;
            prefs.putString("ssid", ssid);
        } else if (argname == "wifipass") {
            wifipass = argv;
            prefs.putString("wifipass", wifipass);
        } else if (argname == "room_name") {
            room_name = argv;
            prefs.putString("room_name", room_name);
        } else if (argname == "url_apiserver") {
            url_apiserver = argv;
            prefs.putString("url_apiserver", url_apiserver);
        } else if (argname == "on_unlock") {
            on_unlock_devs = argv;
            prefs.putString("on_unlock", on_unlock_devs);
        } else if (argname == "on_lock") {
            on_lock_devs = argv;
            prefs.putString("on_lock", on_lock_devs);
        } else if (argname == "off_unlock") {
            off_unlock_devs = argv;
            prefs.putString("off_unlock", off_unlock_devs);
       } else if (argname == "off_lock") {
            off_lock_devs = argv;
            prefs.putString("off_lock", off_lock_devs);
         } else if (argname == "use_beaconinfo") {
            use_beaconinfo = (argv == "true");
            prefs.putBool("use_beaconinfo", use_beaconinfo);
        }
    }

    json["hostname"] = hostname;
    json["url_apiserver"] = url_apiserver;
    json["use_beaconinfo"] = use_beaconinfo ? "true" : "false";
    json["lock_mac"] = lock_mac;
    json["room_name"] = room_name;
    json["ssid"] = ssid;
    json["wifipass"] = "******";
    json.printTo(message);
    webServer.send(200, "application/json", message);
}

void handleReboot() {
    String message;
    message = "{reboot:\"done\"}";
    webServer.send(200, "application/json", message);
    delay(500);
    ESP.restart();
    while (1) {
        delay(0);
    }
}

String makePage(String title, String contents) {
    String s = R"=====(
<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<link rel="stylesheet" href="/pure.css">
)=====";
    s += "<title>";
    s += title;
    s += "</title></head><body>";
    s += contents;
    s += R"=====(
<div class="footer l-box">
<p>WiFiManager Lite by @omzn</p>
</div>
)=====";
    s += "</body></html>";
    return s;
}

void startWebServer() {
    webServer.on("/", handleStatus);
    webServer.on("/reboot", handleReboot);
    webServer.on("/status", handleStatus);
    webServer.on("/config", handleConfig);
    webServer.begin();
}

void startWebServer_ap() {
    //  webServer.on("/pure.css", handleCss);
    webServer.on("/setap", []() {
        ssid = webServer.arg("ssid");
        wifipass = webServer.arg("pass");
        hostname = webServer.arg("site");
        prefs.putString("hostname", hostname);
        prefs.putString("ssid", ssid);
        prefs.putString("wifipass", wifipass);

        String s = "<h2>Setup complete</h2><p>Device will be connected to \"";
        s += ssid;
        s += "\" after restart.</p><p>Your computer also need to re-connect to "
             "\"";
        s += ssid;
        s += R"=====(
".</p><p><button class="pure-button" onclick="return quitBox();">Close</button></p>
<script>function quitBox() { open(location, '_self').close();return false;};setTimeout("quitBox()",10000);</script>
)=====";
        webServer.send(200, "text/html", makePage("Wi-Fi Settings", s));
        ESP.restart();
        while (1) {
            delay(0);
        }
    });
    webServer.onNotFound([]() {
        int n = WiFi.scanNetworks();
        delay(100);
        String ssidList = "";
        for (int i = 0; i < n; ++i) {
            ssidList += "<option value=\"";
            ssidList += WiFi.SSID(i);
            ssidList += "\">";
            ssidList += WiFi.SSID(i);
            ssidList += "</option>";
        }
        String s = R"=====(
<div class="l-content">
<div class="l-box">
<h3 class="if-head">WiFi Setting</h3>
<p>Please enter your password by selecting the SSID.<br />
You can specify site name for accessing a name like http://aquamonitor.local/</p>
<form class="pure-form pure-form-stacked" method="get" action="setap" name="tm"><label for="ssid">SSID: </label>
<select id="ssid" name="ssid">
)=====";
        s += ssidList;
        s += R"=====(
</select>
<label for="pass">Password: </label><input id="pass" name="pass" length=64 type="password">
<label for="site" >Site name: </label><input id="site" name="site" length=32 type="text" placeholder="Site name">
<button class="pure-button pure-button-primary" type="submit">Submit</button></form>
</div>
</div>
)=====";
        webServer.send(200, "text/html", makePage("Wi-Fi Settings", s));
    });
    webServer.begin();
}

boolean connectWifi() {
    boolean state = true;
    int i = 0;

    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), wifipass.c_str());
    Serial.println("Connecting to WiFi");

    // Wait for connection
    Serial.print("Connecting...");
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
        if (i > 30) {
            state = false;
            break;
        }
        i++;
    }
    Serial.println("");
    if (state) {
        Serial.print("Connected to ");
        Serial.println(ssid);
        Serial.print("IP address: ");
        Serial.println(WiFi.localIP());
    } else {
        Serial.println("Connection failed.");
    }
    return state;
}

void post_alive() {
    char params[128];
    static int fail_count = 0;

    String apiurl = url_apiserver + "/aqua/alive";
    String ipaddress = WiFi.localIP().toString();

    Serial.println(apiurl);

    http.begin(client, apiurl.c_str());
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    sprintf(params, "ipaddress=%s&label=%s", ipaddress.c_str(),
            hostname.c_str());
    int httpCode = http.POST(params);
    if (httpCode > 0) {
        Serial.printf("[HTTP] POST alive... code: %d\n", httpCode);
        Serial.printf("[HTTP] url: %s params: %s\n", apiurl.c_str(), params);
        if (fail_count > 0 ) {
            message_timer = millis();
            fail_count = 0;
        }
    } else {
        Serial.printf(
            "[ERROR] POST alive... no connection or no HTTP server: %s\n",
            http.errorToString(httpCode).c_str());
        Serial.printf("[ERROR] url: %s params: %s\n", apiurl.c_str(), params);
        fail_count++;
        messageBox(0, 0, "No WiFi connection", TFT_WHITE, TFT_MAGENTA);
        if (fail_count > 2) { // 仏の顔も三度
            ESP.restart();
            while(1) {
                delay(1);
            }
        }
    }
    http.end();
}

void post_note(String label, String value) {
    char params[128];
    String apiurl = url_apiserver + "/aqua/add";
    Serial.println(apiurl);
    http.begin(client, apiurl.c_str());
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    sprintf(params, "label=%s&note=%s", label.c_str(), value.c_str());
    int httpCode = http.POST(params);
    if (httpCode > 0) {
        Serial.printf("[HTTP] POST note... code: %d\n", httpCode);
        Serial.printf("[HTTP] url: %s params: %s\n", apiurl.c_str(), params);
    } else {
        Serial.printf(
            "[ERROR] POST note... no connection or no HTTP server: %s\n",
            http.errorToString(httpCode).c_str());
        Serial.printf("[ERROR] url: %s params: %s\n", apiurl.c_str(), params);
    }
    http.end();
}
int check_room(uint32_t student_id, boolean use_beacon = false) {
    char params[128];
    int is_authorized = -1;
    sprintf(params, "%s/%8d", room_name, student_id);
    String command = (use_beacon ? "roombeacon" : "room");
    String apiurl = url_apiserver + "/lock/" + command + "/" + String(params);
    Serial.println(apiurl);
    http.begin(client, apiurl.c_str());
    int httpCode = http.GET();
    if (httpCode > 0) {
        Serial.printf("[HTTP] GET /lock/%s ... code: %d\n", command.c_str(), httpCode);
        Serial.printf("[HTTP] url: %s \n", apiurl.c_str());
        is_authorized = http.getString().toInt();
    } else {
        Serial.printf(
            "[ERROR] GET /lock/%s... no connection or no HTTP server: %s\n",
            command.c_str(),
            http.errorToString(httpCode).c_str());
        Serial.printf("[HTTP] url: %s \n", apiurl.c_str());
    }
    http.end();
    return is_authorized;
}

void post_lock(int command, String device) {  
    // command: 0 : currently locked, then unlock,   
    //          1 : currently unlocked, then lock
    char params[128];

    String apiurl = url_apiserver + "/lock/" + (command == 0 ? "unlock" : "lock");
    Serial.println(apiurl);
    http.begin(client, apiurl.c_str());
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    sprintf(params, "id=%s", device.c_str());
    for (int i = 0; i < 3; i++) {
        int httpCode = http.POST(params);
        if (httpCode > 0) {
            Serial.printf("[HTTP] POST lock ... code: %d\n", httpCode);
            Serial.printf("[HTTP] url: %s params: %s\n", apiurl.c_str(), params);
            break;
        } else {
            Serial.printf(
                "[ERROR] POST lock... no connection or no HTTP server: %s\n",
                http.errorToString(httpCode).c_str());
            Serial.printf("[ERROR] url: %s params: %s\n", apiurl.c_str(), params);
            if (i < 2) {
                delay(500);
            } else {
                messageBox(0, 0, "Failed to POST", TFT_WHITE, TFT_MAGENTA);
            }
        }
    }
    http.end();
}

void post_relay(int command, String api) {  
    // command: 0 : off  api /relay/:id
    //          1 : on
    char params[128];
    String apiurl = url_apiserver + api ;
    Serial.println(apiurl);
    http.begin(client, apiurl.c_str());
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    sprintf(params, "opr=%s", command ? "on" : "off");
    for (int i = 0; i < 3; i++) {
        int httpCode = http.POST(params);
        if (httpCode > 0) {
            Serial.printf("[HTTP] POST relay ... code: %d\n", httpCode);
            Serial.printf("[HTTP] url: %s params: %s\n", apiurl.c_str(), params);
            break;
        } else {
            Serial.printf(
                "[ERROR] POST relay... no connection or no HTTP server: %s\n",
                http.errorToString(httpCode).c_str());
            Serial.printf("[ERROR] url: %s params: %s\n", apiurl.c_str(), params);
            if (i < 2) {
                delay(500);
            } else {
                messageBox(0, 0, "Failed to POST", TFT_WHITE, TFT_MAGENTA);
            }
        }
    }
    http.end();
}

String mac2deviceid(String mac) {
    String deviceid = "";
    mac.toUpperCase();
    for (int i = 0; i < mac.length(); i++) {
        if (mac[i] != ':') {
            if ((mac[i] >= '0' && mac[i] <= '9') || (mac[i] >= 'A' && mac[i] <= 'F')) {
                deviceid += mac[i];
            } else {
                return "";
            }
        }
    }
    return deviceid;
}

void cardreading() {
    Serial.println("INT");
    //    if ((millis() - nfcscan_timer) > 5000) {
    irq = true;
    //    }
}


void setup(void) {
    // put your setup code here, to run once:
    pinMode(PIN_PN532_IRQ, INPUT_PULLUP);
    M5.begin();

    prefs.begin("aquatan", false);
    hostname = prefs.getString("hostname", hostname);
    url_apiserver = prefs.getString("url_apiserver", url_apiserver);
    lock_mac = prefs.getString("lock_mac", lock_mac);
    ssid = prefs.getString("ssid", ssid);
    wifipass = prefs.getString("wifipass", wifipass);
    room_name = prefs.getString("room_name", room_name);
    use_beaconinfo = prefs.getBool("use_beaconinfo", use_beaconinfo);
    on_lock_devs = prefs.getString("on_lock", on_lock_devs);
    on_unlock_devs = prefs.getString("on_unlock", on_unlock_devs);
    off_lock_devs = prefs.getString("off_lock", off_lock_devs);
    off_unlock_devs = prefs.getString("off_unlock", off_unlock_devs);

    lcd->init();
    lcd->setFont(&fonts::Font2);
    lcd->setBrightness(128);
    bg.setMapData(bgmap);
    bg.drawEntireMap();

    sp320x64 = new LGFX_Sprite(lcd);
    sp320x64->createSprite(320,64);

    aquatan.start(160 - 32, 120 + 64, ORIENT_FRONT);
    aquatan.setMap(&bg);

    Serial.begin(115200);
    Serial.println("Hello!");

    NimBLEAddress lock_addr(lock_mac.c_str(), 1);
    NimBLEDevice::init("");
    NimBLEDevice::whiteListAdd(lock_addr);
    pBLEScan = NimBLEDevice::getScan();
    pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks(), true);
    pBLEScan->setFilterPolicy(BLE_HCI_SCAN_FILT_USE_WL);
    pBLEScan->setMaxResults(0);  // do not store the scan results, use callback only.

    messageBox(0, 0, "Connecting WiFi");
    if (!connectWifi()) {
        messageBox(0, 0, "Connect SSID:" + String(apSSID));
        WiFi.mode(WIFI_AP);
        Serial.println("Wifi AP mode");
        WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
        WiFi.softAP(apSSID);
        dnsServer.start(53, "*", apIP);
        wifi_ap_mode = true;
    } else {
        // WiFi.setSleep(false);
        ArduinoOTA.setHostname(hostname.c_str());
        ArduinoOTA
            .onStart([]() {
                String type;
                if (ArduinoOTA.getCommand() == U_FLASH)
                    type = "sketch";
                else  // U_SPIFFS
                    type = "filesystem";
                // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS
                // using SPIFFS.end()
                Serial.println("Start updating " + type);
            })
            .onEnd([]() {
                Serial.println("\nEnd");
            })
            .onProgress([](unsigned int progress, unsigned int total) {
                Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
            })
            .onError([](ota_error_t error) {
                Serial.printf("Error[%u]: ", error);
                if (error == OTA_AUTH_ERROR)
                    Serial.println("Auth Failed");
                else if (error == OTA_BEGIN_ERROR)
                    Serial.println("Begin Failed");
                else if (error == OTA_CONNECT_ERROR)
                    Serial.println("Connect Failed");
                else if (error == OTA_RECEIVE_ERROR)
                    Serial.println("Receive Failed");
                else if (error == OTA_END_ERROR)
                    Serial.println("End Failed");
            });
        ArduinoOTA.begin();
        bg.drawMap(0, 0, 10, 2);
    }

    /* PN53x borad initialize */

    nfc.begin();
    Wire.setClock(50000UL);
    uint32_t versiondata = nfc.getFirmwareVersion();
    if (!versiondata) {
        Serial.print("Didn't find PN53x board");
        messageBox(0, 0, "No PN53x found.", TFT_WHITE, TFT_MAGENTA);
        while (1) {
            delay(10);
        };  // halt
    } else {
        Serial.print("Found chip PN5");
        Serial.println((versiondata >> 24) & 0xFF, HEX);
        Serial.print("Firmware ver. ");
        Serial.print((versiondata >> 16) & 0xFF, DEC);
        Serial.print('.');
        Serial.println((versiondata >> 8) & 0xFF, DEC);
        nfc.setPassiveActivationRetries(0xFF);
        nfc.SAMConfig();
    }
    // Set the max number of retry attempts to read from a card
    // This prevents us from waiting forever for a card, which is
    // the default behaviour of the PN532.

    memset(_prevIDm, 0, 8);
    nfc.felica_WaitingCard(systemCode, requestCode, idm, pmm, &systemCodeResponse);
    attachInterrupt(PIN_PN532_IRQ, cardreading, FALLING);

    InitI2SSpeakerOrMic(MODE_MIC);
    if (!wifi_ap_mode) {
        startWebServer();
        bg.drawMap(0, 0, 10, 2);
    } else {
        startWebServer_ap();
        wifi_connect_timer = millis();
    }
}

void keepalive(int seconds) {
    static uint32_t keepalive_timer = 0;
    if (millis() - keepalive_timer > seconds * 1000) {
        post_alive();
        keepalive_timer = millis();
    }
}

void relaydevices(String devs, int state) {
    int idx;
    int prev_idx = 0;
    if (devs != "") {
        do {
            idx = devs.indexOf(",", prev_idx);
            if (idx < 0) {
                idx = devs.length();
            }
            String dev = devs.substring(prev_idx, idx);
            if (dev != "") {
                post_relay(state,dev);
            }
            prev_idx = idx + 1;
      } while (idx < devs.length());
    }
}

void loop(void) {
    int8_t ret;

    webServer.handleClient();
    if (wifi_ap_mode) {
        dnsServer.processNextRequest();
        if (millis() - wifi_connect_timer > 90 * 1000 ) {
            ESP.restart();
            while (1) {
                delay(1);
            }
        }
        return;
    } else {
        ArduinoOTA.handle();
    }

    keepalive(60);

    if (pBLEScan->isScanning() == false) {
        pBLEScan->start(0, nullptr, false);
        lock_state = -1;
    }

    TouchPoint_t pos = M5.Touch.getPressPoint();
    if (pos.x != -1) {
        if (!touch) {
            touch = true;
            if (pos.y > 240) {
                // Yが240以上はボタンのエリア
                Serial.println("Button Area");
                if (pos.x < 109) {
                    BtnA = true;
                    playSound(SE_PU);
                } else if (pos.x > 218) {
                    BtnC = true;
                    playSound(SE_PU);
                } else if (pos.x >= 109 && pos.x <= 218) {
                    BtnB = true;
                    playSound(SE_PU);
                }
            }
            if (pos.x >= 0 && pos.x < 320 && pos.y < 240 && pos.y > 96) {
                if (pos.x >= aquatan.current_x() && pos.x <= aquatan.current_x() + aquatan.current_width() 
                    && pos.y >= aquatan.current_y() && pos.y <= aquatan.current_y() + aquatan.current_height()) {
                    if (lock_state == 1) {
                        messageBox(0, 0, "Lock");
                        /* 施錠のみ */
                        aquatan.clearActionQueue();
                        aquatan.queueMoveTo(128, 96, 2, 4);
                        aquatan.queueAction(STATUS_TOUCH, 0, 0);
                    }
                } else {
                    int to_x = pos.x - pos.x % 32;
                    int to_y = pos.y - pos.y % 32;
                    aquatan.queueMoveTo(to_x, to_y, 4, 4);
                }
                playSound(SE_PI);
            }
        }
    } else {
        if (touch) {
            touch = false;
        }
    }

    if (BtnA) {
        BtnA = false;
        Serial.println("Button A");
        messageBox(0, 0, "IP: " + WiFi.localIP().toString());
        message_timer = millis();
    }

    if (BtnB) {
        BtnB = false;
        if (lock_state == 1) {
            messageBox(0, 0, "Lock");
            /* 施錠のみ */
            aquatan.clearActionQueue();
            aquatan.queueMoveTo(128, 96, 2, 4);
            aquatan.queueAction(STATUS_TOUCH, 0, 0);
        }
    }
    if (BtnC) {
        BtnC = false;
        Serial.println("Button C");
        messageBox(0, 0, "MAC: " + lock_mac);
        message_timer = millis();
    }

    if (irq) {
        // Wait for a FeliCa type cards.
        // When one is found, some basic information such as IDm, PMm, and System Code are retrieved.
        Serial.print("Waiting for a FeliCa card...  ");
        ret = nfc.felica_ReadingCard(systemCode, requestCode, idm, pmm, &systemCodeResponse);

        if (ret != 1) {
            Serial.println("Could not find a card");
            delay(1);
            nfc.felica_WaitingCard(systemCode, requestCode, idm, pmm, &systemCodeResponse);
            irq = false;
            return;
        }

        if (memcmp(idm, _prevIDm, 8) == 0) {
            if ((millis() - nfcscan_timer) < 10000) {
                Serial.println("Same card");
                delay(1);
                nfc.felica_WaitingCard(systemCode, requestCode, idm, pmm, &systemCodeResponse);
                irq = false;
                return;
            }
        }

        nfcscan_timer = millis();

        Serial.println("Found a card!");
        Serial.print("  IDm: ");
        nfc.PrintHex(idm, 8);
        Serial.print("  PMm: ");
        nfc.PrintHex(pmm, 8);
        Serial.print("  System Code: ");
        Serial.print(systemCodeResponse, HEX);
        Serial.print("\n");
        //        lcd->setCursor(100, 100);
        //        lcd->println("Hello World");

        memcpy(_prevIDm, idm, 8);

        uint8_t blockData[4][16];
        uint16_t serviceCodeList[1];
        uint16_t blockList[4];

        Serial.print("Read Without Encryption command -> ");
        serviceCodeList[0] = 0x1A8B;
        blockList[0] = 0x8000;
        blockList[1] = 0x8001;
        blockList[2] = 0x8002;
        blockList[3] = 0x8003;
        delay(1);
        ret = nfc.felica_ReadWithoutEncryption(1, serviceCodeList, 4, blockList, blockData);
        if (ret != 1) {
            Serial.println("error");
            Serial.printf("  Error code: %d\n", ret);
        } else {
            Serial.println("OK!");
            Serial.print("  Student ID: ");
            uint32_t student_id = 0;
            str_student_id = "";
            for (int i = 0; i < 8; i++) {
                str_student_id += (char)blockData[0][6 + i];
            }
            student_id = str_student_id.toInt();

            playSound(SE_PU);

            Serial.print("  Name: ");
            for (int i = 0; i < 16; i++) {
                Serial.printf("%02X ", blockData[1][i]);
            }
            Serial.println();
            Serial.print("  Period of validity: ");
            for (int i = 0; i < 8; i++) {
                Serial.printf("%d", blockData[2][8 + i] & 0x0F);
            }
            Serial.print(" - ");
            for (int i = 0; i < 8; i++) {
                Serial.printf("%d", blockData[3][i] & 0x0F);
            }
            Serial.println();

            /* student_id が valid かどうかを labapi に問い合わせ */
            /* use_beaconinfo = true の場合は，ibeacon情報も含めて問い合わせ */
            int res = check_room(student_id, use_beaconinfo);
            if (res == 0) {
                messageBox(0, 0, str_student_id + ": " + (lock_state == 0 ? "Unlock" : "Lock"));
                /* student id が validでで，room_name に許可があれば あくあたんに解錠の動作をさせる．
                   動作キューをクリアし，解錠動作を入れる． */
                /* 解錠動作を先に行う． */
                String deviceid = mac2deviceid(lock_mac);
                if (deviceid != "") {
                    post_lock(lock_state, deviceid);
                    //playSound(SE_MAC);
                }
                //
                aquatan.clearActionQueue();
                aquatan.queueMoveTo(128, 96, 2, 4);
                aquatan.queueAction(STATUS_TOUCH, 0, 0);

            } else if (res == 1) {
                messageBox(0, 0, "No Aquatan beacon.", TFT_WHITE, TFT_RED);
                playSound(SE_WARN);
                message_timer = millis();
            } else if (res == 2) {
                messageBox(0, 0, "No permission.", TFT_WHITE, TFT_RED);
                playSound(SE_WARN);
                message_timer = millis();
            } else {
                messageBox(0, 0, "Server error.", TFT_WHITE, TFT_MAGENTA);
                playSound(SE_WARN);
                message_timer = millis();
            }
        }

        // waiting next card scan
        Serial.println("Card access completed!\n");
        nfc.felica_WaitingCard(systemCode, requestCode, idm, pmm, &systemCodeResponse);
        irq = false;
    }

    if (millis() - move_timer > 2500) {
        move_timer = millis();
        // ランダムに移動 未解錠時 0, 96, (240-64),(240-64) の範囲内
        if (aquatan.isEmptyQueue()) {
            if (random(100) > 50) {
                int dir = random(100);
                int16_t target_x, target_y;
                if (dir < 25) {  // 上
                    target_x = aquatan.current_x();
                    target_y = aquatan.current_y() - 32;
                } else if (dir < 50) {  // 下
                    target_x = aquatan.current_x();
                    target_y = aquatan.current_y() + 32;
                } else if (dir < 75) {  // 左
                    target_x = aquatan.current_x() - 32;
                    target_y = aquatan.current_y();
                } else {  // 右
                    target_x = aquatan.current_x() + 32;
                    target_y = aquatan.current_y();
                }
                aquatan.queueMoveTo(constrain(target_x, 0, 240 - 64),
                                    constrain(target_y, 96 + 32, 240 - 64));
            }
        }
    }
    if (aquatan.getStatus() == STATUS_WAIT) {
        aquatan.sleep();
        int retval = aquatan.dequeueAction();
        if (retval == STATUS_TOUCH) {
            /* 実際の解錠はあくあたんが動き終わった後に行われる． */
            // use switchbot lock api here
            /* 解錠動作は時間がかかるので，先に実行することにした．
            String deviceid = mac2deviceid(lock_mac);
            if (deviceid != "") {
                post_lock(lock_state, deviceid);
            }*/
            playSound(SE_MAC);
            message_timer = millis();
        }
    }

    if (message_timer > 0 && millis() - message_timer > 5000) {
        message_timer = 0;
        bg.drawMap(0, 0, 10, 2);
    }

    // 鍵を開けた場合は，扉のmapを床で置き換える
    if (lock_state != prev_lock_state) {
        if (prev_lock_state < 0) {
            message_timer = 1;
        }
        if (lock_state == 0) {  // locked
            bgmap[2][4] = bgmap[2][5] = 1;
            bgmap[3][4] = bgmap[3][5] = 0;
            bgmap[4][4] = bgmap[4][5] = 0;
            post_note("door_" + room_name + "_" + "lock", str_student_id);
//            post_relay(0,"/relay/2");
//            post_relay(0,"/relay/3");
//            post_relay(0,"/plug/1");
            relaydevices(off_lock_devs,0);
            relaydevices(on_lock_devs,1);
            str_student_id = "0";
        } else if (lock_state == 1) {  // unlocked
            bgmap[2][4] = bgmap[2][5] = 2;
            bgmap[3][4] = bgmap[3][5] = 2;
            bgmap[4][4] = bgmap[4][5] = 2;
            post_note("door_" + room_name + "_" + "unlock", str_student_id);
//            post_relay(1,"/relay/2");
//            post_relay(1,"/relay/3");
//            post_relay(1,"/plug/1");
            relaydevices(off_unlock_devs,0);
            relaydevices(on_unlock_devs,1);
            str_student_id = "0";
        } else if (lock_state == 2 || lock_state == 3) {  // locking or unlocking
            bgmap[2][4] = 19;
            bgmap[2][5] = 20;
            bgmap[3][4] = 17;
            bgmap[3][5] = 18;
            bgmap[4][4] = 17;
            bgmap[4][5] = 18;
        } else if (lock_state < 0) {
            messageBox(0, 0, "Cannot find lock.", TFT_WHITE, TFT_MAGENTA);
            prev_lock_state = lock_state;
        }
        prev_lock_state = lock_state;
        bg.drawMap(4, 2, 2, 3);
    }

    // delay(10);
    int d1 = 0;
    //    d1 += bg.update();
    d1 += aquatan.update();
    //    d1 += background.update();
    int dd = (1000 / FPS) - d1 > 0 ? (1000 / FPS) - d1 : 1;
    delay(dd);
}
