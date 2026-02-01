#include <WiFi.h>
#include <WiFiClient.h>
#include <dhtnew.h>
#include <MQTT.h>

// board: ESP-32 C3 supermini (ESP32C3 Dev Module)

// wifi settings
#define SSID1 "your-wifi-ssid"
#define PWD1 "your-wifi-password"
// MQTT broker
#define MQTT_BROKER "monolith.onasticksoftware.net"

#define DHTPIN 0     // Digital pin connected to the DHT sensor
#define STATUS_LED 8

// Initialize DHT sensor.
DHTNEW DhtSensor(DHTPIN);

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
	\"expire_after\": 60, \
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
	\"expire_after\": 60, \
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

  // configure the sensor as a DHT22
  DhtSensor.setType(22);

  // connect to MQTT broker
  BrokerConnect() ;

  // publish HA auto-discovery topics for the sensor
  MQclient.publish("homeassistant/sensor/loft-climate/temperature/config",HATempDiscovery,true,0) ;
  MQclient.publish("homeassistant/sensor/loft-climate/humidity/config",HAHumidityDiscovery,true,0) ;

  Serial.println("Setup done");

  pinMode(STATUS_LED, OUTPUT);
  digitalWrite(STATUS_LED, LOW);  
}

void loop() 
{
  static uint32_t Failed = 0u ;

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

  int Rc = DhtSensor.read() ;

  Serial.printf("Read sensor: %d\n",Rc) ;
  switch(Rc)
  {
    case DHTLIB_OK:
      Serial.println("OK");
      break;
    case DHTLIB_ERROR_CHECKSUM:
      Serial.println("Checksum error");
      break;
    case DHTLIB_ERROR_TIMEOUT_A:
      Serial.println("Time out A error");
      break;
    case DHTLIB_ERROR_TIMEOUT_B:
      Serial.println("Time out B error");
      break;
    case DHTLIB_ERROR_TIMEOUT_C:
      Serial.println("Time out C error");
      break;
    case DHTLIB_ERROR_TIMEOUT_D:
      Serial.println("Time out D error");
      break;
    case DHTLIB_ERROR_SENSOR_NOT_READY:
      Serial.println("Sensor not ready");
      break;
    case DHTLIB_ERROR_BIT_SHIFT:
      Serial.println("Bit shift error");
      break;
    case DHTLIB_WAITING_FOR_READ:
      Serial.println("Waiting for read");
      break;
    default:
      Serial.println("Unknown");
      break;
  }

  if ( Rc == DHTLIB_OK )
  {
    // Reading temperature or humidity takes about 250 milliseconds!
    // Sensor readings may also be up to 2 seconds 'old' (its a very slow sensor)
    float Humidity = DhtSensor.getHumidity();
    // Read temperature as Celsius (the default)
    float Temperature = DhtSensor.getTemperature();

    Serial.printf("Temp: %f\n",Temperature);
    Serial.printf("Humidity: %f\n",Humidity);

    Failed = 0u ;

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
  else
  {
    Failed++ ;
    // if had 5 successive sensor read failures, try resetting the library
    if ( Failed == 5u )
    {
      Serial.println("Performing sensor reset") ;
      DhtSensor.reset() ;
      DhtSensor.setType(22);
    }

    // if had 10 successive failures, reset the board
    if ( Failed == 10u )
    {
      Serial.println("Restarting board...") ;
      ESP.restart() ;
    }
  }
}

