#define TINY_GSM_MODEM_SIM7600  // SIM7600 AT instruction is compatible with A7670
#define SerialAT Serial1
#define SerialMon Serial
#define TINY_GSM_USE_GPRS true
#include <TinyGsmClient.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <Adafruit_ADS1X15.h> // Include ADS1115 library
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <Ticker.h>  // Include the Ticker library

#define RXD2 17    
#define TXD2 16   
#define powerPin 4 ////VVM501 MODULE ESP32 PIN D4 CONNECTED TO POWER PIN OF A7670C CHIPSET, INTERNALLY CONNECTED

int LED_BUILTIN = 2;
int ledStatus = LOW;

const char *broker = "test.mosquitto.org"; // REPLACE IF YOU ARE USING ANOTHER BROKER
const int port = 1883;
const char *messageTopic = "qwe";   // PUBLISH TOPIC TO SEND MESSAGE EVERY 3 SECONDS. REPLACE "your/topic" WITH YOUR TOPIC NAME

const char apn[] = ""; // APN automatically detects for 4G SIM, NO NEED TO ENTER, KEEP IT BLANK

#ifdef DUMP_AT_COMMANDS
#include <StreamDebugger.h>
StreamDebugger debugger(SerialAT, SerialMon);
TinyGsm modem(debugger);
#else
TinyGsm modem(SerialAT);
#endif
TinyGsmClient client(modem);
PubSubClient mqtt(client);

Adafruit_ADS1115 ads; // Create an ADS1115 instance with address 0x48

AsyncWebServer server(80);

bool adsConnected = false;
int16_t ch0 = 0;
int16_t ch1 = 0;
int16_t ch2 = 0;

int connectionAttempts = 0;

// Create a Ticker object
Ticker webServerTimer;

// Function to stop the web server
void stopWebServer() {
  server.end();
  WiFi.softAPdisconnect(true);
  Serial.println("Web server stopped");
}

void setup() {
  Serial.begin(115200);

  pinMode(LED_BUILTIN, OUTPUT);

  pinMode(powerPin, OUTPUT);
  digitalWrite(powerPin, LOW);
  delay(100);
  digitalWrite(powerPin, HIGH);
  delay(1000);
  digitalWrite(powerPin, LOW);

  delay(2000);

  Serial.println("\nconfiguring Module. Kindly wait");

  delay(10000);

  SerialAT.begin(115200, SERIAL_8N1, RXD2, TXD2);

  initializeModem();

  // MQTT Broker setup
  mqtt.setServer(broker, port);

  // Initialize ADS1115
  adsConnected = ads.begin();
  if (!adsConnected) {
    Serial.println("Failed to initialize ADS1115");
    mqtt.publish(messageTopic, "Failed to initialize ADS1115");
  }

  // Set up WiFi in AP mode
  WiFi.softAP("ESP32_AP");

  // Set up web server
server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
  String html = "<html><head><meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += "<style>";
  html += "body { background-color: #f0f0f0; }"; // light gray background
  html += ".outer-container { background-color: #f0f0f0; padding: 20px; border: 1px solid #CCCCCC; border-radius: 10px; }";
  html += ".inner-container { background-color: #ADD8E6; padding: 10px; margin-bottom: 10px; }";
  html += ".inner-container span { color: #000000; font-size: 16px; }"; // black text
  html += "@media only screen and (max-width: 600px) {";
  html += "  .outer-container { padding: 10px; }";
  html += "  .inner-container { padding: 5px; font-size: 14px; }";
  html += "}";
  html += "</style></head><body>";
  html += "<div class='outer-container'>";
  html += "<div class='inner-container'><h1>ESP32 Status</h1></div>";
  html += "<div class='inner-container'><p><span>Network Status: " + String(modem.isNetworkConnected() ? "Connected" : "Disconnected") + "</span></p></div>";
  html += "<div class='inner-container'><p><span>MQTT Status: " + String(mqtt.connected() ? "Connected" : "Disconnected") + "</span></p></div>";
  html += "<div class='inner-container'><p><span>Signal Strength: " + getSignalStrength() + "</span></p></div>";
  html += "<div class='inner-container'><p><span>ADS1115 Status: " + String(adsConnected ? "Connected" : "Disconnected") + "</span></p></div>";
  html += "<div class='inner-container'><p><span>Pressure: " + String(ch0) + "</span></p></div>";
  html += "<div class='inner-container'><p><span>Solar Output: " + String(ch1) + "</span></p></div>";    
  html += "<div class='inner-container'><p><span>Battery Voltage: " + String(ch2) + "</span></p></div>";
  html += "</div>";
  html += "</body></html>";
  request->send(200, "text/html", html);
});

  server.begin();

  // Set the timer to stop the web server after 15 minutes (900 seconds)
  webServerTimer.once(300, stopWebServer);
}

void loop() {
  if (!mqtt.connected()) {
    reconnect();                // Just in case we get disconnected from MQTT server
  }

  // Read raw value from ADS1115 channel 0
  if (adsConnected) {
    try {
      ch0 = ads.readADC_SingleEnded(0);
      ch1 = ads.readADC_SingleEnded(1);
      ch2 = ads.readADC_SingleEnded(2);
      Serial.print("ADS1115 Value: ");
      Serial.println(String(ch0) + String(ch1) + String(ch2));
     // char message[50];
     // sprintf(message, "Raw Value: %d", rawValue1);
     // mqtt.publish(messageTopic, message);




    } catch (...) {
      Serial.println("Error reading ADS1115");
      mqtt.publish(messageTopic, "Error reading ADS1115");
    }
  }

  mqtt.publish(messageTopic, "OK");

  delay(3000);

  if (!modem.isNetworkConnected()) {
    Serial.println("Network disconnected");
    if (!modem.waitForNetwork(180000L, true)) {
      Serial.println(" fail");

      delay(10000);
      return;
    }
    if (modem.isNetworkConnected()) {
      Serial.println("Network re-connected");
    }

#if TINY_GSM_USE_GPRS
    // and make sure GPRS/EPS is still connected
    if (!modem.isGprsConnected()) {
      Serial.println("GPRS disconnected!");
      Serial.print(F("Connecting to "));
      Serial.print(apn);
      if (!modem.gprsConnect(apn)) {
        Serial.println(" fail");
        delay(10000);
        return;
      }
      if (modem.isGprsConnected()) {
        Serial.println("GPRS reconnected");
      }
    }
#endif
  }

  mqtt.loop();
  delay(100);
}

void reconnect() {
  while (!mqtt.connected()) {       // Loop until connected to MQTT server
    Serial.print("Attempting MQTT connection...");

    boolean status = mqtt.connect("4g modem test");

    // Or, if you want to authenticate MQTT:
    // boolean status = mqtt.connect("GsmClientName", "mqtt_user", "mqtt_pass");

    if (status == false) {
      Serial.println(" fail");
      Serial.print("failed, rc=");
      Serial.print(mqtt.state());
      Serial.println(" try again in 5 seconds");
      delay(10000);  // Will attempt connection again in 5 seconds
      connectionAttempts++;
      
      if (connectionAttempts >= 5) {
        restartModem();
        connectionAttempts = 0;
      }
    } else {
      Serial.println(" success");
      connectionAttempts = 0;
    }
  }
}

void restartModem() {
  Serial.println("Restarting modem...");
  digitalWrite(powerPin, LOW);
  delay(1000);
  digitalWrite(powerPin, HIGH);
  delay(10000);
  digitalWrite(powerPin, LOW);
  initializeModem();
}

void initializeModem() {
  Serial.println("Initializing modem...");
  if (!modem.init()) {
    Serial.println("Failed to initialize modem");
    return;
  }
  if (!modem.restart()) {
    Serial.println("Failed to restart modem");
    return;
  }

  String name = modem.getModemName();
  Serial.println("Modem Name: " + name);

  String modemInfo = modem.getModemInfo();
  Serial.println("Modem Info: " + modemInfo);

  Serial.println("Waiting for network...");
  if (!modem.waitForNetwork()) {
    Serial.println("Failed to connect to network");
    return;
  }
  Serial.println("Network connected");

  if (!modem.gprsConnect(apn)) {
    Serial.println("Failed to connect to GPRS");
    return;
  }
  Serial.println("GPRS connected");
}

String getSignalStrength() {
  int signalQuality = modem.getSignalQuality();
  //Serial.println(signalQuality);
  return String(signalQuality);
}
