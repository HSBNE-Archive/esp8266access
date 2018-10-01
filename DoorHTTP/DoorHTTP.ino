

/*
    Simple ESP8266 Interlock Control
*/
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#include <ArduinoOTA.h>
#include <TickerScheduler.h>
#include <WebSocketsServer.h>
#include <WebSockets.h>
#include <ESP8266HTTPClient.h>


const char* ssid     = "HSBNEWiFi";
const char* password = "HSBNEWiFiPassword";
const char* host = "http://hostip/logger.php?q=";
const char* deviceName = "DOOR-Test";


#define RFID_SQUELCH_TIME 5000

//#define USE_STATIC
#ifdef USE_STATIC
IPAddress ip(10, 0, 1, 168);
IPAddress gateway(10, 0, 1, 254);
IPAddress subnet(255, 255, 252, 0);
#endif

const int checkinInterval = 60;
const int switchPin = 12;
const int ledPin = 13;
const int statePin = 14;
volatile int heartbeatFlag = 0; //Heartbeat flag triggered by interrupt
int contact = 1; // Set default output state
int lastReadSuccess = 5000; // Set last read success base state. Setting to 10 seconds to make sure on boot it's going to read.
uint32_t lastId = 0;
int activeStart = 0;


int heap = 0;
int prevheap = 0;

HTTPClient client;
ESP8266WebServer http(80);
WebSocketsServer webSocket = WebSocketsServer(81);

Ticker heartbeat;


void ICACHE_RAM_ATTR idleHeartBeatFlag() {
  heartbeatFlag = 1;
}



void startWifi () {
  delay(10);
  // We start by connecting to a WiFi network

  Serial.println();
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  WiFi.hostname(deviceName);

  // If we're setup for static IP assignment, apply it.
#ifdef USE_STATIC
  WiFi.config(ip, gateway, subnet);
#endif

  // While we're not connected output to serial that we're still connecting.
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(100);
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
  delay(10);
}

void pulseContact() {
  switch (contact) {
    case 0:
      {
        digitalWrite(switchPin, HIGH);
        delay(5000);
        digitalWrite(switchPin, LOW);
        break;
      }
    case 1:
      {
        digitalWrite(switchPin, LOW);
        delay(5000);
        digitalWrite(switchPin, HIGH);
        break;
      }
  }
}

void flushSerial () {
  int flushCount = 0;
  while (  Serial.available() ) {
    char t = Serial.read();  // flush any remaining bytes.
    flushCount++;
    // Serial.println("flushed a byte");
  }
  if (flushCount > 0) {
    log("[DEBUG] Flushed " + String(flushCount) + " bytes.");
    flushCount = 0;
  }

}

void httpRoot() {
  String message = "<html><head><script>var connection = new WebSocket('ws://'+location.hostname+':81/', ['arduino']);connection.onopen = function () {  connection.send('Connect ' + new Date()); }; connection.onerror = function (error) {    console.log('WebSocket Error ', error);};connection.onmessage = function (e) {  console.log('Server: ', e.data); var logObj = document.getElementById('logs'); logObj.insertAdjacentHTML('afterend', e.data + '</br>');;};</script></head>";
  message += "<h1> This is access control endpoint " + String(deviceName) + "</h1>";
  message += "Last swiped tag was " + String(lastId)  + "<br />";
  message += "<h2>Logs:</h2><div id='logs'></div>";
  http.send(200, "text/html", message);
}

void log(String entry) {
  Serial.println(entry);
  webSocket.broadcastTXT(millis() + entry);
  delay(10);
}


void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {

  switch (type) {
    case WStype_DISCONNECTED:
      log(num + " Disconnected!");
      break;
    case WStype_CONNECTED: {
        IPAddress ip = webSocket.remoteIP(num);
        Serial.println(String(num) + " Connected from " + String(ip[0]) + "." + String(ip[1]) + "." + String(ip[2]) + "." + String(ip[3]));

        // send message to client
        // webSocket.sendTXT(num, "Connected");
      }
      break;
  }

}

void ICACHE_RAM_ATTR checkIn() {
  // Delay to clear wifi buffer.
  delay(10);
  client.begin(String(host) + "1234567890");
  // Start http request, response will be negative on error
  if (client.GET() > 0) {
    // Checkin succeeded.
    log("[CHECKIN] Success. Response: " + client.getString());
  } else {
    log("[CHECKIN] Failed.");
  }
  client.end();
  // log("[CHECKIN] Checkin done.");
  delay(10);
}

void ICACHE_RAM_ATTR authCard(long tagid) {

  log("[AUTH] Server auth check begin");
  client.begin(String(host) + String(tagid));

  // Start http request.
  int httpCode = client.GET();
  // httpCode will be negative on error
  if (httpCode > 0) {
    // log("[AUTH] Code: " + String(httpCode));

    // Checkin succeeded.
    if (httpCode == HTTP_CODE_OK) {
      String payload = client.getString();
      payload.trim();
      log("[AUTH] Server response: " + payload);
      if (payload.endsWith("1")) {

        log("[AUTH] Access granted, switching door.");
        pulseContact();
        lastId = tagid;
      } else {
        Serial.println("[AUTH] Access not granted.");
        delay(1000);
      }

    }
  } else {
    log("[AUTH] Error: " + client.errorToString(httpCode));
  }
  client.end();
  log("[AUTH] Card Auth done.");
}

void readTag() {
  char tagBytes[6];

  //  while (!Serial.available()) { delay(10); }

  if (Serial.readBytes(tagBytes, 5) == 5)
  {
    uint8_t checksum = 0;
    uint32_t cardId = 0;

    tagBytes[6] = 0;

    //    Serial.println("Raw Tag:");
    for (int i = 0; i < 4; i++)
    {
      checksum ^= tagBytes[i];
      cardId = cardId << 8 | tagBytes[i];
      //     Serial.println(tagBytes[i], HEX);
    }

    if (checksum == tagBytes[4])
    {
      Serial.print("Tag Number:");
      Serial.println(cardId);
      flushSerial();
      authCard(cardId);
      lastReadSuccess = millis();
    } else {
      flushSerial();
      log("incomplete or corrupted RFID read, sorry. ");
    }
  }
}

void setup() {
  Serial.begin(9600);
  Serial.println("Serial Started");
  pinMode(ledPin, OUTPUT);
  digitalWrite(ledPin, LOW);
  Serial.setTimeout(500);
  startWifi();
  // Set switch pin to output.
  pinMode(switchPin, OUTPUT);
  if (!contact) {
    digitalWrite(switchPin, LOW); // Set base switch state.
  } else {
    digitalWrite(switchPin, HIGH); // Set base switch state.
  }
  ArduinoOTA.setHostname(deviceName);
  ArduinoOTA.setPassword((const char *)"otapassword");

  ArduinoOTA.onStart([]() {
    log("[DEBUG] OTA Start");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });
  ArduinoOTA.begin();

  //Setup Websocket debug logger
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);

  http.on("/", httpRoot);

  http.on("/reboot", []() {
    http.sendHeader("Location", "/");
    // Redirect back to root in case chrome refreshes.
    log("[DEBUG] Rebooting.");
    ESP.reset();
  });
  http.on("/bump", []() {
    http.send(200, "text/plain", "Bumping door.");
    log("[DEBUG] Bumped lock.");
    pulseContact();
  });
  http.on("/checkin", []() {
    idleHeartBeatFlag();
  });
  http.begin();
  Serial.println("HTTP server started");
  heartbeat.attach(checkinInterval, idleHeartBeatFlag);
}

void loop()
{
  delay(100);
  if (millis() > (lastReadSuccess + RFID_SQUELCH_TIME)) {
    //log("[DEBUG] Squelching Off");
    if (Serial.available()) {
      log("[DEBUG] Serial Available");
      readTag();
    }
  } else {
    flushSerial();
    yield();
  }
  // Serial.println("Loop complete.");

  if (heartbeatFlag == 1) {
    checkIn();
    log("[DEBUG] Free Heap Size: " + String(ESP.getFreeHeap()));
    heartbeatFlag = 0;
    delay(10);
  }
  delay(100);
  ArduinoOTA.handle();
  http.handleClient();
  webSocket.loop();
  delay(10);
  webSocket.broadcastPing();
}
