#include <ESP8266WiFi.h>
#include <dhtnew.h>
#include <MQTT.h>

// wifi settings
#define SSID1 "your-wifi-ssid"
#define PWD1 "your-wifi-password"
// MQTT broker
#define MQTT_BROKER "monolith.onasticksoftware.net"

#define DHTPIN 14     // Digital pin connected to the DHT sensor
#define STATUS_LED 2

// Initialize DHT sensor.
DHTNEW DhtSensor(DHTPIN);

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

// lifted from AdaFruit's DHT library
/*!
 *  @brief  Converts Celcius to Fahrenheit
 *  @param  c
 *					value in Celcius
 *	@return float value in Fahrenheit
 */
static float convertCtoF(float c) { return c * 1.8 + 32; }

/*!
 *  @brief  Converts Fahrenheit to Celcius
 *  @param  f
 *					value in Fahrenheit
 *	@return float value in Celcius
 */
static float convertFtoC(float f) { return (f - 32) * 0.55555; }

/*!
 *  @brief  Compute Heat Index
 *  				Using both Rothfusz and Steadman's equations
 *					(http://www.wpc.ncep.noaa.gov/html/heatindex_equation.shtml)
 *  @param  temperature
 *          temperature in selected scale
 *  @param  percentHumidity
 *          humidity in percent
 *  @param  isFahrenheit
 * 					true if fahrenheit, false if celcius
 *	@return float heat index
 */
static float computeHeatIndex(float temperature, float percentHumidity) 
{
  float hi;

  temperature = convertCtoF(temperature);

  hi = 0.5 * (temperature + 61.0 + ((temperature - 68.0) * 1.2) +
              (percentHumidity * 0.094));

  if (hi > 79) {
    hi = -42.379 + 2.04901523 * temperature + 10.14333127 * percentHumidity +
         -0.22475541 * temperature * percentHumidity +
         -0.00683783 * pow(temperature, 2) +
         -0.05481717 * pow(percentHumidity, 2) +
         0.00122874 * pow(temperature, 2) * percentHumidity +
         0.00085282 * temperature * pow(percentHumidity, 2) +
         -0.00000199 * pow(temperature, 2) * pow(percentHumidity, 2);

    if ((percentHumidity < 13) && (temperature >= 80.0) &&
        (temperature <= 112.0))
      hi -= ((13.0 - percentHumidity) * 0.25) *
            sqrt((17.0 - abs(temperature - 95.0)) * 0.05882);

    else if ((percentHumidity > 85.0) && (temperature >= 80.0) &&
             (temperature <= 87.0))
      hi += ((percentHumidity - 85.0) * 0.1) * ((87.0 - temperature) * 0.2);
  }

  return convertFtoC(hi);
}

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

  // configure the sensor as a DHT22
  DhtSensor.setType(22);

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

    // Compute heat index in Celsius
    float HeatIndex = computeHeatIndex(Temperature, Humidity);

    Serial.printf("Temp: %f\n",Temperature);
    Serial.printf("Humidity: %f\n",Humidity);
    Serial.printf("Heat Index: %f\n",HeatIndex);

  
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
}

