#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h> 
#include <Ticker.h>
#include <AsyncMqttClient.h>
#include <EEPROM.h>
#include <ArduinoOTA.h>
#include "BL0937.h"

// eeprom
#define MQTT_IP_OFFSET         0
#define MQTT_IP_LENGTH        16
#define MQTT_USER_OFFSET      16
#define MQTT_USER_LENGTH      32
#define MQTT_PASSWORD_OFFSET  48
#define MQTT_PASSWORD_LENGTH  32

// access point
#define AP_NAME "ZX-2820"
#define AP_TIMEOUT 300
#define MQTT_PORT 1883

// these are the nominal values [Ohm] for the resistors in the circuit
#define CURRENT_RESISTOR                0.001
#define VOLTAGE_RESISTOR_UPSTREAM       1000000
#define VOLTAGE_RESISTOR_DOWNSTREAM     1000

// pins
#define BUTTON_GPIO           3
#define BL0937_CF_GPIO        4
#define BL0937_CF1_GPIO       5
#define BL0937_SEL_GPIO_INV  12  // inverted
#define LED_GPIO_INV         13  // inverted
#define REL_GPIO             14

// BL0937
BL0937 bl0937;

// mqtt
IPAddress mqtt_server;
AsyncMqttClient mqttClient;
Ticker mqttReconnectTimer;

char mqtt_ip_pre[MQTT_IP_LENGTH] = "";
char mqtt_user_pre[MQTT_USER_LENGTH] = "";
char mqtt_password_pre[MQTT_PASSWORD_LENGTH] = "";

char mqtt_ip[MQTT_IP_LENGTH] = "";
char mqtt_user[MQTT_USER_LENGTH] = "";
char mqtt_password[MQTT_PASSWORD_LENGTH] = "";
char mqtt_id[30] = AP_NAME;

char topic_relais[30] = "/";
char topic_resenergy[30] = "/";
char topic_voltage[30] = "/";      // voltage (Spannung)
char topic_current[30] = "/";      // current (Strom)
char topic_actPower[30] = "/";     // active power (Wirkleistung)
char topic_reactPower[30] = "/";   // reactive power (Blindleistung)
char topic_appPower[30] = "/";     // apparent power (Scheinleistung)
char topic_energy[30] = "/";       // energy (Energie)
    
boolean mqtt_connected = false;
uint32 time_now;

// wifi
WiFiManager wifiManager;
WiFiEventHandler wifiConnectHandler;
WiFiEventHandler wifiDisconnectHandler;
Ticker wifiReconnectTimer;
char mac_str[13];

// when using interrupts we have to call the library entry point whenever an interrupt is triggered
void ICACHE_RAM_ATTR bl0937_cf1_interrupt()
{
  bl0937.cf1_interrupt();
}

void ICACHE_RAM_ATTR bl0937_cf_interrupt()
{
  bl0937.cf_interrupt();
}

String readEEPROM(int offset, int len)
{
  String res = "";
  for (int i = 0; i < len; ++i)
  {
    res += char(EEPROM.read(i + offset));
  }
  return res;
}

void writeEEPROM(int offset, int len, String value)
{
  for (int i = 0; i < len; ++i)
  {
    if (i < value.length())
    {
      EEPROM.write(i + offset, value[i]);
    }
    else
    {
      EEPROM.write(i + offset, 0x00);
    }
  }
}

void connectToWifi()
{
  Serial.println("Re-Connecting to Wi-Fi...");
  WiFi.setSleepMode(WIFI_NONE_SLEEP);
  WiFi.mode(WIFI_STA);
  WiFi.begin();
}

void onWifiConnect(const WiFiEventStationModeGotIP& event)
{
  Serial.println("Connected to Wi-Fi.");
  connectToMqtt();
}

void onWifiDisconnect(const WiFiEventStationModeDisconnected& event)
{
  Serial.println("Disconnected from Wi-Fi.");
  mqttReconnectTimer.detach(); // ensure we don't reconnect to MQTT while reconnecting to Wi-Fi
  wifiReconnectTimer.once(2, connectToWifi);
}

void connectToMqtt()
{
  Serial.println("Connecting to MQTT...");
  mqttClient.connect();
}

void onMqttConnect(bool sessionPresent)
{
  Serial.println("Connected to MQTT.");
  mqtt_connected = true;
  digitalWrite(LED_GPIO_INV, LOW);
  mqttClient.subscribe(topic_relais, 2);
  mqttClient.subscribe(topic_resenergy, 2);
}

void onMqttDisconnect(AsyncMqttClientDisconnectReason reason)
{
  mqttClient.unsubscribe(topic_resenergy);
  mqttClient.unsubscribe(topic_relais);
  digitalWrite(LED_GPIO_INV, HIGH);
  mqtt_connected = false;
  Serial.println("Disconnected from MQTT.");

  if (WiFi.isConnected())
  {
    mqttReconnectTimer.once(2, connectToMqtt);
  }
}

void onMqttMessage(char* topic, char* payload, AsyncMqttClientMessageProperties properties, size_t len, size_t index, size_t total)
{
  char pl[2];

  pl[0] = payload[0];
  pl[1] = 0;
  Serial.printf("Publish received. Topic: %s Payload: %s\n", topic, pl);

  if ((0 == strcmp(topic, topic_relais)) && (len == 1) && (total == 1))
  {
    if (0 == strcmp(pl, "0"))
    {
      // switch relais off
      digitalWrite(REL_GPIO, LOW);
    }
    else
    {
      // switch relais on
      digitalWrite(REL_GPIO, HIGH);
    }
  }

  if (0 == strcmp(topic, topic_resenergy))
  {
    bl0937.resetEnergy();
  }
}

void setup(void)
{  
  uint8_t mac[6];
  
  // init UART
  Serial.begin(115200);
  Serial.println();
  Serial.println();

  // init EEPROM
  EEPROM.begin(128);

  // init LED (off)
  pinMode(LED_GPIO_INV, OUTPUT);
  digitalWrite(LED_GPIO_INV, HIGH);

  // init relais (off)
  pinMode(REL_GPIO, OUTPUT);
  digitalWrite(REL_GPIO, LOW);

  // init button
  pinMode(BUTTON_GPIO, INPUT_PULLUP);

  // check if button is pressed
  if (LOW == digitalRead(BUTTON_GPIO))
  {
    Serial.println("reset wifi settings and restart.");
    wifiManager.resetSettings();
    delay(1000);
    ESP.restart();    
  }

  // init WIFI
  readEEPROM(MQTT_IP_OFFSET, MQTT_IP_LENGTH).toCharArray(mqtt_ip_pre, MQTT_IP_LENGTH);
  readEEPROM(MQTT_USER_OFFSET, MQTT_USER_LENGTH).toCharArray(mqtt_user_pre, MQTT_USER_LENGTH);
  readEEPROM(MQTT_PASSWORD_OFFSET, MQTT_PASSWORD_LENGTH).toCharArray(mqtt_password_pre, MQTT_PASSWORD_LENGTH);

  WiFiManagerParameter custom_mqtt_ip("ip", "MQTT ip", mqtt_ip_pre, MQTT_IP_LENGTH);
  WiFiManagerParameter custom_mqtt_user("user", "MQTT user", mqtt_user_pre, MQTT_USER_LENGTH);
  WiFiManagerParameter custom_mqtt_password("password", "MQTT password", mqtt_password_pre, MQTT_PASSWORD_LENGTH, "type=\"password\"");

  wifiManager.addParameter(&custom_mqtt_ip);
  wifiManager.addParameter(&custom_mqtt_user);
  wifiManager.addParameter(&custom_mqtt_password);

  WiFi.setSleepMode(WIFI_NONE_SLEEP);
  WiFi.mode(WIFI_STA);
  wifiManager.setConfigPortalTimeout(AP_TIMEOUT);
  wifiManager.setAPStaticIPConfig(IPAddress(192,168,1,1), IPAddress(192,168,1,1), IPAddress(255,255,255,0));
  if (!wifiManager.autoConnect(AP_NAME))
  {
    Serial.println("failed to connect and restart.");
    delay(1000);
    // restart and try again
    ESP.restart();
  }

  strcpy(mqtt_ip, custom_mqtt_ip.getValue());
  strcpy(mqtt_user, custom_mqtt_user.getValue());
  strcpy(mqtt_password, custom_mqtt_password.getValue());

  if ((0 != strcmp(mqtt_ip, mqtt_ip_pre)) ||
      (0 != strcmp(mqtt_user, mqtt_user_pre)) ||
      (0 != strcmp(mqtt_password, mqtt_password_pre)))
  {
    Serial.println("Parameters changed, need to update EEPROM.");
    writeEEPROM(MQTT_IP_OFFSET, MQTT_IP_LENGTH, mqtt_ip);
    writeEEPROM(MQTT_USER_OFFSET, MQTT_USER_LENGTH, mqtt_user);
    writeEEPROM(MQTT_PASSWORD_OFFSET, MQTT_PASSWORD_LENGTH, mqtt_password);

    EEPROM.commit();
  }
  
  wifiConnectHandler = WiFi.onStationModeGotIP(onWifiConnect);
  wifiDisconnectHandler = WiFi.onStationModeDisconnected(onWifiDisconnect);

  // construct MQTT topics with MAC
  WiFi.macAddress(mac);
  sprintf(mac_str, "%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  Serial.printf("MAC: %s\n", mac_str);

  strcat(topic_relais, mac_str);
  strcat(topic_relais, "/relais");
  strcat(topic_resenergy, mac_str);
  strcat(topic_resenergy, "/resenergy");
  
  strcat(topic_voltage, mac_str);
  strcat(topic_voltage, "/voltage");
  strcat(topic_current, mac_str);
  strcat(topic_current, "/current");
  strcat(topic_actPower, mac_str);
  strcat(topic_actPower, "/actPower");
  strcat(topic_reactPower, mac_str);
  strcat(topic_reactPower, "/reactPower");
  strcat(topic_appPower, mac_str);
  strcat(topic_appPower, "/appPower");
  strcat(topic_energy, mac_str);
  strcat(topic_energy, "/energy");

  if (mqtt_server.fromString(mqtt_ip))
  {
    strcat(mqtt_id, "-");
    strcat(mqtt_id, mac_str);
    mqttClient.onConnect(onMqttConnect);
    mqttClient.onDisconnect(onMqttDisconnect);
    mqttClient.onMessage(onMqttMessage);
    mqttClient.setServer(mqtt_server, MQTT_PORT);
    mqttClient.setCredentials(mqtt_user, mqtt_password);
    mqttClient.setClientId(mqtt_id);

    connectToMqtt();
  }
  else
  {
    Serial.println("invalid MQTT Broker IP.");
  }

  // init BL0937
  bl0937.begin(BL0937_CF_GPIO, BL0937_CF1_GPIO, BL0937_SEL_GPIO_INV, LOW, true);
  bl0937.setResistors(CURRENT_RESISTOR, VOLTAGE_RESISTOR_UPSTREAM, VOLTAGE_RESISTOR_DOWNSTREAM);
  attachInterrupt(BL0937_CF1_GPIO, bl0937_cf1_interrupt, FALLING);
  attachInterrupt(BL0937_CF_GPIO, bl0937_cf_interrupt, FALLING);

  // OTA update
  ArduinoOTA.setHostname(mqtt_id);
  ArduinoOTA.setPassword("esp8266");
  ArduinoOTA.onStart([]()
  {
    mqttClient.unsubscribe(topic_resenergy);
    mqttClient.unsubscribe(topic_relais);
    // switch relais off
    digitalWrite(REL_GPIO, LOW);
    digitalWrite(LED_GPIO_INV, HIGH);
  });
  ArduinoOTA.onEnd([]()
  {
    for (int i = 0; i < 40; i++)
    {
      digitalWrite(LED_GPIO_INV, !digitalRead(LED_GPIO_INV));
      delay(50);
    }
  });
  ArduinoOTA.begin();

  time_now = millis();
}

void loop(void)
{
  char str[12];

  if ((millis() - time_now) > 3000)
  {
    if (mqtt_connected == true)
    {
      unsigned int voltage = bl0937.getVoltage();
      double current = bl0937.getCurrent();
      unsigned int actPower = bl0937.getActivePower();
      unsigned int reactPower = bl0937.getReactivePower();
      unsigned int appPower = bl0937.getApparentPower();
      double energy = bl0937.getEnergy() / 3600.0 / 1000.0;
      
      Serial.print("[HLW] Voltage (V)         : "); Serial.println(voltage);
      Serial.print("[HLW] Current (A)         : "); Serial.println(current);
      Serial.print("[HLW] Active Power (W)    : "); Serial.println(actPower);
      Serial.print("[HLW] Reactive Power (W)  : "); Serial.println(reactPower);
      Serial.print("[HLW] Apparent Power (VA) : "); Serial.println(appPower);
      Serial.print("[HLW] Agg. energy (kWh)   : "); Serial.println(energy);
      Serial.println();
     
      snprintf(str, 12, "%d", voltage);
      mqttClient.publish(topic_voltage, 0, false, str);
      snprintf(str, 12, "%.2f", current);
      mqttClient.publish(topic_current, 0, false, str);
      snprintf(str, 12, "%d", actPower);
      mqttClient.publish(topic_actPower, 0, false, str);
      snprintf(str, 12, "%d", reactPower);
      mqttClient.publish(topic_reactPower, 0, false, str);
      snprintf(str, 12, "%d", appPower);
      mqttClient.publish(topic_appPower, 0, false, str);
      snprintf(str, 12, "%.3f", energy);
      mqttClient.publish(topic_energy, 0, false, str);
    }
    time_now = millis();
  }
  ArduinoOTA.handle();
  yield();
}
