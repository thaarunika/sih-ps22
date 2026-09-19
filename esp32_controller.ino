#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <DHT.h>

// =====================================================
// PIN CONFIGURATION
// =====================================================
#define DHT_PIN       21
#define FAN_RELAY     18
#define AUX_RELAY     19
#define BUZZER_PIN    23

#define DHT_TYPE DHT22

// =====================================================
// RELAY LOGIC
// Most 5V relay modules are ACTIVE LOW
// =====================================================
#define RELAY_ON  LOW
#define RELAY_OFF HIGH

// =====================================================
// BUZZER TEMPERATURE LIMIT
// =====================================================
#define TEMP_LIMIT 35.0

// =====================================================
// OBJECTS
// =====================================================
DHT dht(DHT_PIN, DHT_TYPE);

WebServer server(80);
WebSocketsServer webSocket(81);

// =====================================================
// VARIABLES
// =====================================================
float temperature = 0.0;
float humidity = 0.0;

bool fanState = false;
bool auxRelayState = false;
bool buzzerState = false;

unsigned long lastDHTRead = 0;
const unsigned long DHT_INTERVAL = 2000;

// =====================================================
// FAN CONTROL
// =====================================================
void setFan(bool state)
{
  fanState = state;

  digitalWrite(
    FAN_RELAY,
    state ? RELAY_ON : RELAY_OFF
  );
}

// =====================================================
// AUX RELAY CONTROL
// =====================================================
void setAuxRelay(bool state)
{
  auxRelayState = state;

  digitalWrite(
    AUX_RELAY,
    state ? RELAY_ON : RELAY_OFF
  );
}

// =====================================================
// BUZZER CONTROL
// =====================================================
void setBuzzer(bool state)
{
  buzzerState = state;

  digitalWrite(
    BUZZER_PIN,
    state ? HIGH : LOW
  );
}

// =====================================================
// SEND TELEMETRY TO DASHBOARD
// =====================================================
void sendTelemetry()
{
  String json = "{";

  json += "\"temp\":";
  json += String(temperature, 1);

  json += ",\"hum\":";
  json += String(humidity, 1);

  json += ",\"fan\":";
  json += fanState ? "true" : "false";

  json += ",\"aux\":";
  json += auxRelayState ? "true" : "false";

  json += ",\"buzzer\":";
  json += buzzerState ? "true" : "false";

  json += ",\"limit\":";
  json += String(TEMP_LIMIT, 1);

  json += ",\"rssi\":";
  json += String(WiFi.RSSI());

  json += "}";

  webSocket.broadcastTXT(json);
}

// =====================================================
// WEBSOCKET EVENT
// =====================================================
void webSocketEvent(
  uint8_t num,
  WStype_t type,
  uint8_t *payload,
  size_t length
)
{
  // ---------------------------------------------------
  // DASHBOARD CONNECTED
  // ---------------------------------------------------
  if (type == WStype_CONNECTED)
  {
    Serial.println("Dashboard connected.");

    // Send current state immediately
    String json = "{";

    json += "\"temp\":";
    json += String(temperature, 1);

    json += ",\"hum\":";
    json += String(humidity, 1);

    json += ",\"fan\":";
    json += fanState ? "true" : "false";

    json += ",\"aux\":";
    json += auxRelayState ? "true" : "false";

    json += ",\"buzzer\":";
    json += buzzerState ? "true" : "false";

    json += ",\"limit\":";
    json += String(TEMP_LIMIT, 1);

    json += ",\"rssi\":";
    json += String(WiFi.RSSI());

    json += "}";

    webSocket.sendTXT(num, json);
  }

  // ---------------------------------------------------
  // COMMAND FROM DASHBOARD
  // ---------------------------------------------------
  else if (type == WStype_TEXT)
  {
    String command = "";

    for (size_t i = 0; i < length; i++)
    {
      command += (char)payload[i];
    }

    Serial.print("Dashboard command: ");
    Serial.println(command);

    // =================================================
    // FAN MANUAL TOGGLE
    // =================================================
    if (command.indexOf("\"toggle\":\"fan\"") >= 0)
    {
      setFan(!fanState);

      Serial.print("FAN: ");
      Serial.println(fanState ? "ON" : "OFF");
    }

    // =================================================
    // AUX MANUAL TOGGLE
    // =================================================
    else if (command.indexOf("\"toggle\":\"aux\"") >= 0)
    {
      setAuxRelay(!auxRelayState);

      Serial.print("AUX: ");
      Serial.println(auxRelayState ? "ON" : "OFF");
    }

    // Send updated states
    sendTelemetry();
  }
}

// =====================================================
// HTTP ROOT
// =====================================================
void handleRoot()
{
  server.send(
    200,
    "text/plain",
    "Agarbathi Dryer ESP32 Controller is running."
  );
}

// =====================================================
// WIFI CONNECTION
// =====================================================
void connectWiFi()
{
  Serial.println();
  Serial.print("Connecting to WiFi: ");
  Serial.println(WIFI_SSID);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int count = 0;

  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");

    count++;

    if (count >= 40)
    {
      Serial.println();
      Serial.println("WiFi connection failed.");
      return;
    }
  }

  Serial.println();
  Serial.println("==============================");
  Serial.println("WiFi connected!");
  Serial.print("ESP32 IP Address: ");
  Serial.println(WiFi.localIP());
  Serial.println("==============================");
}

// =====================================================
// SETUP
// =====================================================
void setup()
{
  Serial.begin(115200);

  // ---------------------------------------------------
  // OUTPUT PINS
  // ---------------------------------------------------
  pinMode(FAN_RELAY, OUTPUT);
  pinMode(AUX_RELAY, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);

  // ---------------------------------------------------
  // SAFE INITIAL STATE
  // Both relays OFF
  // Buzzer OFF
  // ---------------------------------------------------
  digitalWrite(FAN_RELAY, RELAY_OFF);
  digitalWrite(AUX_RELAY, RELAY_OFF);
  digitalWrite(BUZZER_PIN, LOW);

  // ---------------------------------------------------
  // DHT22
  // ---------------------------------------------------
  dht.begin();

  delay(1000);

  // ---------------------------------------------------
  // WIFI
  // ---------------------------------------------------
  connectWiFi();

  // ---------------------------------------------------
  // Keep both relays OFF initially
  // They are controlled ONLY by dashboard
  // ---------------------------------------------------
  setFan(false);
  setAuxRelay(false);

  // ---------------------------------------------------
  // Buzzer initially OFF
  // ---------------------------------------------------
  setBuzzer(false);

  // ---------------------------------------------------
  // HTTP SERVER
  // ---------------------------------------------------
  server.on("/", handleRoot);
  server.begin();

  // ---------------------------------------------------
  // WEBSOCKET SERVER
  // ---------------------------------------------------
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);

  Serial.println("HTTP server started.");
  Serial.println("WebSocket server started on port 81.");
  Serial.println("System ready.");
}

// =====================================================
// MAIN LOOP
// =====================================================
void loop()
{
  server.handleClient();
  webSocket.loop();

  // ===================================================
  // READ DHT22 EVERY 2 SECONDS
  // ===================================================
  if (millis() - lastDHTRead >= DHT_INTERVAL)
  {
    lastDHTRead = millis();

    float newTemp = dht.readTemperature();
    float newHum = dht.readHumidity();

    // -------------------------------------------------
    // VALID READING
    // -------------------------------------------------
    if (!isnan(newTemp) && !isnan(newHum))
    {
      temperature = newTemp;
      humidity = newHum;

      Serial.print("Temperature: ");
      Serial.print(temperature, 2);

      Serial.print(" °C | Humidity: ");
      Serial.print(humidity, 2);

      Serial.println(" %");

      // ===============================================
      // ONLY BUZZER IS AUTOMATIC
      // ===============================================
      if (temperature >= TEMP_LIMIT)
      {
        setBuzzer(true);

        Serial.println(
          "⚠ Temperature >= 35°C → BUZZER ON"
        );
      }
      else
      {
        setBuzzer(false);
      }

      // Send updated sensor + device states
      sendTelemetry();
    }

    // -------------------------------------------------
    // INVALID DHT22 READING
    // -------------------------------------------------
    else
    {
      Serial.println(
        "DHT22 reading failed - keeping last valid value."
      );
    }
  }
}