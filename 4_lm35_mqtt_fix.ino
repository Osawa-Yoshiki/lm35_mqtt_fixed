#define PIN1 A0     //温度センサー
#define PIN2 5      //モーター用その①
#define PIN3 6      //モーター用その②
#define PWM1 7      //モーター用その③
#define PIN4 2      //LED用
#define MEAN 100    //移動平均を出すまでのサンプリング数を指定します。
#define USERNAME "user20"

//#include <ArduinoJson.h>
#include <Ethernet.h>
#include <EthernetUdp.h>
#include <PubSubClient.h>                                  // MQTTクライアントライブラリ

//NTPサーバー用
unsigned int localPort = 8888;       // UDPパケット用ポート
char timeServer[] = "time.nist.gov"; // time.nist.gov NTPサーバー
const int NTP_PACKET_SIZE = 48; // NTPのタイムスタンプは最初の48バイト
byte packetBuffer[ NTP_PACKET_SIZE]; //NTPパケット用バッファ

//UDPインスタンス
EthernetUDP Udp;

//移動平均用
int cnt = 0;
int temp[MEAN+1] = {};
boolean calibration_done = false;

//時刻用
unsigned long epoch;

//MACアドレスとMQTTブローカー設定
byte mac[]    = {  0xDE, 0xED, 0xBA, 0x01, 0x05, 0x61 };    // 自身のEtherernet Sheild の MACアドレス に変更
byte server[]   = { 123, 123, 123, 123 };                   // Mosquittoが実行されているPCのIPアドレス に変更

//送信用JSONの初期設定
//DynamicJsonBuffer jsonBuffer;

//char json[] =
//  "{\"protocol\":\"1.0\",\"loginId\":\"ad\",\"name\":\"userXX\",\"template\":\"h\",\"status\":[{\"time\":\"\",\"values\":[{\"name\":\"temp\",\"type\":\"3\",\"value\":\"\"}]}]}";
//JsonObject& root = jsonBuffer.parseObject(json);
String json1 = "{\"protocol\":\"1.0\",\"loginId\":\"ad\",\"name\":\"";
String json2 = "\",\"template\":\"h\",\"status\":[{\"time\":\"";
String json3 = "\",\"values\":[{\"name\":\"temp\",\"type\":\"3\",\"value\":\"";
String json4 = "\"}]}]}";

void callback(char* topic, byte* payload, unsigned int length) {
  //メッセージ受信時にシリアルに表示する。
  Serial.println("message arrived!");

  //LEDをオンにする
  digitalWrite(PIN4, HIGH);

  //モーターをオンにする
  digitalWrite(PIN2, HIGH);
  digitalWrite(PIN3, LOW);
  analogWrite(PWM1, 250);

  //2秒待機してオフにする。
  delay(2000);
  digitalWrite(PIN4, LOW);
  analogWrite(PWM1, 0);
}

EthernetClient ethClient;           //イーサネットクライアントの初期化
PubSubClient client(ethClient);     //MQTTクライアントの初期化

//温度センサーの値を摂氏で取得
float get_temp() {
  int analogValue = analogRead(PIN1);
  float temp = ((analogValue * 5) / 1024.0) * 100;
  return temp;
}

void sendNTPpacket(char* address) {
  // set all bytes in the buffer to 0
  memset(packetBuffer, 0, NTP_PACKET_SIZE);
  // Initialize values needed to form NTP request
  // (see URL above for details on the packets)
  packetBuffer[0] = 0b11100011;   // LI, Version, Mode
  packetBuffer[1] = 0;     // Stratum, or type of clock
  packetBuffer[2] = 6;     // Polling Interval
  packetBuffer[3] = 0xEC;  // Peer Clock Precision
  // 8 bytes of zero for Root Delay & Root Dispersion
  packetBuffer[12]  = 49;
  packetBuffer[13]  = 0x4E;
  packetBuffer[14]  = 49;
  packetBuffer[15]  = 52;

  // all NTP fields have been given values, now
  // you can send a packet requesting a timestamp:
  Udp.beginPacket(address, 123); //NTP requests are to port 123
  Udp.write(packetBuffer, NTP_PACKET_SIZE);
  Udp.endPacket();
}


void setup() {
  Serial.begin(9600);
  Serial.println("ok");

  //MQTT送信用ユーザー名(JSON)
  //root["name"] = USERNAME;

  pinMode(PIN2, OUTPUT);
  pinMode(PIN3, OUTPUT);
  pinMode(PWM1, OUTPUT);

  client.setServer(server, 1883);
  client.setCallback(callback);

  Ethernet.begin(mac);                            //イーサネットの開始
  if (client.connect("arduinoClient")) {           //クライアント名"arduinoClient"で接続。
    client.publish("outTopic", "hello, mqtt!");    //outTopicに対し、"hello, mqtt!"を送信
    client.subscribe("Fab/#");                     //"Fab/#"でサブスクライブを開始
  }

  Udp.begin(localPort);                           //NTPサーバー用UDP通信開始
}

void loop() {
  if (calibration_done == true) {
    sendNTPpacket(timeServer); //NTPパケット送信
    delay(1000);  //1秒待機
    if (Udp.parsePacket()) {
      //UDPパケット読み込み
      Udp.read(packetBuffer, NTP_PACKET_SIZE); // read the packet into the buffer
      unsigned long highWord = word(packetBuffer[40], packetBuffer[41]);
      unsigned long lowWord = word(packetBuffer[42], packetBuffer[43]);
      unsigned long secsSince1900 = highWord << 16 | lowWord;
      //Serial.print("Seconds since Jan 1 1900 = ");
      //Serial.println(secsSince1900);

      //エポック秒へ変換
      //Serial.print("Unix time = ");
      const unsigned long seventyYears = 2208988800UL;
      epoch = secsSince1900 - seventyYears;
      //epoch += 32400;  //日本時間(UTC+9)
      //Serial.println(epoch);

      //時刻表示
      //Serial.print("The UTC time is ");
      Serial.print((epoch  % 86400L) / 3600); //時
      Serial.print(':');
      if (((epoch % 3600) / 60) < 10) { //一桁分の場合に'0'を付加
        Serial.print('0');
      }
      Serial.print((epoch  % 3600) / 60); //分
      Serial.print(':');
      if ((epoch % 60) < 10) { //一桁秒のばあいに'0'を付加
        Serial.print('0');
      }
    Serial.println(epoch % 60); // 秒
    Serial.println(epoch);
  }
    
  Ethernet.maintain();
  }
  
  //temp[MEAN-1]が0でない場合、キャリブレーションは完了していると判定しフラグを立てる
  if (temp[MEAN-1] != 0) {
    calibration_done = true;
  }

  //結果格納用変数
  float sum = 0.0;

  //cnt=MEAN、つまり配列の最後までいったらカウンタをゼロリセットする。
  if (cnt == MEAN) {
    cnt = 0;
  }
  temp[cnt] = get_temp() * 10;   //temp[cnt]に最新の温度を格納する
  cnt++;

  //配列の平均をとる。
  for (int i = 0; i < MEAN;i++) {
    sum += temp[i];
    //Serial.print(i);
    //Serial.print(":");
    //Serial.print(temp[i]);
    //Serial.print(" ");
  }
  float celsius = sum / MEAN / 10;

  //キャリブレーションが未完了の場合、以降の処理をスキップする
  if (calibration_done == true) {
    //Serial.print("Degree(C):   ");
    //Serial.println(celsius);
    //Serial.print("temp: ");
    //Serial.println(get_temp());

    String payload = json1 + USERNAME + json2 + String(epoch) + "000" + json3 + String(celsius) + json4;                                            //payloadを初期化
      
    int payload_length = payload.length() + 1;                      //payloadの長さ＋１
    //Serial.print("payload_length: ");
    //Serial.println(payload_length);
    Serial.println(payload);
    char payload_data[payload_length];                              //char型の配列を初期化
    payload.toCharArray(payload_data, payload_length);              //payloadをcharArrayに変換
    client.publish("Notify", payload_data);                         //MQTTブローカーへメッセージ送信  
    delay(1000);                                                    //1秒待機  
  } else {
    Serial.println("wait...");
  }

  client.loop();                                                  
}

