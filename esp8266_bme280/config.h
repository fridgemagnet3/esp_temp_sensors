#ifndef CONFIG_H

#define CONFIG_H

// wifi settings
#define SSID1 "your-wifi-ssid"
#define PWD1 "your-wifi-password"

// MQTT broker
#define MQTT_BROKER "monolith.onasticksoftware.net"

// I2C pins for the sensor
#define I2C_SCL 14  // D5
#define I2C_SDA 12  // D6

// I2C device addr
#define IC2_SENSOR_ADDR 0x76

// status LED pin
#define STATUS_LED 2

#endif
