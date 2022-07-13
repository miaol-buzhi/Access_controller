#include <Arduino.h>
#include <WiFiClient.h>
#include <NTPClient.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <string.h>
#include <EEPROM.h>
#include <Ticker.h>
/******************************************************************************************
 *                                   环境变量及引脚定义
******************************************************************************************/
#define EEPROM_SIZE       120    //申请120个EEPROM的内存
#define ssidaddress       0      //存放WIFI账号地址
#define passwordaddress   20     //存放账号密码地址

WiFiClient wificlient;                 //建立网络连接对象
PubSubClient mqttClient(wificlient);   //建立MQTT连接对象

Ticker readKey;                        //监控按键 是否长按 
Ticker keyDelayHandle;                 //按键延时处理函数
/******************** WIFI信息 *****************/
// const char* ssid = "360免费WiFi-CJ";
// const char* password = "147258369";

/******************** 服务器信息 ***************/
// const char *mqttServer = "47.98.243.24";    //MQQT服务端地址  
// const uint16_t mqttPort = 61613;              //端口号          
// const char *mqttUserName = "maosir";          //服务器连接用户名
// const char *mqttPassword = "1234@qwer";      //服务器连接密码 
/***************** 测试服 *************/
const char *mqttServer = "10.168.1.86";    //MQQT服务端地址  
const uint16_t mqttPort = 1883;              //端口号          
const char *mqttUserName = "maosir";          //服务器连接用户名
const char *mqttPassword = "1qaz2wsx";      //服务器连接密码 

//遗嘱相关信息
const char *willMsg = "OFFLINE";            //遗嘱主题信息
const int willQos = 0;                      //遗嘱Qos
const int willRetain = true;                //遗嘱保留
const int subQos = 1;                       //客户端订阅时使用Qos级别
const bool cleanSession = false;            //清除回话

/******************** 设备IO口选择及状态 ***********/
#define DCS_PIN       13             //电磁锁控制端口
#define DCS_BACK_PIN  16             //电磁锁反馈信号端口
#define WIFI_LED_PIN  4              //WIFI指示灯控制端口
#define WIFI_KEY_PIN  5              //寻网按键端口

String mac;                          //MAC地址
String ssid;                         //WIFI名称
String passWord;                     //WIFI密码
uint8_t smartConfigFlag = 0;
uint8_t DCS_BACK_Status = 0;         //电磁锁反馈信号状态
uint8_t KeyCnt = 0;                  //按键标志
uint8_t Door_Flag;
/******************** 函数申明 **********************/
void connectMQTTserver(void);                                   //连接MQTT服务函数
void receiveCallback(char* topic,byte* payload,int length);     //回调函数
void subscribeTopic(void);                                      //订阅主题函数
void pubMQTTmsg(String reportMsg);                              //消息发布函数
void publishOnlineStatus(void);                                 //发布在线状态
void connectWifi(void);                                         //WIFI连接
bool writeStrToFlash(uint16_t wAddress,String str,uint8_t len); //写入EEPROM函数
String readstrFromFlash(uint16_t rAddress,uint8_t len);         //读取EEPROM函数
void LED_1S_Display(void);                                      //LED每秒闪烁一次
void LED_500ms_Display(void);
void LED_250ms_Display(void);
void ReadKeyValue(void);
void KeyPress(void);
IRAM_ATTR void KeyInterrupt(void);
void SmartConfig(void);
void Close_Message(void);
/******************************************************************************************/

void setup() {
  Serial.begin(115200);    
  EEPROM.begin(EEPROM_SIZE);

  pinMode(DCS_PIN,OUTPUT);                  //引脚状态设置为输出模式
  pinMode(DCS_BACK_PIN,INPUT);             //引脚状态设置为输入模式
  pinMode(WIFI_LED_PIN,OUTPUT);            //引脚状态设置为输出模式
  pinMode(WIFI_KEY_PIN,INPUT_PULLUP);      //引脚状态设置为输入上拉模式
  attachInterrupt(digitalPinToInterrupt(WIFI_KEY_PIN), KeyInterrupt, FALLING);//作为本地开关使用
  digitalWrite(DCS_PIN,LOW);
  WiFi.mode(WIFI_STA);             //设置ESP8266工作模式为无限终端模式

  /*从EEPROM中获取密码及账号名称*/
  if((EEPROM.read(ssidaddress + 1) != 0)&&(EEPROM.read(ssidaddress) == 0x2A)){
    ssid = readstrFromFlash(ssidaddress,EEPROM.read(ssidaddress + 1));
    passWord = readstrFromFlash(passwordaddress,EEPROM.read(passwordaddress + 1));

    Serial.println(ssid);
    Serial.println(passWord);
  }

  connectWifi();       //连接网络

  mqttClient.setServer(mqttServer,mqttPort);     //设置MQTT服务器和端口号
  mqttClient.setCallback(receiveCallback);       //接受到信号后执行回调函数receiveCallback
  mqttClient.setKeepAlive(60);                   //设置心跳值为60S
  connectMQTTserver();                           //连接MQTT服务器
}

void loop() {
  if(smartConfigFlag == 1){
    Serial.println("smartconfig start...");
    SmartConfig();
  }
  if (!mqttClient.connected()) {                 // 如果控制板未能成功连接服务器，则继续尝试连接服务器
    connectMQTTserver();
  }
  mqttClient.loop();                             // 处理信息以及心跳
  DCS_BACK_Status = digitalRead(DCS_BACK_PIN);
  Serial.print("DCS_BACK_Status=");
  Serial.println(DCS_BACK_Status);
  // DCS_BACK_Status = !DCS_BACK_Status;
  if(DCS_BACK_Status == 1){
    Door_Flag = 1;
  }
  if(Door_Flag == 1 && DCS_BACK_Status == 0){
    Door_Flag = 0;    
    Close_Message();
  }
  else{}
}

/*触发按键中断后，开始按键定时计数*/ //IRAM_ATTR   或者ICACHE_RAM_ATTR  中断回调函数必须位于IRAM中
IRAM_ATTR void KeyInterrupt(void)
{
  if(smartConfigFlag == 0){
    keyDelayHandle.attach_ms(200,KeyPress);     //打开延时，10m后读取按键状态，消抖
  }
}

void KeyPress(void)
{
  if(digitalRead(WIFI_KEY_PIN) == 0){
    if(smartConfigFlag == 0){
      readKey.attach(1,ReadKeyValue);          //按键状态读取 （按键触发后开启,触发成功后关闭）
    }
  }
  keyDelayHandle.detach();                    //关闭此次延时定时
}
/**************** 寻网标志触发，进入寻网状态 *********************/
void ReadKeyValue(void)
{
  if(digitalRead(WIFI_KEY_PIN) == 0){
    if(++KeyCnt > 2){
      KeyCnt = 0;
      smartConfigFlag = 1;
      readKey.detach();         //关闭读取定时器
      Serial.println("start seacrh net ");
    }
  }
  else{
    KeyCnt = 0;
    readKey.detach();           //关闭读取定时器
  }
}

/**************** LED每秒闪烁一次 ******************/
void LED_1S_Display(void)
{
  digitalWrite(WIFI_LED_PIN,LOW);
  delay(1000);
  digitalWrite(WIFI_LED_PIN,HIGH);
  delay(1000);
}

/**************** LED每500ms闪烁一次 ******************/
void LED_500ms_Display(void)
{
  digitalWrite(WIFI_LED_PIN,LOW);
  delay(500);
  digitalWrite(WIFI_LED_PIN,HIGH);
  delay(500);
}

/**************** LED每250ms闪烁一次 ******************/
void LED_250ms_Display(void)
{
  digitalWrite(WIFI_LED_PIN,LOW);
  delay(250);
  digitalWrite(WIFI_LED_PIN,HIGH);
  delay(250);
}

/**************** ESP8266一键配网 ******************/
void SmartConfig(void)
{
  WiFi.mode(WIFI_STA);                         //设置WIFI为STA模式
  Serial.println("Waiting for connection");
  WiFi.beginSmartConfig();
  while(WiFi.status() != WL_CONNECTED){        //等待连接成功
    Serial.print(">");
    LED_500ms_Display();
    //step 1：接收消息成功
    //step 2：等待连接wifi时长15s
    //step 3：超时restart 重启
    //step 4：连接成功后，写入有效的ssid 和 pwd
    if(WiFi.smartConfigDone()){
      Serial.print("SmartConfig infor rec Success");
      for(int i = 0; i < 30; i++){
        Serial.print("-");
        LED_250ms_Display();
        if(WiFi.status() == WL_CONNECTED){
          smartConfigFlag = 0;
          Serial.print("WIFI CONNECTED OK!");
          break;
        }
        else{
          if(i == 29){
            Serial.println("WIFI connected fail : restart!!");
            ESP.restart();                 //如果未连接将重新启动
          }
        }
      }
    }
  }
  writeStrToFlash(ssidaddress,WiFi.SSID().c_str(),strlen(WiFi.SSID().c_str()));      //将WIFI名称写入EEPROM
  writeStrToFlash(passwordaddress,WiFi.psk().c_str(),strlen(WiFi.psk().c_str()));    //将WIFI密码写入EEPROM
  /* 读取数值 */  
  ssid = readstrFromFlash(ssidaddress,EEPROM.read(ssidaddress + 1));
  passWord = readstrFromFlash(passwordaddress,EEPROM.read(passwordaddress + 1));

  Serial.println(ssid);
  Serial.println(passWord);

    smartConfigFlag = 0;    
}
/**************** WIFI连接函数 *********************/
void connectWifi(void)
{
  WiFi.begin(ssid,passWord);
  while (WiFi.status() != WL_CONNECTED){
    if(smartConfigFlag == 1){
      break;
    }
    LED_1S_Display();         //LED 1S闪烁一次
    Serial.print(".");
  }
  WiFi.setSleepMode (WIFI_MODEM_SLEEP);
  Serial.println("WIFI_MODEM_SLEEP START");
  Serial.println("WiFi connected");
}

/**
 * @description: 向wAddress中写入长度为len的str
 * @param {uint16_t} wAddress 首地址
 * @param {String} str  写入字符串
 * @param {uint8_t} len 写入长度
 * @return {true false} 成功、失败
 */
bool writeStrToFlash(uint16_t wAddress,String str,uint8_t len){
  if((len+4) > EEPROM_SIZE){
    return false;                 //超长禁止写入 最大长度1K  1024个uint8_t
  }
  EEPROM.write(wAddress,0x2A);    //字头
  EEPROM.write(wAddress+1,str.length());  //写入长度为lenght()的字符串
  for (uint8_t i = 0; i < len; i++)
  {
    EEPROM.write(wAddress+2+i,str[i]);
  }
  EEPROM.commit();              //将写入的数据保存到EEPROM中
  /*检查写入的数据是否正确*/
  for (int i = 0; i < len; i++)
  {
    if(EEPROM.read(wAddress+2+i) !=str[i]){
      Serial.println("write false");
      return false;
    }
  }
  return true; 
}

/**
 * @description: 读取地址rAddress中长度为len的数据
 * @param {uint16_t} rAddress
 * @param {String} str
 * @param {uint8_t} len
 * @return {*}
 */
String readstrFromFlash(uint16_t rAddress,uint8_t len){
  String str;
  if (EEPROM.read(rAddress) != 0x2A)
  {
    Serial.println("read falsh1");
  }
  for(uint8_t i=0;i<EEPROM.read(rAddress+1);i++){
    str += char(EEPROM.read(rAddress+2+i));
  }
  return str;  
}

/*************连接MQTT服务器并订阅消息***************/
void connectMQTTserver(void)
{
  String ClientId = WiFi.macAddress();      //获取控制板ESP8266MAC地址
  ClientId.replace(":","");                 //去掉MAC地址中的：
  String willString = ClientId + "-Will";   //遗嘱主题名称，设备MAC+will
  char willTopic[willString.length() + 1];
  strcpy(willTopic,willString.c_str());

  if(mqttClient.connect(ClientId.c_str(), mqttUserName, mqttPassword)){
    Serial.print ("MQTT Server Connect ID.ClientID: ");
    Serial.println(ClientId);
    Serial.print("MQTT Server: ");
    Serial.println(mqttServer);
    subscribeTopic();                //订阅指定主题，客户端连接候就订阅这个主题   MAC这个主题
    digitalWrite(WIFI_LED_PIN,LOW);  //点亮LED指示灯
    digitalWrite(DCS_PIN,HIGH);      //合上电磁锁
  }
  else{
    Serial.print("MQTT Server Connect Failed. Client State:");
    Serial.println(mqttClient.state());
    delay(5000);
  }
}

/******************订阅指定主题********************/
void subscribeTopic(void)
{
  // 订阅Mac主题，只订阅自己MAC的板子
  String topicString = WiFi.macAddress();
  topicString.replace(":","");
  char subTopic[topicString.length() + 1];  
  strcpy(subTopic, topicString.c_str());          //将字符串转换为字符数组

  if(mqttClient.subscribe(subTopic,subQos)){
    Serial.print("Subscrib Topic:");
    Serial.print(subTopic );
    Serial.println("OK");
  } else {
    Serial.print("Subscribe subTopic Fail...");
  } 
}

/*********************发布在线状态***************************/
// 发布信息
void publishOnlineStatus(void)
{
  String willString = WiFi.macAddress() + "-Will";
  willString.replace(":","");
  char willTopic[willString.length() + 1];  
  strcpy(willTopic, willString.c_str());
  // 建立设备在线的消息。此信息将以保留形式向遗嘱主题发布
  String onlineMessageString = "ONLINE"; 
  char onlineMsg[onlineMessageString.length() + 1];   
  strcpy(onlineMsg, onlineMessageString.c_str());
  // 向遗嘱主题发布设备在线消息
  if(mqttClient.publish(willTopic, onlineMsg, true)){
    Serial.print("Published Online Message: ");
    Serial.println(onlineMsg);    
  } else {
    Serial.println("Online Message Publish Failed."); 
  }
}

/******************收到信息后执行回调函数********************/
void receiveCallback(char* topic,byte* payload,int length)
{
  String pubTopicMsg;   //{ "cm":"productService/open","mac":"A848FAC07128","event":"open","state":0}
  Serial.print("Message Receive [");
  Serial.print(topic);
  Serial.println("]");
  Serial.print("payload = ");
  for(int i = 0; i<length; i++){
    Serial.print((char)payload[i]);
  }
  DynamicJsonDocument docin(200);           //声明一个JSON对象
  deserializeJson(docin,payload);           //反序列化json数据,将接收的payload数据给到docin中
  JsonObject root = docin.as<JsonObject>();
  
  StaticJsonDocument <200> cdoc;            //声明一个JsonDocument对象

  String eventvalue = root["event"];
  String datavalue = root["state"];
 
  if(length>10)
  {
    String macString = WiFi.macAddress();   //获取MAC地址
    macString.replace(":","");              //去掉MAC中的
    cdoc["cm"] = "productService/open";
    cdoc["mac"] = macString;
    if(eventvalue == "open")
    {
      cdoc["event"] = "open";
      digitalWrite(DCS_PIN,LOW);     //打开电磁锁
      uint8_t DCS_Status = digitalRead(DCS_PIN);
      if(DCS_Status == 0)
      {
        cdoc["state"] = 0;
      }
      else{
        cdoc["state"] = 1;
      }
      serializeJson(cdoc, pubTopicMsg);   //序列化JSON数据（压缩形式），并从pubTopicMsg输出
      Serial.println(pubTopicMsg);
      pubMQTTmsg(pubTopicMsg);              //发布MQTT消息
      delay(5000);
      digitalWrite(DCS_PIN,HIGH);    //关闭电磁锁   

      DCS_BACK_Status = digitalRead(DCS_BACK_PIN);
      if(DCS_BACK_Status == 0)
      {
        Close_Message();
      }      
    }
    // serializeJson(cdoc, pubTopicMsg);   //序列化JSON数据（压缩形式），并从pubTopicMsg输出
    // Serial.println(pubTopicMsg);
    // pubMQTTmsg(pubTopicMsg);              //发布MQTT消息
  }     
}

/****************** 门关闭后返回关闭信息到服务 *******************/
void Close_Message(void)
{
  String CloseDoorMsg;
  StaticJsonDocument <120> cdoc;            //声明一个JsonDocument对象
  String macString = WiFi.macAddress();   //获取MAC地址
  macString.replace(":","");              //去掉MAC中的
  cdoc["cm"] = "productService/open";
  cdoc["mac"] = macString;
  cdoc["event"] = "close";
  delay(2000);
  if(digitalRead(DCS_BACK_PIN) == 0)
  {
    cdoc["state"] = 0;
  }
  else {
    cdoc["state"] = 1;
  }
  serializeJson(cdoc,CloseDoorMsg);
  Serial.println(CloseDoorMsg);
  pubMQTTmsg(CloseDoorMsg);
}
/******************发布消息********************/
void pubMQTTmsg(String reportMsg)
{
  //想主题xxxx发布消息
  String macString = WiFi.macAddress();        //获取MAC地址
  macString.replace(":","");

  String topicString = "maosir-server";
  char publishTopic[topicString.length() + 1];
  strcpy(publishTopic,topicString.c_str());

  Serial.println(reportMsg);
  char publishMsg[reportMsg.length() + 1];
  strcpy(publishMsg, reportMsg.c_str());

  //向主题发布信息
  if(mqttClient.publish(publishTopic,publishMsg)){
    Serial.print("Publish Report Topic:");
    Serial.println(publishTopic);
    Serial.print("Publish Report Message:");
    Serial.println(publishMsg);
  }
  else{
    Serial.println("Report Message Publish Failed.");
  }
}