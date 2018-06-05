
/*
    Simple ESP8266 Interlock Control
*/
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#include <ArduinoOTA.h>
#include <WS2812FX.h>



const char* ssid     = "HSBNEWiFi";
const char* password = "HSBNE Wifi Password Here";
const char* host = "10.0.1.253";
const char* deviceName = "INT-TestPlate";


#define RFID_SQUELCH_TIME 5000

//#define USE_STATIC
#ifdef USE_STATIC
IPAddress ip(10, 0, 1, 165);
IPAddress gateway(10, 0, 1, 254);
IPAddress subnet(255, 255, 252, 0);
#endif

const int switchPin = 12;
const int ledPin = 13;
const int statePin = 14;
int contact = 0; // Set default switch state
int lastReadSuccess = 5000; // Set last read success base state. Setting to 10 seconds to make sure on boot it's going to read.
uint32_t lastId = 0;
int activeStart = 0;

WS2812FX ws2812fx = WS2812FX(1, 14, NEO_RGB + NEO_KHZ800);
ESP8266WebServer http(80);


void setup() {
  Serial.begin(9600);
  Serial.println("Serial Started");
  ws2812fx.init();
  ws2812fx.setBrightness(250);
  ws2812fx.start();
  statusLight('p');
  pinMode(ledPin, OUTPUT);
  digitalWrite(ledPin, LOW);
  Serial.setTimeout(500);
  startWifi();
  // Set switch pin to output.
  pinMode(switchPin, OUTPUT);
  digitalWrite(switchPin, LOW); // Always make sure interlock is off on boot.
  ArduinoOTA.setHostname(deviceName);
  ArduinoOTA.setPassword((const char *)"OTAPASSWORDHERE");

  ArduinoOTA.onStart([]() {
    Serial.println("Start");
    statusLight('p');
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
    ws2812fx.service();
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
  
  http.on("/", httpRoot);

  http.on("/reboot", [](){
    http.sendHeader("Location","/");        
    // Redirect back to root in case chrome refreshes.
    http.send(303, "text/plain", "Rebooting.");       
    ESP.reset();
  });
  http.begin();
  Serial.println("HTTP server started");
}

void loop()
{
  yield();
  ArduinoOTA.handle();
  http.handleClient();
  if (millis() > (lastReadSuccess + RFID_SQUELCH_TIME)) {
    if (!contact) {
      statusLight('b');
    } else {
      statusLight('g');
    }
    if (Serial.available()) {
      readTag();
    }
  } else {
    flushSerial();
    yield();
  }
  //Serial.println("Loop complete.");
  ws2812fx.service();

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
      if (cardId != lastId) {
        Serial.println("Tag is new, checking with server.");
        statusLight('w');
        Serial.println(millis());
        checkCard(cardId);
      } else {
        Serial.println("This is the last user disabling the interlock.");
        int state = contact;
        toggleContact();
        int activeFor = millis() - activeStart;
        statusLight('w');
        lastId = 0;
        updateServer(cardId, activeFor);
      }

      lastReadSuccess = millis();
    } else {
      flushSerial();
      Serial.println("incomplete or corrupted RFID read, sorry. ");
    }
  }
}


void checkCard(long tagid) {
  delay(10);
  String url = "" + String(host) + "/interlocks.php?q=" + String(tagid) + "";
  Serial.print("Full URI ");
  Serial.println(url);
  Serial.print("connecting to ");
  Serial.println(host);

  // Use WiFiClient class to create TCP connections
  WiFiClient client;
  const int httpPort = 80;
  if (!client.connect(host, httpPort)) {
    Serial.println("connection failed");
    statusLight('y');
    return;
  }

  // We now create a URI for the request
  url = "/interlocks.php?q=" + String(tagid) + "";
  Serial.print("Requesting URL: ");
  Serial.println(url);

  // This will send the request to the server
  client.println(String("GET ") + url + " HTTP/1.1\r\n" +
                 "Host: " + host + "\r\n" +
                 "Connection: close\r\n\r\n");

  int timeout = millis() + 1500;
  while (client.available() == 0) {
    if (timeout - millis() < 0) {
      Serial.println(">>> Client Timeout !");
      client.stop();
      return;
    }
  }
  // Read all the lines of the reply from server and print them to Serial
  String line = "";
  while (client.available()) {
    ws2812fx.service();
    delay(10);
    line = client.readStringUntil('\n');
  }
  client.stop();
  Serial.println(millis());
  Serial.print("Server response: ");
  Serial.println(line);
  //  Serial.print("Access byte.");
  //  Serial.println(line.endsWith("1"));
  if (line.endsWith("1")) {

    Serial.println("Access granted, toggling contactor.");
    toggleContact();
    activeStart = millis();
    lastId = tagid;
  } else {
    Serial.println("Access not granted.");
    statusLight('r');
    delay(1000);
  }

  Serial.println();
  Serial.println("closing connection");
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

  // While we're not connected breathe the status light and output to serial that we're still connecting.
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(10);
    ws2812fx.service();


  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
  statusLight('b');
  delay(10);
}

void toggleContact() {
  switch (contact) {
    case 0:
      {
        contact = 1;
        digitalWrite(switchPin, HIGH);
        statusLight('e');
        break;
      }
    case 1:
      {
        contact = 0;
        digitalWrite(switchPin, LOW);
        statusLight('b');
        break;
      }
  }
}

char statusLight(char color) {
  switch (color) {
    case 'r':
      {
        ws2812fx.setSegment(0,  0,  0, FX_MODE_STATIC, 0xFF0000, 1000, false);
        break;
      }
    case 'g':
      {
        ws2812fx.setSegment(0,  0,  0, FX_MODE_STATIC, 0x00FF00, 1000, false);
        break;
      }
    case 'b':
      {
        ws2812fx.setSegment(0,  0,  0, FX_MODE_STATIC, 0x0000FF, 1000, false);
        break;
      }
    case 'y':
      {
        ws2812fx.setSegment(0,  0,  0, FX_MODE_STROBE, 0xFF6400, 250, false);
        break;
      }
    case 'p':
      {
        ws2812fx.setSegment(0,  0,  0, FX_MODE_BREATH, 0x800080, 250, false);
        break;
      }
    case 'w':
      {
        ws2812fx.setSegment(0,  0,  0, FX_MODE_BREATH, 0x0000FF, 250, false);
        break;
      }
    case 'e':
      {
        ws2812fx.setSegment(0,  0,  0, FX_MODE_BREATH, 0x0000FF, 250, false);
        break;
      }
  }
  ws2812fx.service();
}

void flushSerial () {
  while (  Serial.available() ) {
    char t = Serial.read();  // flush any remaining bytes.
    Serial.println("flushed a byte");
  }
}

void updateServer(long tagid, int activeTime) {
  String url = "" + String(host) + "/interlocks.php?q=" + String(tagid) + "&t=" + String(activeTime);
  Serial.print("Full URI: ");
  Serial.println(url);
  Serial.print("connecting to ");
  Serial.println(host);

  // Use WiFiClient class to create TCP connections
  WiFiClient client;
  const int httpPort = 80;
  if (!client.connect(host, httpPort)) {
    Serial.println("connection failed");
    statusLight('y');
    return;
  }

  // We now create a URI for the request
  url = "/interlocks.php?q=" + String(tagid) + "&t=" + String(activeTime);
  Serial.println("Requesting URL: ");
  Serial.print(url);

  // This will send the request to the server
  client.println(String("GET ") + url + " HTTP/1.1\r\n" +
                 "Host: " + host + "\r\n" +
                 "Connection: close\r\n\r\n");

  int timeout = millis() + 1500;
  while (client.available() == 0) {
    if (timeout - millis() < 0) {
      Serial.println(">>> Client Timeout !");
      client.stop();
      return;
    }
  }
  // Read all the lines of the reply from server and print them to Serial
  String line = "";
  while (client.available()) {
    line = client.readStringUntil('\n');
  }
  client.stop();
  Serial.println();
  Serial.println("closing connection");
}

void httpRoot() {
    String message = "<h1> This is interlock " + String(deviceName) + "</h1>";
    message += "Contactor is currently " + String(contact) + "<br />";
    message += "Last swiped tag was " + String(lastId)  + "<br />";
    http.send(200, "text/html", message);
}

