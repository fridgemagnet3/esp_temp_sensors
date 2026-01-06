#include <ESP8266WiFi.h>
#include "DHT.h"
#include <MQTT.h>

// wifi settings
#define SSID1 "your-wifi-ssid"
#define PWD1 "your-wifi-password"
// MQTT broker
#define MQTT_BROKER "monolith.onasticksoftware.net"

#define DHTPIN 14     // Digital pin connected to the DHT sensor
#define STATUS_LED 2

// Initial version using Adafruit DHT library

// Initialize DHT sensor.
DHT DhtSensor(DHTPIN, DHT22);

// MQTT client instance
static MQTTClient MQclient;
static WiFiClient Net;

static const char *HATempDiscovery = "\
{\
	\"name\": \"Inside temperature\",\
	\"state_topic\": \"internal-climate/temperature\",\
	\"device_class\": \"temperature\",\
  \"unit_of_measurement\": \"°C\",\
	\"suggested_display_precision\": 1,\
	\"platform\": \"sensor\",\
	\"expire_after\": 60, \
  \"unique_id\": \"esp_internal_climate_temperature\" \
}" ;

static const char *HAHeatIndexDiscovery = "\
{\
	\"name\": \"Inside heat index\",\
	\"state_topic\": \"internal-climate/heatindex\",\
	\"device_class\": \"temperature\",\
  \"unit_of_measurement\": \"°C\",\
	\"suggested_display_precision\": 1,\
	\"platform\": \"sensor\",\
	\"expire_after\": 60, \
  \"unique_id\": \"esp_internal_climate_heatindex\" \
}" ;

static const char *HAHumidityDiscovery = "\
{\
	\"name\": \"Inside humidity\",\
	\"state_topic\": \"internal-climate/humidity\",\
	\"device_class\": \"humidity\",\
  \"unit_of_measurement\": \"%\",\
	\"suggested_display_precision\": 1,\
	\"platform\": \"sensor\",\
	\"expire_after\": 60, \
  \"unique_id\": \"esp_internal_climate_humidity\" \
}" ;

static void BrokerConnect(void)
{
  // connect to MQTT broker
  Serial.println("Connecting to broker");
  MQclient.begin(MQTT_BROKER,Net) ;
  while(!MQclient.connect("house-climate-pub"))
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
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(F("."));
  }
  LocalIp = WiFi.localIP();
  Serial.println(F("WiFi connected"));
  Serial.println(LocalIp);

  // start the DHT sensor
  DhtSensor.begin();

  // connect to MQTT broker
  BrokerConnect() ;

  // publish HA auto-discovery topics for the sensor
  MQclient.publish("homeassistant/sensor/internal-climate/temperature/config",HATempDiscovery,true,0) ;
  MQclient.publish("homeassistant/sensor/internal-climate/heatindex/config",HAHeatIndexDiscovery,true,0) ;
  MQclient.publish("homeassistant/sensor/internal-climate/humidity/config",HAHumidityDiscovery,true,0) ;

  Serial.println("Setup done");

  pinMode(STATUS_LED, OUTPUT);
  digitalWrite(STATUS_LED, LOW);  
}

void loop() 
{
  static float LastTemp = NAN ;

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
      delay(500);
      Serial.print(F("."));
    }
  }

  // Reading temperature or humidity takes about 250 milliseconds!
  // Sensor readings may also be up to 2 seconds 'old' (its a very slow sensor)
  float Humidity = DhtSensor.readHumidity();
  // Read temperature as Celsius (the default)
  float Temperature = DhtSensor.readTemperature();

  // Check if any reads failed and exit early (to try again).
  if (isnan(Humidity) || isnan(Temperature))
  {
    Serial.println(F("Failed to read from DHT sensor!"));
    return;
  }

  // Compute heat index in Celsius (isFahreheit = false)
  float HeatIndex = DhtSensor.computeHeatIndex(Temperature, Humidity, false);

  Serial.printf("Temp: %f\n",Temperature);
  Serial.printf("Humidity: %f\n",Humidity);
  Serial.printf("Heat Index: %f\n",HeatIndex);

  // workaround for sporadic readings coming back at half expected value
  // could be same issue as this https://github.com/esphome/issues/issues/2102
  if (!isnan(LastTemp))
  {
    if(abs(Temperature-LastTemp) > 5)
    {
      Serial.println("Spurious temperature read, discarding") ;
      return ;
    }
  }
  LastTemp = Temperature ;
  
  // publish data
  if ( !MQclient.connected())
  {
    Serial.println("Lost connection to broker, re-connecting") ;
    BrokerConnect() ;
  }

  String TempBuf(Temperature) ;
  String HumidityBuf(Humidity) ;
  String HeatIndexBuf(HeatIndex) ;

  // publish to MQTT broker
  bool Success = MQclient.publish("internal-climate/temperature",TempBuf,false,1) ;
  Success&=MQclient.publish("internal-climate/humidity",HumidityBuf,false,1) ;
  Success&=MQclient.publish("internal-climate/heatindex",HeatIndexBuf,false,1) ;
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

