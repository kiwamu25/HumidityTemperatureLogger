//Temperature and Humidity Data Logger
// ver 1.0.1.1
// 2019.01.31 K.Ishigame
// Project Code SI-E003
#include <ESP8266WiFi.h>


#include<ESP8266HTTPClient.h>
#include<EEPROM.h>
#include <DHT.h>
#include <DHT_U.h>

#define DHTPIN 4
#define ADC_EnablePIN 5

#define DHTTYPE DHT22   // DHT 22  (AM2302), AM2321
DHT dht(DHTPIN, DHTTYPE);


extern "C" {
#include "user_interface.h"
}


static const uint32_t USER_DATA_ADDR = 66;

// ハッシュ関数 (FNV) CRC でいいけどコード的に短いのでFNV
static uint32_t fnv_1_hash_32(uint8_t *bytes, size_t length)
{
  static const uint32_t FNV_OFFSET_BASIS_32 = 2166136261U;
  static const uint32_t FNV_PRIME_32 = 16777619U;
  uint32_t hash = FNV_OFFSET_BASIS_32;
  for (size_t i = 0 ; i < length ; ++i)
  {
    hash = (FNV_PRIME_32 * hash) ^ (bytes[i]);
  }
  return hash;
}

// struct の hash (先頭にあることを想定) を除くデータ部分のハッシュを計算する
template <typename T> uint32_t calc_hash(T& data)
{
  return fnv_1_hash_32(((uint8_t*)&data) + sizeof(data.hash), sizeof(T) - sizeof(data.hash));
};

struct {
  // retain data
  uint32_t hash;
  uint16_t count;
  uint8_t  send;
  uint16_t etc2;
} retain_data;





void post_sensor_data();
void SQL_Insert(float Humidity , float Temp);
void SQL_Insert_B();

const char* ssid = "";
const char* password = "";

//DeviceID
const int IDNo = 1;
const long Timer = 30 * 60 * 1000 * 1000;



void setup()
{
  pinMode(ADC_EnablePIN , OUTPUT);
  digitalWrite(ADC_EnablePIN, HIGH);

  Serial.begin(115200);
  Serial.println("");
  //温湿度モジュール初期化
  dht.begin();

  // データ読みこみ
  bool ok;
  ok = system_rtc_mem_read(USER_DATA_ADDR, &retain_data, sizeof(retain_data));
  if (!ok) {
    Serial.println("system_rtc_mem_read failed");
  }
  Serial.print("retain_data.count = ");
  Serial.println(retain_data.count);

  // ハッシュが一致していない場合、初期化されていないとみなし、初期化処理を行う
  uint32_t hash = calc_hash(retain_data);
  if (retain_data.hash != hash) {
    Serial.println("retain_data may be uninitialized");
    retain_data.count = 0;
    retain_data.send = 0;
  }

  // データの変更処理(任意)
  retain_data.count++;
  if (!retain_data.send) {
    // 4回に1度送信する
    retain_data.send = retain_data.count % 8 == 0;
  } else {
    retain_data.send = 0;
    // なんか定期的に書きこみたい処理
    post_sensor_data();
  }
  // 書きこみ処理。hash を計算していれておく
  retain_data.hash = hash = calc_hash(retain_data);
  ok = system_rtc_mem_write(USER_DATA_ADDR, &retain_data, sizeof(retain_data));
  if (!ok) {
    Serial.println("system_rtc_mem_write failed");
  }

  if (retain_data.send) {
    ESP.deepSleep(Timer, WAKE_RF_DEFAULT);
  } else {
    // sendしない場合は WIFI をオフで起動させる
    ESP.deepSleep(Timer, WAKE_RF_DISABLED);
  }

}

void loop()
{

}


void post_sensor_data()
{
  //ADCon許可出力ピンを設定する
  pinMode(ADC_EnablePIN , OUTPUT);


  //ADCon動作許可
  digitalWrite(ADC_EnablePIN , HIGH);
  //ADConの結果用変数初期化
  uint ADC_Value = 0;
  //AD変換開始
  ADC_Value = system_adc_read();
  //設定値を読込電圧値に変換
  float V_Value = (ADC_Value * 0.00097656) * 10;
  Serial.println("Battery : " + String(V_Value) + "V");

  //AD変換終了
  digitalWrite(ADC_EnablePIN , LOW);

  //温湿度を1度読み込む（安定用）
  dht.readHumidity();
  dht.readTemperature();
  float h = 0;
  float t = 0;
  //再度温湿度を読み込む
  delay(2000);
  h = dht.readHumidity();
  // Read temperature as Celsius (the default)
  t =  dht.readTemperature();

  int StateCounter = 0;
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
    StateCounter++;
    if (StateCounter >= 100)
    {
      return;
    }
  }
  //結果に問題がなければSQLに保存する
  //Wifi設定、および接続開始
  Serial.print("Humidity : ");
  Serial.println(h);
  Serial.print("temperatuer : ");
  Serial.println(t);
  SQL_Insert(h, t , V_Value);
}


HTTPClient http;
void SQL_Insert(float Humidity , float Temp , float Battery)
{
  //保存先Uri
  http.begin("http://***");
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  //ポスト用データの作成
  String postData = "DeviceID=";
  postData += IDNo;
  postData += "&Temperature=";
  postData += Temp;
  postData += "&Humidity=";
  postData += Humidity;
  postData += "&Battery=";
  postData += Battery;
  int httpCode = http.POST(postData);
  if (httpCode == HTTP_CODE_OK)
  {
    String payload = http.getString();
    Serial.println("[Http]POST...Complete");
  }
  else
  {
    Serial.printf("[HTTP]GET...failed, error: % s\n", http.errorToString(httpCode).c_str());
  }
  http.end();
}
