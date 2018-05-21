
/*
    Simple ESP8266 ccess Control
*/
#include <ESP8266HTTPUpdateServer.h>
#include <ESP8266WiFi.h>
#include <Adafruit_NeoPixel.h>
#include <ArduinoOTA.h>

const char* ssid     = "HSBNEWiFi";
const char* password = "HSBNEWiFiPassword";
const char* host = "10.0.1.253";
const char* deviceName = "INTMSGreyLathe";

#define USE_STATIC
#ifdef USE_STATIC
IPAddress ip(10,0,1,165);   
IPAddress gateway(10,0,1,254);   
IPAddress subnet(255,0,0,0);   
#endif

const int switchPin = 12;
const int ledPin = 13;
const int statePin = 14;
int contact = 0; // Set default switch state


Adafruit_NeoPixel status = Adafruit_NeoPixel(1, 14, NEO_RGB + NEO_KHZ800);


void setup() {
  Serial.begin(9600);
  status.begin();
  pinMode(ledPin, OUTPUT);
  digitalWrite(ledPin, LOW);
  Serial.setTimeout(500);
  startWifi();
  Serial.println("Serial Started");
  // Set switch pin to output.
  pinMode(switchPin, OUTPUT);



  // Port defaults to 8266
  // ArduinoOTA.setPort(8266);

  // Hostname defaults to esp8266-[ChipID]
  ArduinoOTA.setHostname(deviceName);

  // No authentication by default
   ArduinoOTA.setPassword((const char *)"passwordgoeshere");

  ArduinoOTA.onStart([]() {
    Serial.println("Start");
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
}

void loop()
{
  ArduinoOTA.handle();
  readTag();
  
// Serial.println("Loop complete.");
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
      checkCard(cardId);
    } else {
      flushSerial();
      Serial.println("incomplete or corrupted RFID read, sorry. ");
    }
  }
}


void checkCard(long tagid) {
  String url = "" + String(host) + "/interlock.php?q=" + String(tagid) + "";
  Serial.print("Full URI");
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
  url = "/interlock.php?q=" + String(tagid) + "";
  Serial.print("Requesting URL: ");
  Serial.println(url);

  // This will send the request to the server
  client.print(String("GET ") + url + " HTTP/1.1\r\n" +
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
    line = client.readStringUntil('\r');
  }
  //  Serial.println("Server response:");
  //  Serial.print(line);
  //  Serial.print("Access byte.");
  //  Serial.println(line.endsWith("1"));
  if (line.endsWith("1")) {
 
    Serial.println("Access granted, toggling contactor.");
    toggleContact();
  } else {
    Serial.println("Access not granted.");
    statusLight('y');

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

  WiFi.begin(ssid, password);
  WiFi.hostname(deviceName);
  #ifdef USE_STATIC
  WiFi.config(ip, gateway, subnet);
  #endif 

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    statusLight('b');
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
  statusLight('r');
}

void toggleContact() {
  switch (contact) {
    case 0:
      {
        contact = 1;
        digitalWrite(switchPin, HIGH);
        statusLight('g');
        break;
      }
    case 1:
      {
        contact = 0;
        digitalWrite(switchPin, LOW);
        statusLight('r');
        break;
      }
  }
}

char statusLight(char color) {
  switch (color) {
    case 'r':
      {
        status.setPixelColor(0, 250, 0, 0);
        break;
      }
    case 'g':
      {
        status.setPixelColor(0, 0, 250, 0);
        break;
      }
    case 'b':
      {
        status.setPixelColor(0, 0, 0, 250);
        break;
      }
    case 'y':
      {
        status.setPixelColor(0, 255, 100, 0);
        break;
      }
  }
  status.show();
}

void flushSerial () {
    while (  Serial.available() ) { char t = Serial.read(); Serial.print("flushed:"); Serial.println(t); } // flush any remaining bytes.
}

