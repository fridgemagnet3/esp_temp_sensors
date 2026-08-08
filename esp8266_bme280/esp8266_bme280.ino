#include <ESP8266WiFi.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <MQTT.h>
#include "config.h"

// Board: LOLIN(WEMOS) D1 R2 & mini

static TwoWire I2CBME;
static Adafruit_BME280 Bme280;

static bool SensorDetected = false ;

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

  // configure the sensor
  I2CBME.begin(I2C_SDA, I2C_SCL, 100000);
  SensorDetected = Bme280.begin(IC2_SENSOR_ADDR,&I2CBME) ;
  
  if ( SensorDetected )
  {
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
  else
    Serial.println("Failed to detect sensor");
}

void loop() 
{
  // Wait a few seconds between measurements.
  delay(5000);

  if ( !SensorDetected )
    return ;

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

