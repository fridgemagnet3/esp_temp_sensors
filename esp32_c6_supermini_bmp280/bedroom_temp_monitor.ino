#include <WiFi.h>
#include <WiFiClient.h>
#include <Wire.h>
#include <SPI.h>
#include <Adafruit_BMP280.h>
#include <MQTT.h>
#include "config.h"

// Initialize sensor
static TwoWire I2CBME = TwoWire(0);
static Adafruit_BMP280 Bmp280(&I2CBME); 

RTC_DATA_ATTR int BootCount = 0;

static const char *HATempDiscovery = "\
{\
	\"name\": \"Bedroom temperature\",\
	\"state_topic\": \"bedroom-climate/temperature\",\
	\"device_class\": \"temperature\",\
  \"unit_of_measurement\": \"°C\",\
	\"suggested_display_precision\": 1,\
	\"platform\": \"sensor\",\
	\"expire_after\": 3600, \
  \"unique_id\": \"esp_bedroom_climate_temperature\" \
}" ;

static const char *HAPressureDiscovery = "\
{\
	\"name\": \"Barometer pressure\",\
	\"state_topic\": \"internal-climate/pressure\",\
	\"device_class\": \"atmospheric_pressure\",\
  \"unit_of_measurement\": \"mbar\",\
	\"suggested_display_precision\": 0,\
	\"platform\": \"sensor\",\
	\"expire_after\": 3600, \
  \"unique_id\": \"esp_internal_climate_pressure\" \
}" ;

static const char *HABatteryLevelDiscovery = "\
{\
	\"name\": \"Bedroom sensor battery voltage\",\
	\"state_topic\": \"bedroom-climate/battery\",\
	\"device_class\": \"voltage\",\
  \"unit_of_measurement\": \"V\",\
 	\"suggested_display_precision\": 1,\
	\"platform\": \"sensor\",\
	\"expire_after\": 3600, \
  \"unique_id\": \"esp_bedroom_batt_voltage\" \
}" ;


void setup() 
{
  uint32_t Retries = 10 ;
  IPAddress LocalIp ;
  MQTTClient MQclient;
  WiFiClient Net;
  bool Connected = false ;
  bool Success = false ;
  const uint32_t SleepTime = 15*60 ; // expressed in seconds

  // turn on status led, indicate we're alive
  pinMode(STATUS_LED, OUTPUT);
  digitalWrite(STATUS_LED, HIGH);
  
  Serial.begin(115200);
  Serial.println("Starting up...");

  // connect to Wifi
  WiFi.disconnect(true);
  Serial.println("Connecting to WiFi");
  WiFi.mode(WIFI_STA);
  WiFi.begin(SSID1, PWD1);
  // don't spin indefinately here, if we fail to connect after 10 tries, give up for this session
  while ((WiFi.status() != WL_CONNECTED) && Retries)
  {
    Serial.print(F("."));
    Retries-- ;
    delay(1000);
  }

  if ( !Retries )
    Serial.println("Failed to connect to Wifi");
  else
  {
    LocalIp = WiFi.localIP();
    Serial.println(F("WiFi connected"));
    Serial.println(LocalIp);

    Serial.println("Connecting to broker");
    MQclient.begin(MQTT_BROKER,Net) ;
    Connected = MQclient.connect("bedroom-temp-pub") ;

    if ( !Connected )
      Serial.println("Failed to connect to MQTT broker");
  }

  if ( Connected )
  {
    if ( !BootCount )
    {
      // publish HA auto-discovery topics for the sensor on first boot
      MQclient.publish("homeassistant/sensor/bedroom-climate/temperature/config",HATempDiscovery,true,0) ;
      MQclient.publish("homeassistant/sensor/internal-climate/pressure/config",HAPressureDiscovery,true,0) ;
      MQclient.publish("homeassistant/sensor/bedroom-climate/battery/config",HABatteryLevelDiscovery,true,0) ;
    }
    BootCount++ ;

    // configure the sensor
    I2CBME.begin(I2C_SDA, I2C_SCL, 100000);
    Success = Bmp280.begin(IC2_SENSOR_ADDR) ;
    if ( Success )
    {
      /* Default settings from datasheet. */
      Bmp280.setSampling(Adafruit_BMP280::MODE_NORMAL,     /* Operating Mode. */
                         Adafruit_BMP280::SAMPLING_X2,     /* Temp. oversampling */
                         Adafruit_BMP280::SAMPLING_X16,    /* Pressure oversampling */
                         Adafruit_BMP280::FILTER_X16,      /* Filtering. */
                         Adafruit_BMP280::STANDBY_MS_500); /* Standby time. */

      float Temperature = Bmp280.readTemperature() ;
      float Pressure = Bmp280.readPressure() / 100.0; // millibars

      Serial.printf("Temp: %f\n",Temperature);
      Serial.printf("Pressure: %f\n",Pressure);

      String TempBuf(Temperature) ;
      String PressureBuf(Pressure) ;

      // publish to MQTT broker
      Success = MQclient.publish("bedroom-climate/temperature",TempBuf,false,1) ;
      Success&=MQclient.publish("internal-climate/pressure",PressureBuf,false,1) ;

      // report the current battery voltage level
      int AnalogMv = analogReadMilliVolts(BATTERY_ADC);
      AnalogMv = (AnalogMv*1515)/1000 ;  // 1515 = 5/3.3v voltage divider

      String BatLevelBuf(AnalogMv/1000.0) ;
      Serial.printf("Battery level: %s\n", BatLevelBuf) ;
      MQclient.publish("bedroom-climate/battery",BatLevelBuf,true,0) ;
    }

    // reset the device and shutdown the I2C interface
    // this stops the sensor drawing current whilst in deep sleep mode
    Bmp280.reset() ;
    I2CBME.end() ;

    MQclient.disconnect() ;
  }
  else
  {
    Serial.println("Could not find a BMP280 sensor") ;
  }

  Serial.println("Entering deep sleep mode");

  if ( Success )
  {
    // time to allow any pending traffic to flush through
    // pulse LED to indicate successful update
    for(uint32_t i=0 ; i < 3 ; i++ )
    {
      digitalWrite(STATUS_LED, LOW);
      delay(400);
      digitalWrite(STATUS_LED, HIGH);
      delay(400);
    }
  }
  else
    delay(2000) ;
  
  digitalWrite(STATUS_LED, LOW);
  esp_sleep_enable_timer_wakeup(SleepTime*1000*1000) ; // expressed in microseconds
  esp_deep_sleep_start();
}

void loop() 
{
  // does nothing (we can never get here)
  delay(1000) ;
}

