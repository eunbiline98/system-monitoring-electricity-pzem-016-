#include <Arduino.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <ModbusMaster.h>
#include <WiFi.h>
#include <SPI.h>

//=========Pinout I/O===========
// RS485
#define MAX485_DE 26
#define MAX485_RE 25
#define MAX485_RO 33
#define MAX485_DI 27
#define PZEMSerial Serial2
// Relay
#define lampu 18
// sensor MCB
#define MCB_Sensor 14

#define led_data 2

//=========MQTT Config===========
const char *SSID = "BUSOL";
const char *PASS = "$ut0h0m312";

const char *MQTT_SERVER = "xxxxxx";
const char *MQTT_STATUS = "pow/edspert/status/cmnd";

const char *MQTT_ELECTRICITY_CMD = "pow/edspert/data/cmnd";
const char *MQTT_ELECTRICITY_STATE = "pow/edspert/data/stat";

const char *MQTT_RELAY_CMD = "relay/edspert/control/cmnd";
const char *MQTT_RELAY_STATE = "relay/edspert/control/stat";

const char *MQTT_CLIENT_ID = "edspert_Training";
const char *MQTT_USERNAME = "xxxx";
const char *MQTT_PASSWORD = "xxxx";

// PZEM-014
double Voltage_AC, Current_AC, Power_AC, Energy_AC, Freq_AC, PF_AC;
static uint8_t pzemSlaveAddr_pzem016 = 0x02; // input address sensor

// Millis
unsigned long startMillisPZEM;
unsigned long startMillis1;

unsigned long startMillisPublishData;
unsigned long currentMillisPublishData;

int ResetCounter = 0;
String ST_MCB, ST_LAMP;
uint8_t result;
uint32_t delayMS;

// Set IP Static
IPAddress ip(192, 168, 8, 159);
IPAddress dns(8, 8, 8, 8);
IPAddress gateway(192, 168, 8, 1);
IPAddress subnet(255, 255, 255, 0);

WiFiClient espClient;
PubSubClient client(espClient);
ModbusMaster node_pzem016;

void setup_wifi()
{
  WiFi.mode(WIFI_STA);
  WiFi.begin(SSID, PASS);
  // WiFi.config(ip, gateway, subnet);
  Serial.print("Connecting");
  while (WiFi.status() != WL_CONNECTED)
  {
    Serial.print(".");
    delay(250);
    digitalWrite(led_data, HIGH);
    delay(250);
    digitalWrite(led_data, LOW);
  }
  while (WiFi.waitForConnectResult() != WL_CONNECTED)
  {
    Serial.println("Connection Failed!");
    delay(5000);
    ESP.restart();
  }
  digitalWrite(led_data, LOW);
  Serial.println("");
  Serial.print("Connected to ");
  Serial.println(SSID);
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  Serial.print("MAC Address:  ");
  Serial.println(WiFi.macAddress());
  WiFi.setAutoReconnect(true);
  WiFi.persistent(true);
}

void preTransmission()
{
  if (millis() - startMillis1 > 5000)
  {
    digitalWrite(MAX485_RE, 1);
    digitalWrite(MAX485_DE, 1);
    delay(1);
  }
}

void postTransmission()
{
  if (millis() - startMillis1 > 5000)
  {
    delay(3);
    digitalWrite(MAX485_RE, 0);
    digitalWrite(MAX485_DE, 0);
  }
}

void initModbus()
{
  startMillis1 = millis();

  PZEMSerial.begin(9600, SERIAL_8N2, MAX485_RO, MAX485_DI);

  startMillisPZEM = millis();
  pinMode(MAX485_RE, OUTPUT);
  pinMode(MAX485_DE, OUTPUT);
  digitalWrite(MAX485_RE, 0);
  digitalWrite(MAX485_DE, 0);

  node_pzem016.preTransmission(preTransmission);
  node_pzem016.postTransmission(postTransmission);
  node_pzem016.begin(pzemSlaveAddr_pzem016, PZEMSerial);
  delay(1000);

  while (millis() - startMillis1 < 5000)
  {
    delay(500);
    digitalWrite(led_data, HIGH);
    delay(500);
    digitalWrite(led_data, LOW);
  }
}
void MCB_read()
{
  int state_MCB1 = digitalRead(MCB_Sensor);
  if (state_MCB1 == HIGH)
  {
    ST_MCB = "OFF";
  }
  else
  {
    ST_MCB = "ON";
  }
}

void powerRead()
{
  MCB_read();
  String rssi = String(WiFi.RSSI()).c_str();
  // node.clearResponseBuffer();
  String PZEM017_CHECK;
  result = node_pzem016.readInputRegisters(0x0000, 10);
  if (result == node_pzem016.ku8MBSuccess)
  {
    Voltage_AC = (node_pzem016.getResponseBuffer(0x00) / 10.0f);
    Current_AC = (node_pzem016.getResponseBuffer(0x01) / 1000.000f);
    Power_AC = (node_pzem016.getResponseBuffer(0x03) / 10.0f);
    Energy_AC = (node_pzem016.getResponseBuffer(0x05) / 1000.0f);
    Freq_AC = (node_pzem016.getResponseBuffer(0x07) / 10.0f);
    PF_AC = (node_pzem016.getResponseBuffer(0x08) / 100.0f);
  }
  else
  {
    Voltage_AC = 0;
    Current_AC = 0;
    Power_AC = 0;
    Freq_AC = 0;
    PF_AC = 0;
  }

  // publish data to MQTT
  StaticJsonBuffer<300> JSONbuffer;
  JsonObject &JSONencoder = JSONbuffer.createObject();

  JSONencoder["POWER"] = Power_AC;
  JSONencoder["VOLT"] = Voltage_AC;
  JSONencoder["AMP"] = Current_AC;
  JSONencoder["FREQ"] = Freq_AC;
  JSONencoder["PF"] = PF_AC;
  JSONencoder["ENERGY"] = Energy_AC;
  JSONencoder["MCB"] = ST_MCB;
  JSONencoder["LAMP"] = ST_LAMP;
  JSONencoder["RSSI"] = rssi;

  char JSONmessageBuffer_electric[300];
  JSONencoder.printTo(JSONmessageBuffer_electric, sizeof(JSONmessageBuffer_electric));
  client.publish(MQTT_ELECTRICITY_STATE, JSONmessageBuffer_electric);

  // Serial.println("Error sending message");

  Serial.println("Sending message to MQTT topic..");
  Serial.println(JSONmessageBuffer_electric);
}

void callback(char *topic, byte *message, unsigned int length)
{
  Serial.print("Message arrived in topic: ");
  Serial.println(topic);
  Serial.print("Message:");
  String messageTemp;

  for (int i = 0; i < length; i++)
  {
    Serial.print((char)message[i]);
    Serial.println();
    if ((char)message[i] != '"')
      messageTemp += (char)message[i];
  }

  if (String(topic) == MQTT_RELAY_CMD)
  {
    if (messageTemp == "1")
    {
      digitalWrite(lampu, LOW);
      ST_LAMP = "ON";
    }
    else if (messageTemp == "0")
    {
      digitalWrite(lampu, HIGH);
      ST_LAMP = "OFF";
    }
  }
}

void reconnect()
{
  // Loop until we're reconnected
  while (!client.connected())
  {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    if (client.connect(MQTT_CLIENT_ID, MQTT_USERNAME, MQTT_PASSWORD, MQTT_STATUS, 1, 1, "Offline"))
    {
      Serial.println("connected");
      // subscribe
      client.subscribe(MQTT_RELAY_CMD);
      client.publish(MQTT_STATUS, "Online", true);
    }
    else
    {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      ResetCounter++;
      if (ResetCounter >= 200)
      {
        ESP.restart();
        ResetCounter = 0;
      }
      delay(5000);
      digitalWrite(led_data, LOW);
    }
  }
}

void setup()
{
  Serial.begin(115200);
  pinMode(MCB_Sensor, INPUT);
  pinMode(lampu, OUTPUT);
  pinMode(led_data, OUTPUT);

  digitalWrite(lampu, HIGH);
  ST_LAMP = "ON";

  initModbus();
  delay(1000);

  setup_wifi();
  client.setCallback(callback);
  client.setServer(MQTT_SERVER, 1883);
}

void loop()
{
  digitalWrite(led_data, HIGH);
  if (!client.connected())
  {
    reconnect();
  }
  client.loop();
  currentMillisPublishData = millis();
  if (currentMillisPublishData - startMillisPublishData >= 5000)
  {
    powerRead();
    startMillisPublishData = millis();
  }
}