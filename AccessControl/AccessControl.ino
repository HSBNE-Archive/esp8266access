// (c) David "Buzz" Bussenschutt  21 Oct 2017
#include <SoftwareSerial.h>
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <IPAddress.h>
#include <WiFiUdp.h>
#include <ESP8266WebServer.h>
#include <EEPROM.h>
#include <NTPClient.h>             // installed using library manager and searching for 'ntp'. https://github.com/arduino-libraries/NTPClient 
#include "EEPROMAnything.h"

//#define USE_NEOPIXELS 1
#ifdef USE_NEOPIXELS
#include <Adafruit_NeoPixel.h>
Adafruit_NeoPixel NEO = Adafruit_NeoPixel(1, 14, NEO_RGB + NEO_KHZ800);
#endif

#define USE_OTA 1
#ifdef USE_OTA
#include <ArduinoOTA.h>
#endif

// START DEFINES --------------------------------------------------------
#define GREEN_ONBOARD_LED 13
#define RELAY_ONBOARD 12

// THREE external LEDS  GREEN-WHITE-RED   , where the green and red are GPIO connected, and the "white" is just a power indicator.
 // GPIO4 =  lowest pin on box , yellow wire inside.
 // GPIO5 =  2nd from the end, next to 4.  
#define GREEN_EXTERNAL_LED 5
//#define WHITE_LED , always powered on.
#define RED_LED 4

#define EEPROM_SIZE 1024
#define MEMORY_HEADER_LEN 100
#define MEMORY_RFID_LENGTH 4

#define ENABLE_ESTOP_AS_EGRESS_BUTTON 1
// on SONOFF GPIO0 is the oboard bushbutton, but an external button can be soldered over-the-top so either/both work.  any gpio except GPIO16 is ok.
#define EGRESS_OR_ESTOP 0 

// END DEFINES --------------------------------------------------------

// START GLOBALS --------------------------------------------------------

const byte interruptPin = EGRESS_OR_ESTOP;
volatile byte interruptCounter = 0; // changes to non-zero on interrupt for use outside of the interrupt. 

WiFiUDP NTP;    // yep NTP is actually a UDP protocol. 
int tzoffset = 10*3600;    // 10hrs in seconds.
int updateinterval = 60*1000; // 60 seconds in ms
NTPClient timeClient(NTP, "pool.ntp.org", tzoffset, updateinterval); // dns resolver will find nearest AU NTP server for us.
// GetDateTimeFromUnix() call results go here: 
uint8_t Seconds, Minutes, Hours, WeekDay, Year, Month, Day; 

SoftwareSerial readerSerial(14, SW_SERIAL_UNUSED_PIN); // RX, TX

ESP8266WebServer HTTP(80);

IPAddress myIP; // us, once we know it.

//  a client of this TX wifi network:
const char *password = "xxxxxxxxxxxxx";
const char *ssid = "HSBNEWiFi";

// static IP address assigment, gateway, and netmask.
char * Cipa = "10.0.1.220";
char * Cgate = "10.0.1.254";
char * Csubnetmask = "255.255.254.0";
String deviceName = "MembersStorage";

int next_empty_slot = -1; 

// unsigned long is 4 bytes
struct config_t
{
    unsigned long rfid_tag;
} tagcache;

int eot = 0; // position of most recentkly read byte in rfidreadbytes structure, so repreenting the 'end of tag' in the array.

byte rfidreadbytes[12];

// remote web server
const char* host = "10.0.1.253";
const int httpPort = 80;
unsigned long lastConnectionTime = 0; // most recent time we had any comms on the http/wifi interface. 
unsigned long lastAttemptTime = 0; // most recent time we tried to have any comms on the http/wifi interface. 
unsigned long pollingInterval = 60;        // maximum 60 secs between network checks, in seconds. Also, being offline for 5 in a row ( 5 mins ) of these causes a hard reboot.


//// unused at present.
//#ifdef OTHERUDP
//char incoming[1024]; // buff for recieving UDP when in STA mode.
//const int navPort = 5554;
//WiFiUDP Udp; // this will be the BROADCAST address we send to, if we use it. 
//IPAddress broadcast;
//#endif

unsigned long previousMillis = 0;        // will store last time LED/relay was updated

const long interval = 4000; // how long it's OPEN for..

int ledState = LOW; 

// if there's been any change in the data, and we'll be formulating a new UDP packet..
int udpnew = 0;

// passed to logger.php, and also expected in the POST to verify it's legit.
String programmed_secret = "abc123"; 

//END GLOBALS --------------------------------------------------------

// so function matches callback.
void handleHTTP() {
  handleHTTPplus("");
}

void handleHTTPplus(String toclient) {
        Serial.print("A / http client connected, fyi.\n");
        WiFiClient x = HTTP.client();
        IPAddress i = x.remoteIP();
        Serial.println(i);

        //String toclient = "";

        Serial.print("LAST UPDATE WAS AT:"); 
        Serial.println(get_long_from_offset(96));

        unsigned long when = get_long_from_offset(96);

        // machine readable date
        toclient += "LAST_UPDATE:"; 
        toclient += when;
        toclient += "<br>\n"; 
        
        // human readable date
        toclient += get_nice_date_from_epoch(when);         

        int cached_eeprom_count = display_tags_in_eeprom(); 
        String num = String(cached_eeprom_count); 
        toclient +=  "TOTAL_CACHED:"; 
        toclient +=  num;
        toclient += "<br>\n";  

        // Wifi HTTP print the same list. all of them.
        String tagstring = get_tags_in_eeprom();
        toclient +=  "<pre>SAVED_LIST:\n"; 
        toclient += tagstring;
        toclient += "</pre>\n";  
       
        HTTP.send(200, "text/html", "<h1>You are at this snarc_esp's web page, nothing to do here.</h1><br>\n"+toclient);

}


  unsigned long convert_to_ulong(String s) 
  {
      
      char value[25];
      
      s.toCharArray(value,25);
  
      // convert it to a long 
      unsigned long value2 =  strtoul(value, NULL,0); 
  
      // if the number is too long to fit inside an unsigned long, then 'stroul' gives us 4294967295 instead.
      // if the conversion failed, we get zero.   we need ot handle both these as error cases.
      if (( value2 == 0 ) || ( value2 == 4294967295 ) ) { 
          return 0;
      } 
  
      return value2;
  }

String get_nice_date_from_epoch(unsigned long now_epoch_secs) {
        // display a more human time info
        GetDateTimeFromUnix(now_epoch_secs);
        
        //  human info approximating  YYYY/MM/DD-HH:MM:SS format. 
        String Str_now = "REAL_TIME:";
         String YearStr = String(Year + 2000); 
        Str_now += YearStr;Str_now += "/";
         String MonthStr = Month < 10 ? "0" + String(Month) : String(Month);
        Str_now += MonthStr;Str_now += "/";
         String DayStr = Day < 10 ? "0" + String(Day) : String(Day);        
        Str_now += DayStr;Str_now += "-";
        unsigned long hours = (now_epoch_secs % 86400L) / 3600;
        String hoursStr = hours < 10 ? "0" + String(hours) : String(hours);
        unsigned long minutes = (now_epoch_secs % 3600) / 60;
        String minuteStr = minutes < 10 ? "0" + String(minutes) : String(minutes);
        unsigned long seconds = now_epoch_secs % 60;
        String secondStr = seconds < 10 ? "0" + String(seconds) : String(seconds);
        Str_now +=  hoursStr; Str_now += ":";
        Str_now +=  minuteStr; Str_now += ":";
        Str_now +=  secondStr; 
        
        Str_now += "<br>\n";

        return Str_now;
}

//  only accepts POST requests for now. 
bool handleHTTPusers() {
        Serial.print("A /users http client connected, fyi.\n");

        // did the POST request come from the 'server' IP we have hardcoded in here..? 
        WiFiClient x = HTTP.client();
        IPAddress i = x.remoteIP();

        Serial.print("client connected from IP:" );
        Serial.println(i);

        // convert the string for the remot server's IP into an address format.
        IPAddress ihost; ihost.fromString(host);

         
        String from_client = HTTP.arg("plain");
        String toclient = "";

      if ( i != ihost ) { 
            Serial.print("client connected from  WRONG IP:" );
            Serial.print(i);
            Serial.print(" instead of ");
            Serial.println(ihost);
            toclient += "client connected from  WRONG IP: "; 
            toclient += i;
            toclient += " instead of ";
            toclient += ihost;
            toclient += "<br>\n";
           handleHTTPplus(toclient);  
           return false;
        } else { 
            Serial.println("Client connected from CORRECT IP, continuing." );          
        }
       

        if (HTTP.hasArg("plain")== false){
           //Expecting POST request for user list, if we didn't get it, just display the default page with info.
           handleHTTPplus("Expecting POST request for user list, dodn't get it.");  
           return false;
        }

        // to get to this point in the code, it's got to be a POST, and to the /users endpoint, do we trust it? 
        // a bit more verification, just to be on the safe side. 
        // here we require one of the lines of POST data to be like this: 'secret:somethingspecial\n'
        int secretstart = from_client.indexOf("secret:");
        secretstart += 7 ; // to skip over the 'secret:' bit 
        int secretend  = from_client.indexOf('\n', secretstart+1);
        if (( secretstart >= 0 ) && ( secretend > 0) )  { 
           String mysecret = from_client.substring(secretstart,secretend ); 
           // don't display this, as it's secret. :-) 
           //Serial.print("SECRET:"); Serial.println(mysecret);
           //toclient += "SECRET:"; toclient += mysecret; toclient += "<br>\n";

           if ( mysecret != programmed_secret ) { 
              handleHTTPplus("Expecting 'secret' as part of  request for user list, didn't get it.");  
              return false;
           } else { 
             // secret was accepted OK, say nothing about it here.
           }
          
        }

        // we're about to get a whole new list ,so discard the old one. :-) 
        clear_rfids_from_eeprom();
        
        // we'll also want to know when this was last done: 
        unsigned long now_epoch_secs = timeClient.getEpochTime();

        // output imprint of exactly when this even occurred
        Serial.println(now_epoch_secs);
        toclient += "TIME_STAMP:";  
        toclient += now_epoch_secs;  //seconds since Jan. 1, 1970
        toclient += "<br>\n";  
        // and also save that in eeprom as 4bytes ( a long) in eeprom offsets 96-99 ( tags start from 100)
        //we're abusing the tag-writing function to do the same thing, but at a out-of-spec "offset".
        tagcache.rfid_tag = now_epoch_secs;
        cache_rfid( 96, tagcache );   // we can later read this with get_long_from_offset(96) 

        // human readable date
        String Str_now = get_nice_date_from_epoch(now_epoch_secs); 
        // output it to both places:
        Serial.println(Str_now);
        toclient += Str_now;

        String remainder = from_client ;

        int find_nl = 0;
        int stuck_in_loop = 0;
        while (find_nl != -1 ) { 
          //int start_of_string = 0;
          find_nl = remainder.indexOf('\n');  // starts off as entire string .  indexOf() returns -1 if no string found.
          
          String first_line = remainder.substring(0,find_nl); // get the first line of the text block that is remaining

          unsigned long ul = convert_to_ulong(first_line); 
          if ( ul == 0 ) {  
            if ( ! first_line.startsWith("secret:") ) {  // we know this line isn't a ulong, ignore it here.
              Serial.print("ulong conversion failed, sorry for: "); Serial.println(first_line); 
              toclient += "ulong conversion failed, sorry for: ";  toclient += first_line; toclient += "<br>\n";  
            }
          } 
         
          remainder = remainder.substring(find_nl+1);  // get the rest. to the end of the string.

         // now we have the ulong, save it to the eeprom, ! 

           if ( ul != 0 ) { 
                // identify offset for next tag to be written to...
               next_empty_slot = find_next_empty_slot(); 
               // then write it
               tagcache.rfid_tag = ul; 
               cache_rfid( next_empty_slot,tagcache ) ;   
               // then expire the offset 
               next_empty_slot = -1; // forget the previously known offset, we don't wnat to point ot eh wrong place.
           }


          stuck_in_loop++;
          if ( stuck_in_loop > 255 ) { find_nl = -1; Serial.println("stuck_in_loop triggered.err."); } // break out of loop if we got stuck ofr unknown reason or bad parse.
        } 


        int cached_eeprom_count = display_tags_in_eeprom(); 
        String num = String(cached_eeprom_count); 
        toclient +=  "TOTAL_CACHED:"; 
        toclient +=  num;
        toclient += "<br>\n";  

        // Wifi HTTP print the same list. all of them.
        String tagstring = get_tags_in_eeprom();
        toclient +=  "<pre>SAVED_LIST:\n"; 
        toclient += tagstring;
        toclient += "</pre>\n";  

        //if this was a POST, say thx. 
        HTTP.send(200, "text/html", "<h1>NEW USERS LIST SAVED! RESULTS BELOW.</h1><br>\n"+toclient);
        //x.stop();


}


void CheckFlashConfig() {

    uint32_t realSize = ESP.getFlashChipRealSize();
    uint32_t ideSize = ESP.getFlashChipSize();
    FlashMode_t ideMode = ESP.getFlashChipMode();

    Serial.printf("Flash real id:   %08X\n", ESP.getFlashChipId());
    Serial.printf("Flash real size: %u\n", realSize);
    Serial.printf("Flash ide  size: %u\n", ideSize);
    Serial.printf("Flash ide speed: %u\n", ESP.getFlashChipSpeed());
    Serial.printf("Flash ide mode:  %s\n", (ideMode == FM_QIO ? "QIO" : ideMode == FM_QOUT ? "QOUT" : ideMode == FM_DIO ? "DIO" : ideMode == FM_DOUT ? "DOUT" : "UNKNOWN"));

    if(ideSize != realSize) {
        Serial.println("Flash Chip configuration wrong!\n");
    } else {
        Serial.println("Flash Chip configuration ok.\n");
    }

    //delay(1000);
}


// Start NTP only after IP network is connected
void onSTAGotIP(WiFiEventStationModeGotIP ipInfo) {
  Serial.printf("Got IP: %s\r\n", ipInfo.ip.toString().c_str());
  //NTP.begin("pool.au.ntp.org", 1, true);
  //NTP.setInterval(63);
    timeClient.begin();
 // digitalWrite(ONBOARDLED, LOW); // Turn on LED
}

// Manage network disconnection
void onSTADisconnected(WiFiEventStationModeDisconnected event_info) {
  Serial.printf("Disconnected from SSID: %s\n", event_info.ssid.c_str());
  Serial.printf("Reason: %d\n", event_info.reason);
  //digitalWrite(ONBOARDLED, HIGH); // Turn off LED
  //NTP.stop(); // NTP sync can be disabled to avoid sync errors
  timeClient.end();
}

// only called by GPIO0 being pulled to GND. ( ie pushbutton ) 
void handleInterrupt() {
  interruptCounter++;
}

// with software PWM, and vals in range 0-1023
void internal_green_on() { analogWrite(GREEN_ONBOARD_LED, 0 ); }  // it's an inverted logic LED 0 = ON
void internal_green_half() { analogWrite(GREEN_ONBOARD_LED, 512 ); }
void internal_green_off() { analogWrite(GREEN_ONBOARD_LED, 1024 ); }  //1024 = OFF

void red_on() { digitalWrite(RED_LED, 1 ); }  
void red_off() { digitalWrite(RED_LED, 0 ); }  
void external_green_on() { digitalWrite(GREEN_EXTERNAL_LED, 1 ); }  
void external_green_off() { digitalWrite(GREEN_EXTERNAL_LED, 0 ); }  

// well, since millis() is 1000 in a second, and we need approx 1024 intervals for the brightness 
// we'll naturally get a 5hz cycle  at %5000 or heartbeat or pulse, we'll need to call this constantly in the main loop.
//the /2 makes it darker ,the 1024- puts it at the darker end of the spectrum as this LED is wired HIGH=OFF
void internal_green_pulse() { int m =  millis()%5000; int d = m > 2500?1:-1; int brightness = m/5*d; analogWrite(GREEN_ONBOARD_LED, brightness ); }
// different to green, as is a digial , not a PWM
//void eternal_green_pulse() { int m =  millis()%2000; int d = m > 1000?1:1; digitalWrite(GREEN_EXTERNAL_LED, d ); }


void setup() {
        Serial.begin(19200);
        delay(1000);
        Serial.println("begin");

        CheckFlashConfig();

        // LED and relay
        pinMode(GREEN_ONBOARD_LED, OUTPUT);     // Initialize the  pin as an output
        pinMode(RED_LED, OUTPUT);     // Initialize the  pin as an output
        pinMode(GREEN_EXTERNAL_LED, OUTPUT);     // Initialize the  pin as an output

        //set initial LED state/s to off
        digitalWrite(RED_LED, LOW);   // Turn the LED on (Note that LOW is the voltage level
        digitalWrite(GREEN_EXTERNAL_LED, LOW);   // Turn the LED on (Note that LOW is the voltage level
        digitalWrite(GREEN_ONBOARD_LED, HIGH);   // Turn the LED on (Note that LOW is the voltage level


        pinMode(RELAY_ONBOARD, OUTPUT);     // Initialize the pin as an output
        pinMode(interruptPin, INPUT_PULLUP); // pushbutton on GPIO0
        
        attachInterrupt(digitalPinToInterrupt(interruptPin), handleInterrupt, FALLING);

        #ifdef USE_NEOPIXELS
        NEO.begin(); // optional RGB led/s. 
        statusLight('b'); // blue on bootup till we have a wifi signal
        #endif

        // epprom has rfid tags cached.
        EEPROM.begin(EEPROM_SIZE); // any number up to 4096.  don't forget EEPROM.commit() after EEPROM.write()

        Serial.print("LATEST EEPROM POST: "); 
        Serial.println(get_long_from_offset(96));

        
        Serial.print("Configuring as Station, NOT AP...");
        // Connect to pre-existing WiFi network with WIFI_STA
        WiFi.disconnect();
        // DYNAMIC or SERVER ASSIGNED IP SETUP
        //WiFi.mode(WIFI_STA); // WIFI_AP or WIFI_STA
        //WiFi.begin(ssid,password );
        //OR:
        // STATIC IP ASSIGNMENT..      
        WiFi.mode(WIFI_STA);
        IPAddress ipa;   ipa.fromString(Cipa); 
        IPAddress gate;  gate.fromString(Cgate);
        IPAddress subnetmask; subnetmask.fromString(Csubnetmask);
        
        WiFi.hostname(deviceName.c_str());      // DHCP Hostname (useful for finding device for static lease)
        WiFi.config(ipa, gate, subnetmask);  // (DNS not required)
        WiFi.begin(ssid, password);

        // on wifi connect and wifi disconnect events, we get notified. 
        WiFi.onStationModeGotIP(onSTAGotIP);// As soon WiFi is connected, start NTP Client
        WiFi.onStationModeDisconnected(onSTADisconnected);


        // Wait for connection
        Serial.println("trying to connect to WIFI.....");
        int tries = 0;
        while ((WiFi.status() != WL_CONNECTED ) && (tries < 50 ) ){
          Serial.println(".....");
          delay(200);
          tries++;
          if ( tries %2 ==  0 ) { external_green_off();  internal_green_off(); red_off();  } else { external_green_on();  internal_green_on(); red_on();  } 
          //digitalWrite(GREEN_ONBOARD_LED, !digitalRead(GREEN_ONBOARD_LED) ); 
        }
        //WIFI fail
        if (tries >= 30 ) {  
          // REBOOT
          Serial.println("rebooting.");
          
          ESP.restart(); //this hangs the first time after a device was serial programmed.
          //WiFi.forceSleepBegin(); wdt_reset(); ESP.restart(); while(1)wdt_reset(); // this is an attempt at a workaround to the above, but not worky.
        }
        
        Serial.println("Connected!");
        myIP = WiFi.localIP();
        Serial.print("STA IP address: ");
        Serial.println(myIP);

        // after wifi is setup, default it to this: 
        external_green_off(); red_off(); internal_green_on();
        
        #ifdef USE_NEOPIXELS
        statusLight('r'); // NEO goes blue->red after wifi is connected.
        #endif

        #ifdef USE_OTA

            Serial.println("OTA ACTIVE!");
            // Port defaults to 8266
            // ArduinoOTA.setPort(8266);
          
            // Hostname defaults to esp8266-[ChipID], we set it to the access control name
            ArduinoOTA.setHostname(deviceName.c_str());
          
            // No authentication by default
            //ArduinoOTA.setPassword((const char *)"admin");
          
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
            Serial.println("Ready");
            Serial.print("IP address: ");
            Serial.println(WiFi.localIP());
        #endif // USE_OTA


          // be a HTTP server:  - not totally critical for this, but it's working, so we might use it later for config.
        HTTP.on("/", handleHTTP);
        HTTP.on("/index.html", handleHTTP); // same as above
        Serial.println("HTTP server started");
        HTTP.on("/users", handleHTTPusers); // same as above
        Serial.println("HTTP server started");


        HTTP.begin();
 
        //NOTE: NTP is not started here, it's started as soon as the wifi interface is "up", but it could have probably been.

//        #ifdef OTHERUDP
//        // we listen, all the time, for UDP data coming IN. ( Waiting for UDP on 5554 )
//        Udp.begin(navPort); //Open port for navdata
//        Udp.flush();
//        #endif
          
        
        Serial.println("Starting main loop  "); 

}

void card_ok_entry_permitted() { 
    digitalWrite(RELAY_ONBOARD, RELAY_UNLOCK);
    external_green_on();
    internal_green_on();
    red_off();
    delay(50);
    //digitalWrite(GREEN_ONBOARD_LED, HIGH); // LOW = ON for this LED, so turns green led OFF while door is OPEN. TODO maybe flash fast = good? ..? 
    // it gets pulled low elsewhere after some delay.
}

// fast-flash-30-times for denied. ( thats ~3 secs ) 
void card_ok_entry_denied() { 

    for ( int x = 0 ; x < 30 ; x++ ) { 
    //digitalWrite(GREEN_ONBOARD_LED, HIGH);
    red_off();
    delay(50);
    //digitalWrite(GREEN_ONBOARD_LED, LOW);
    red_on();
    delay(50);
    } 
    red_off();
}

// returns -1 on unhandled error, 0 if remote server denies it, and 1 if remote server permits it.
int remote_server_says_yes( unsigned long tag, unsigned long int door, String name ) { 

    Serial.print("Connecting to ");
    Serial.println(host);

    lastAttemptTime = millis(); // tell the network-checker code we just ATTEMPTED  a network check,


    // Use WiFiClient class to create TCP connections
    WiFiClient client;
    if (!client.connect(host, httpPort)) {
      Serial.println("http outbound connection failed");
      return -1;
    }

    Serial.println(F("http client connected"));
    
    client.print("GET /logger.php?secret="); 
    client.print(programmed_secret);
    client.print("&q=");
    client.print(tag);
    client.print("&d=");
    client.print(door);
    client.print("&n=");
    client.println(name);
    client.println();

    // delay some arbitrary amount for the server to respond to the client. say, 1 sec. ?
    //delay(2000);
    // wait at most 2000ms for some data from client, don't wait that long if we din't have to.
    unsigned long timeout = millis();
      while (client.available() == 0) {
        if (millis() - timeout > 2000) {
          Serial.println(">>> Client Timeout !");
          client.stop();
          return -1;
        }
      }

      
      // Read all the lines of the reply from server and print them to Serial
      while(client.available()){
        String line = client.readStringUntil('\r');
        Serial.print(line);
        if (line.startsWith("access:1") ) { 
            Serial.println("permission GRANTED by remote server");
            lastConnectionTime = millis(); // tell the network-checker code we just SUCCEEDED  a network check
           return 1; 
        }
        if (line.startsWith("access:0") ) { 
            Serial.println("permission DENIED by remote server");
            lastConnectionTime = millis(); // tell the network-checker code we just SUCCEEDED  a network check
           return 0; 
        }
      }
      Serial.println();
      Serial.println("closing connection");
      
      client.stop();
    
      return -1; // shouldn't get here, but if we do, consider it an error as we didn't get a suitable response from the server. 
}


// to be valid, its got to be in either the onboard eeprom cache, OR the remove server had to respond with 'access:1'
bool rfid_is_valid() { 
// rfidreadbytes[] global stored the last-read RFID tag.


    // convert raw bytes into the "long" we have in eeprom: ( ignoring the checksum for now. ) 
    unsigned long rfid_tag = 0;
    //rfid_tag = 0;
    rfid_tag += rfidreadbytes[0] << 24;
    rfid_tag += rfidreadbytes[1] << 16;
    rfid_tag += rfidreadbytes[2] << 8;
    rfid_tag += rfidreadbytes[3];
    

    Serial.println("looking for tag (as bytes): ");
    for(int i = 0; i < eot ; i++)
    {
      Serial.print(" ");
      Serial.print(rfidreadbytes[i]);
    }
      Serial.print(" (as long):");
      Serial.print(rfid_tag);
      Serial.println();


    // identify offset for next tag to be written to...
    next_empty_slot = find_next_empty_slot(); 

    // print the last of eeprom stored tags, all of them: 
    display_tags_in_eeprom(); 

    // quietly scan for tag now
    bool found_in_eeprom = find_requested_tag_in_eeprom(rfid_tag);
    if ( found_in_eeprom ) return true; 

    // check if remote server sats it's good..? 
    if ( remote_server_says_yes(rfid_tag,42,deviceName)  == 1 ) { 
        Serial.println("remote server approved tag -OK-.");
        tagcache.rfid_tag = rfid_tag;
        
        if ( found_in_eeprom == false ) {  // precaution: don't write it more than once to the eeprom
          cache_rfid( next_empty_slot,tagcache ) ;    
          next_empty_slot = -1; // forget the previously known offset, we don't wnat to point ot eh wrong place.
        }
        return true; 
    }

  return false;
} 


int find_next_empty_slot( ) { 

      //Serial.println("Current tags in eeprom already:");
    // scan for tag, and also next empty slot.
    bool found_in_eeprom = false;
    next_empty_slot = -1;
    for(int i=MEMORY_HEADER_LEN; i < EEPROM_SIZE ;i+=MEMORY_RFID_LENGTH)
    {
        EEPROM_readAnything(i, tagcache);
          
        // if its the first empty slot..? 
        if ( tagcache.rfid_tag == 4294967295 ) { 
          if ( next_empty_slot == -1 ) { 
            next_empty_slot = i;
            Serial.print("next: ");  Serial.println(next_empty_slot); 
          }
        }
    }
    return next_empty_slot;
} 

int find_requested_tag_in_eeprom(unsigned long rfid_tag) {
    // quietly scan for tag now
    bool found_in_eeprom = false;
    for(int i=MEMORY_HEADER_LEN; i < EEPROM_SIZE ;i+=MEMORY_RFID_LENGTH)
    {
        EEPROM_readAnything(i, tagcache);
        
        // does it match the user requested tag..? 
        if ( tagcache.rfid_tag == rfid_tag ) { 
           // found tag in cache! 
           Serial.println("tag match in eeprom found -OK-.");
           found_in_eeprom = true;
        }    
    }
    return found_in_eeprom;
}


int display_tags_in_eeprom( ) { 
    Serial.println("Current tags in eeprom:");
    // scan for tag, and also next empty slot.
    int tagcount = 0;
    for(int i=MEMORY_HEADER_LEN; i < EEPROM_SIZE ;i+=MEMORY_RFID_LENGTH)
    {
        EEPROM_readAnything(i, tagcache);
        
        // display non-empty slots 
        if ( tagcache.rfid_tag != 4294967295 ) {    // that's 32bits of 1  ie 11111111111111111111111111111111, which means the eeprom is blank at that location
          Serial.print(i); Serial.print(": "); 
          Serial.println(tagcache.rfid_tag ); // display tag just read off eeprom to user.
          tagcount++;
        } 
    }
    return tagcount;
}

// outputs nothing, returns them as a \n separated list in a String object. ( so its easy to print or output later. ) 
String get_tags_in_eeprom( ) { 
    //Serial.println("Current tags in eeprom:");
    // scan for tag, and also next empty slot.
    //int tagcount = 0;
    String str = "";
    for(int i=MEMORY_HEADER_LEN; i < EEPROM_SIZE ;i+=MEMORY_RFID_LENGTH)
    {
        EEPROM_readAnything(i, tagcache);
        
        // display non-empty slots 
        if ( tagcache.rfid_tag != 4294967295 ) {    // that's 32bits of 1  ie 11111111111111111111111111111111, which means the eeprom is blank at that location
          //str += i; str += ": "; 
          str += tagcache.rfid_tag; // display tag just read off eeprom to user.
          str += "\n";
          //tagcount++;
        } 
    }
    return str;
}

// returns ZERO when slot is empty, NOT 4294967295
unsigned long get_long_from_offset( int offset ) { 

  tagcache.rfid_tag = 4294967295; // wipe that global cache b4 use, just in casse.
  EEPROM_readAnything(offset, tagcache);

  if (tagcache.rfid_tag >= 4294967295 ) {
    return 0;
  }
  
  return tagcache.rfid_tag;

  
}
          

void cache_rfid(int next_empty_slot, struct config_t tagcache) { 
// writes it to the requested eeprom offset:

    // tmp hack, any tag not found is added to teh cache at the next empty slot. 
    Serial.print("writing tag to slot... ");
    Serial.print(next_empty_slot);  Serial.print(": ");
    Serial.print(tagcache.rfid_tag);
    EEPROM_writeAnything(next_empty_slot, tagcache);
    EEPROM.commit(); // yes, I really want it to persist. 
    Serial.println(" done");
  
}



void clear_rfids_from_eeprom() { 
    for(int i=MEMORY_HEADER_LEN; i < EEPROM_SIZE ;i+=MEMORY_RFID_LENGTH)
    {
      EEPROM_writeAnything(i, (unsigned long) 4294967295);
    }
    EEPROM.commit(); // yes, I really want it to persist. 
}

int heap = 0;
int prevheap = 0;

void loop() {
  //Serial.println("loop");

   HTTP.handleClient(); // http server requests need handling... 

   timeClient.update(); // NTP packets   for current time do this:   Serial.println(timeClient.getFormattedTime());

    ArduinoOTA.handle(); // this should allow us to OTA-update this device.

   // heap diagnostics
   heap = ESP.getFreeHeap();
   if ( abs(heap - prevheap) > 100) {  // allow small movements without reporting.
     Serial.printf("free heap size: %u\n", heap);
     prevheap = heap;
   }

  #ifdef ENABLE_ESTOP_AS_EGRESS_BUTTON
  if ( interruptCounter  > 0 ) { 
    card_ok_entry_permitted();  //calling another functon from interrupt context can generate crashes
    previousMillis = millis(); // this triggers a door-lock event a few seconds later.
    interruptCounter = 0;
  }
  #endif


  internal_green_pulse(); // show heartbeat on internal LED when we do nothing else.

  //unsigned long most_recent_time = lastConnectionTime > lastAttemptTime ? lastConnectionTime : lastAttemptTime;

  // we re-attempt things approx every minute 
  if ((unsigned long)(millis() - lastAttemptTime) > (unsigned long)(pollingInterval*1000) ) {
     Serial.println(F("network poll checking now.... "));

     unsigned long test_code = 1234567890;
    if ( remote_server_says_yes(test_code,42,deviceName) >= 0  ) {   // in thise case, if the server permits OR denies, both are valid comms with the server.
        Serial.println("....remote server COMMS TEST tag OK.");
        // lastConnectionTime is also updated inside the above function call, so we don't need to do it here. 
    } 
    
  }
  // if we go more than 5 minutes without an actual link, reboot! 
  // now do something extreme if we go for 5 minutes without hearing from the server:  ( ie 5 times the polling interval )
  if ((unsigned long)(millis() - lastConnectionTime) > (unsigned long)(pollingInterval*1000*5) ) {
    Serial.println("Rebooting becuase network was offline for more than 5 minutes.");
    Serial.flush();
    delay(5);
    ESP.restart(); //this hangs the first time after a device was serial programmed.
  }


  // this little loop relies on the serial RFIS data getting into our serial buffer smartly, so it's all there before our delay(2) runs out.
  while ( readerSerial.available() ) { 
    // eot means we think it's the end-of-transmission for the rfid byte data
    if ( eot == 0 ) { Serial.print("\nbytes:"); } 
    rfidreadbytes[eot] =  readerSerial.read();
    Serial.print(rfidreadbytes[eot]);        Serial.print(" ");
    eot++;
    delay(2); // just to give a tiny bit more time for the bytes to arrive before we decide the 'read' is done.
  }

//  #ifdef OTHERUDP
//  // wait for UDP packet on port 5554, then break it open and use it. :-) 
//  if(Udp.parsePacket()) {
//      int len = Udp.read(incoming, 1024);
//      //Serial.print("packet len:");
//      //Serial.println(len);
//      // TODO if len != 16 skip all this, as it's a dud packet.
//      //
//      incoming[len] = 0;   
//      //for ( int b = 0 ; b < channel_count ; b ++ ) { 
//         byte t1 = incoming[2];
//         byte t2 = incoming[2+1];
//         short s = (short)(t1 << 8)  | (short)t2;        
//        Serial.print("\t");
//        Serial.print(s);
//        // TODO get something meaningful from the UDP packet and acton it. 
//        // 
//       // if (s > 4000 ) {  Serial.print("\nEEK 4000 EEK EEK EEK EEK EEk !!!!!!!!"); break;  } 
//       // if (s < 400 ) {  Serial.print("\nEEK 400 EEK EEK EEK EEK EEk !!!!!!!!"); channel_count--; break;  } 
//       //  pulselens[b] = s;
//      //} 
//      
//      Serial.println();
//
//  } 
//  #endif  

  // we've ( probably) got a full RFID tag here.   4 bytes of data and one byte of checksum at the end.
  if ( eot >= 5 ) { 
      Serial.print("\nEOT:");
      Serial.println(eot); 

       // tell user we got some sort of "scan" as early in the process as we can...
       //digitalWrite(GREEN_ONBOARD_LED, HIGH); // for this LED , HIGH = off, but it's on most of the time, so turning it OFF here works.
       external_green_on(); 
      
      if ( rfid_is_valid() ) { 
        Serial.println("...Permitted entry.");
        // open the door and show user other signs ass approriate.
        card_ok_entry_permitted();
        internal_green_on();
      } else { 
        Serial.println("...Denied entry.");
        // warn user that they have been denied entry
        card_ok_entry_denied();
        red_on();
      }

      previousMillis = millis(); // this is the time we turned it on. 
      // this will let *any* card in 
      //ledState = HIGH;
      //digitalWrite(13, ledState); // LED
      //digitalWrite(12, ledState); // and RELAY

      eot = 0;  // done with that tag, lets forget it. :-) 
  }
  if ( ( eot < 5) && (eot > 0) ) { 
    Serial.println("incomplete or corrupted RFID read, sorry. ");
    while ( readerSerial.available() ) readerSerial.read(); // flush any remaining bytes.
    eot = 0;
  }

   // turn it off some time later. 
   unsigned long currentMillis = millis();

   if ((currentMillis - previousMillis >= interval) && (  previousMillis != 0 ) )  {

        Serial.println("RELOCKED-DOOR");
    
        // set the LED with the ledState of the variable:
        //digitalWrite(13, LOW); // LED
        digitalWrite(12, LOW); // and RELAY
        internal_green_off();
        external_green_off();
        red_off();

        previousMillis = 0;
        
   }
      
}

// date helpers stashed at the end of the code as they are used together 

#define RTC_LEAP_YEAR(year) ((((year) % 4 == 0) && ((year) % 100 != 0)) || ((year) % 400 == 0))
/* Days in a month */
static uint8_t RTC_Months[2][12] = {
  {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31}, /* Not leap year */
  {31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31}  /* Leap year */
};
void GetDateTimeFromUnix( uint32_t unix) {
    uint16_t year;      
    //data->Unix = unix;          /* Store unix time to unix in struct */
    Seconds = unix % 60;  /* Get seconds from unix */
    unix /= 60;                 /* Go to minutes */
    Minutes = unix % 60;  /* Get minutes */
    unix /= 60;                 /* Go to hours */
    Hours = unix % 24;    /* Get hours */
    unix /= 24;                 /* Go to days */
    WeekDay = (unix + 3) % 7 + 1; /* Get week day, monday is first day */

    year = 1970;                /* Process year */
    while (1) {
        if (RTC_LEAP_YEAR(year)) {
            if (unix >= 366) {
                unix -= 366;
            } else {
                break;
            }
        } else if (unix >= 365) {
            unix -= 365;
        } else {
            break;
        }
        year++;
    }
    /* Get year in xx format */
    Year = (uint8_t) (year - 2000);
    /* Get month */
    for (Month = 0; Month < 12; Month++) {
        if (RTC_LEAP_YEAR(year)) {
            if (unix >= (uint32_t)RTC_Months[1][Month]) {
                unix -= RTC_Months[1][Month];
            } else {
                break;
            }
        } else if (unix >= (uint32_t)RTC_Months[0][Month]) {
            unix -= RTC_Months[0][Month];
        } else {
            break;
        }
    }

    Month++;            /* Month starts with 1 */
    Day = unix + 1;     /* Date starts with 1 */
}

#ifdef USE_NEOPIXELS
char statusLight(char color) {
  switch (color) {
    case 'r':
      {
        NEO.setPixelColor(0, 250, 0, 0);
        break;
      }
    case 'g':
      {
        NEO.setPixelColor(0, 0, 250, 0);
        break;
      }
    case 'b':
      {
        NEO.setPixelColor(0, 0, 0, 250);
        break;
      }
    case 'y':
      {
        NEO.setPixelColor(0, 255, 100, 0);
        break;
      }
  }
  NEO.show();
}

#endif

