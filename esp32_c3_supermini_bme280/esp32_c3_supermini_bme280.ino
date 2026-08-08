#include <WiFi.h>
#include <WiFiClient.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <MQTT.h>
#include "config.h"

// board: ESP-32 C3 supermini (ESP32C3 Dev Module)

static TwoWire I2CBME(0);
static Adafruit_BME280 Bme280;

static bool SensorDetected = false ;

// MQTT client instance
static MQTTClient MQclient;
static WiFiClient Net;

static const char *HATempDiscovery = "\
{\
	\"name\": \"Loft temperature\",\
	\"state_topic\": \"loft-climate/temperature\",\
	\"device_class\": \"temperature\",\
  \"unit_of_measurement\": \"°C\",\
	\"suggested_display_precision\": 1,\
	\"platform\": \"sensor\",\
	\"expire_after\": 300, \
  \"unique_id\": \"esp_loft_climate_temperature\" \
}" ;

static const char *HAHumidityDiscovery = "\
{\
	\"name\": \"Loft humidity\",\
	\"state_topic\": \"loft-climate/humidity\",\
	\"device_class\": \"humidity\",\
  \"unit_of_measurement\": \"%\",\
	\"suggested_display_precision\": 1,\
	\"platform\": \"sensor\",\
	\"expire_after\": 300, \
  \"unique_id\": \"esp_loft_climate_humidity\" \
}" ;

static void BrokerConnect(void)
{
  // connect to MQTT broker
  Serial.println("Connecting to broker");
  MQclient.begin(MQTT_BROKER,Net) ;
  while(!MQclient.connect("house-loft-climate-pub"))
  {
    delay(500);
    Serial.print(F("."));
  }
}

void setup() 
{
  IPAddress LocalIp ;

  Serial.begin(115200);

  Serial.println("Connecting to WiFi");
  WiFi.mode(WIFI_STA);
  WiFi.begin(SSID1, PWD1);
  // workaround https://forum.arduino.cc/t/no-wifi-connect-with-esp32-c3-super-mini/1324046/12
  WiFi.setTxPower(WIFI_POWER_8_5dBm);
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(F("."));
  }
  LocalIp = WiFi.localIP();
  Serial.println(F("WiFi connected"));
  Serial.println(LocalIp);

  I2CBME.begin(I2C_SDA, I2C_SCL, 100000);
  SensorDetected = Bme280.begin(IC2_SENSOR_ADDR,&I2CBME) ;
  
  if ( SensorDetected )
  {
    // connect to MQTT broker
    BrokerConnect() ;

    // publish HA auto-discovery topics for the sensor
    MQclient.publish("homeassistant/sensor/loft-climate/temperature/config",HATempDiscovery,true,0) ;
    MQclient.publish("homeassistant/sensor/loft-climate/humidity/config",HAHumidityDiscovery,true,0) ;

    Serial.println("Setup done");

    pinMode(STATUS_LED, OUTPUT);
    digitalWrite(STATUS_LED, LOW); 
  }
  else
    Serial.println("Failed to detect sensor");
}

void loop() 
{
  // Wait a few seconds between measurements.
  delay(5000);

  // check wifi still connected
  if (WiFi.status() != WL_CONNECTED)
  {
    Serial.println("Lost Wifi connection, attempting reconnect");
    MQclient.disconnect() ;
    WiFi.disconnect();
    WiFi.reconnect();
    while (WiFi.status() != WL_CONNECTED)
    {
      uint32_t Retries = 20 ;

      delay(500);
      Serial.print(F("."));

      Retries-- ;
      if ( !Retries )
      {
        Serial.println("Failed to reconnect to Wifi, restarting board...") ;
        ESP.restart() ;
      }
    }
  }

  float Humidity = Bme280.readHumidity();
  // Read temperature as Celsius (the default)
  float Temperature = Bme280.readTemperature();

  // can sometimes read stupid values so attempt to recover
  // by resetting the device
  if ( Temperature > 70 )
  {
    Serial.println("Detected stupid reading, attempting re-init to recover") ;
    Bme280.init() ;
    SensorDetected = Bme280.begin(IC2_SENSOR_ADDR,&I2CBME) ;
    if ( !SensorDetected )
    {
      Serial.println("Failed to detect sensor after reset, resetting board") ;
      ESP.restart() ;
    }
    return ;
  }

  Serial.printf("Temp: %f\n",Temperature);
  Serial.printf("Humidity: %f\n",Humidity);

  // publish data
  if ( !MQclient.connected())
  {
    Serial.println("Lost connection to broker, re-connecting") ;
    BrokerConnect() ;
  }

  String TempBuf(Temperature) ;
  String HumidityBuf(Humidity) ;

  // publish to MQTT broker
  bool Success = MQclient.publish("loft-climate/temperature",TempBuf,false,1) ;
  Success&=MQclient.publish("loft-climate/humidity",HumidityBuf,false,1) ;
  if ( Success )
  {
    // toggle status LED on successful update
    if ( digitalRead(STATUS_LED)==LOW)
      digitalWrite(STATUS_LED, HIGH);
    else
      digitalWrite(STATUS_LED, LOW);
  }
  else
  {
    // force a reconnect next time round
    MQclient.disconnect() ;
    Serial.println("Failed to publish one or messages") ;
  }

}

