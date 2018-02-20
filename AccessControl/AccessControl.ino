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
#include "Songs.h"  // monophonic music support


// when compiling for Sonoff, use : Tools -> Board -> 'Generic ESP8266 Module', and then press-and-hold the "Press to Exit" button while pluggin in FTDI usb cable to pc.
// when compiling for Wemos Mini D1: Tools -> Board -> 'WeMos D1 R2 & mini' ( its reset method is "nodemcu")  there is no need press any hardware button/s during upload or power on.


#define USE_OTA 1
#ifdef USE_OTA
#include <ArduinoOTA.h>
#endif

// doto make this used everywhere as reqd: 
#define ISDOOR 1
#define ISINTERLOCK 2
#define INTERLOCK_OR_DOOR ISDOOR 
//#define INTERLOCK_OR_DOOR ISINTERLOCK 



// for now, the OLED display just display/s a clockface.
//#define USE_OLED_WEMOS 1
#ifdef USE_OLED_WEMOS
#include <Wire.h>  // Include Wire if you're using I2C
#include <SFE_MicroOLED.h>  // Include the SFE_MicroOLED library
#define OLED_RESET 255  //
#define DC_JUMPER 0  // I2C Addres: 0 - 0x3C, 1 - 0x3D
MicroOLED oled(OLED_RESET, DC_JUMPER);  // I2C Example
// Use these variables to set the initial time
int hours = 11;
int minutes = 50;
int seconds = 30;
// How fast do you want the clock to spin? Set this to 1 for fun.
// Set this to 1000 to get _about_ 1 second timing.
const int CLOCK_SPEED = 1000; // TODO, sync this with the NTP time we actually have onboard.
// Global variables to help draw the clock face:
const int MIDDLE_Y = oled.getLCDHeight() / 2;
const int MIDDLE_X = oled.getLCDWidth() / 2;
int CLOCK_RADIUS;
int POS_12_X, POS_12_Y;
int POS_3_X, POS_3_Y;
int POS_6_X, POS_6_Y;
int POS_9_X, POS_9_Y;
int S_LENGTH;
int M_LENGTH;
int H_LENGTH;
unsigned long lastDraw = 0;
#endif

// START DEFINES --------------------------------------------------------
#define HW_SONOFF_CLASSIC 0
#define HW_WEMOS_D1 1
#define HW_OTHER 2
#define HW_NOG_TH16 3

// uncomment only one of thee:
//#define HARDWARE_TYPE  HW_SONOFF_CLASSIC
//#define HARDWARE_TYPE  HW_WEMOS_D1
//#define HARDWARE_TYPE  HW_OTHER
#define HARDWARE_TYPE  HW_NOG_TH16


#if HARDWARE_TYPE == HW_SONOFF_CLASSIC
#define GREEN_ONBOARD_LED 13  //  classic sonoff has a onboard LED, and we use GPIO13 for it.
//#define USE_NEOPIXELS 0
#define RELAY_ONBOARD 12  // TODO check this.
#endif

#if HARDWARE_TYPE == HW_WEMOS_D1
#define RGBLEDPIN            D3  // GPIO0
#define USE_NEOPIXELS 1
#define RELAY_ONBOARD 12   // THIS IS good.
#endif

#if HARDWARE_TYPE == HW_OTHER    // nogs interlock HW on wood saw.. ( untested with this code) 
#define RGBLEDPIN            14  
#define USE_NEOPIXELS 1
#define RELAY_ONBOARD 12 
#endif

#if HARDWARE_TYPE == HW_NOG_TH16    // nogs DOOR hardware with TH16, two round metal connectors on case ( a 2 pin for solenoid/door and a 4 pin for rfid etc ) 
#define GREEN_ONBOARD_LED 13
#define RELAY_ONBOARD 12 
#endif


#ifndef RELAY_ONBOARD
#error you must define RELAY_ONBOARD to match your hardware or no relay will be toggled.
#endif

// When we setup the NeoPixel library, we tell it how many pixels ( in this case, just one), and which pin to use to send signals.
// Note that for older NeoPixel strips you might need to change the third parameter--see the strandtest
// example for more information on possible values.
#ifdef USE_NEOPIXELS
#include <Adafruit_NeoPixel.h>
Adafruit_NeoPixel NEO = Adafruit_NeoPixel(1, RGBLEDPIN, NEO_RGB + NEO_KHZ800);
#endif



// IMPORTANT thse two change around depending on the type of door hardware that's attached to the relay.  
// some doors are apply-power-to-open, and some are apply-power-to-lock
#define RELAY_UNLOCK 1  
#define RELAY_LOCK 0


// THREE external LEDS  GREEN-WHITE-RED   , where the green and red are GPIO connected, and the "white" is just a power indicator.
 // GPIO4 =  lowest pin on box , yellow wire inside.
 // GPIO5 =  2nd from the end, next to 4.  
#define GREEN_EXTERNAL_LED 5
//#define WHITE_LED , always powered on.
#define RED_LED 4

#define EEPROM_SIZE 1024
#define MEMORY_HEADER_LEN 100
#define MEMORY_RFID_LENGTH 4


// on SONOFF GPIO0 is the oboard bushbutton, but an external button can be soldered over-the-top so either/both work.  any gpio except GPIO16 is ok.
#define ENABLE_ESTOP_AS_EGRESS_BUTTON 1

#if HARDWARE_TYPE == HW_SONOFF_CLASSIC
#define EGRESS_OR_ESTOP 0 
#endif
#if HARDWARE_TYPE == HW_WEMOS_D1
#define EGRESS_OR_ESTOP D4 // aka GPIO2
#endif
#if HARDWARE_TYPE == HW_OTHER
#define EGRESS_OR_ESTOP 0
#endif
#if HARDWARE_TYPE == HW_NOG_TH16    
#define EGRESS_OR_ESTOP 0   // GPIO0
#endif

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

// must compile as 'generic esp8266 module' for this to work..? 
#define RFID_RX 14

#if HARDWARE_TYPE == HW_WEMOS_D1
#define RFID_RX 13   // pin labeled D7 = GPIO13
#endif

#if HARDWARE_TYPE != HW_NOG_TH16    
SoftwareSerial readerSerial(RFID_RX, SW_SERIAL_UNUSED_PIN); // reader/s RX, TX is usually on it's own GPIO, except for HW_NOG_TH16
#endif

#if HARDWARE_TYPE == HW_NOG_TH16 // RFID tag reader is on same RX/TX as programming port / debug console, and is unplugged during programming. 
#define readerSerial Serial
#endif


ESP8266WebServer HTTP(80);

IPAddress myIP; // us, once we know it.

//  a client of this TX wifi network:
const char *ssid = "HSBNEWiFi";
const char *password = "HSBNEPortHack";
//const char *ssid = "PRETTYFLYFORAWIFI";
//const char *password = "qwer1234";

// static IP address assigment, gateway, and netmask.
char * Cipa = "10.0.1.221";
char * Cgate = "10.0.1.254";
//char * Cipa = "192.168.192.220";
//char * Cgate = "192.168.192.1";
char * Csubnetmask = "255.255.254.0";

String deviceName = "BuzzTest2"; // needs to exact match name displayed here: http://porthack.hsbne.org/access_summary.php
String deviceNameLong = deviceName+"-Door"; // for OTA,etc make name a bit longer.


// passed to logger.php, and also expected in the POST to verify it's legit.
String programmed_secret = "asecret"; 


int next_empty_slot = -1; 

// unsigned long is 4 bytes
struct config_t
{
    unsigned long rfid_tag;
} tagcache;

int eot = 0; // position of most recentkly read byte in rfidreadbytes structure, so repreenting the 'end of tag' in the array.

byte rfidreadbytes[50]; // in a perfect world, we'll never use more than 5 or 12, but to reduce buffer overflow/s it's a bit longer.

// remote web server
const char* host = "10.0.1.253";
const int httpPort = 80;
unsigned long lastConnectionTime = 0; // most recent time we had any comms on the http/wifi interface. 
unsigned long lastAttemptTime = 0; // most recent time we tried to have any comms on the http/wifi interface. 

#if INTERLOCK_OR_DOOR == ISDOOR 
unsigned long pollingInterval = 60;        // maximum 60 secs between network checks, in seconds. Also, being offline for 5 in a row ( 5 mins ) of these causes a hard reboot.
#endif
#if INTERLOCK_OR_DOOR == ISINTERLOCK 
unsigned long pollingInterval = 600;        // maximum 600 secs between network checks, in seconds. Also, being offline for 5 in a row ( 50 mins ) of these causes a hard reboot.
#endif


//// unused at present.
//#ifdef OTHERUDP
//char incoming[1024]; // buff for recieving UDP when in STA mode.
//const int navPort = 5554;
//WiFiUDP Udp; // this will be the BROADCAST address we send to, if we use it. 
//IPAddress broadcast;
//#endif

unsigned long previousMillis = 0;        // will store last time LED/relay was updated

#if INTERLOCK_OR_DOOR == ISDOOR 
const long interval = 4000; // how long it's OPEN for..  4 seconds is enough to go thru a door
#endif

#if INTERLOCK_OR_DOOR == ISINTERLOCK
const long interval = 30000; // how long it's OPEN for..  TEST = 30 secs.   live: 4000*3600 = 4 hrs should be enough to do a laser job.
#endif

int ledState = LOW; 

// if there's been any change in the data, and we'll be formulating a new UDP packet..
int udpnew = 0;

// buzz notes: 
// https://wiki.wemos.cc/products:d1_mini_shields:oled_shield
// oled sheild uses D1 and D2 



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

        // display the url that we can in at, back to the user, for debug only
        toclient += HTTP.uri(); // 
        toclient += "<br>\n"; // 

        // RESPOND TO SPECIFIC ARGS FROM USER, IF GIVEN, specifically allow /?clear=1 ( bit like a nettrol ) to be equivalent to /clear 
        if ( HTTP.arg("clear")  != ""){     //Parameter not found
             Serial.print("A /?clear=1 CLEAR CLEAR CLEAR.\n");
              toclient += "a /?clear=1 CLEAR request for user list actioned.! <br>\n";
              //  discard the eeprom list :-) 
              clear_rfids_from_eeprom();
        }
        if ( HTTP.arg("open")  != ""){     //Parameter not found
             Serial.print("A /?open=1 requested \n");
              toclient += "A /?open=1 requested, doing it *now*......<br>\n";

              // TODO security ..? 
              // open door now. ( it will self-close after X amount of time. )
              card_ok_entry_permitted(); // not really a 'card' request, faking it is ok for this. 
              previousMillis = millis();
        }
        if ( HTTP.arg("close")  != ""){     //Parameter not found
             Serial.print("A /?close=1 requested\n");
              toclient += "A /?close=1 requested, ignored as esp8266 doors self-close.<br>\n";
              // not needed, door self-locks
        }

        // GENERIC DISPLAY TO USER: 
        
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

bool handleHTTPclear() {
        Serial.print("A /clear http client connected, fyi.\n");

        // we're about to get a whole new list ,so discard the old one. :-) 
        clear_rfids_from_eeprom();

        handleHTTPplus("CLEAR request for user list actioned.! <br>\n");  

} 

bool handleHTTPopen() {
        Serial.print("A /open http client connected, fyi.\n");

        card_ok_entry_permitted();
        previousMillis = millis();

        handleHTTPplus("/open request actioned.! <br>\n");  

} 

bool handleHTTPclose() {
        Serial.print("A /close http client connected, fyi.\n");

        handleHTTPplus("/close request ignored. door closes itself..! <br>\n");  

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

      //OPTIONAL - enforce that http requests to /users endpoint must come from a known and trusted IP address...
      #if 0 
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
      #endif
       

        if (HTTP.hasArg("plain")== false){
           //Expecting POST request for user list, if we didn't get it, just display the default page with info.
           handleHTTPplus("Expecting POST request for user list, didn't get it.<br>");  
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
              handleHTTPplus("Expecting 'secret' as part of  request for user list, didn't get it.<br>");  
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
// these  LED function/s are not used in all hardware type/s.
void internal_green_on() { analogWrite(GREEN_ONBOARD_LED, 0 ); }  // it's an inverted logic LED 0 = ON
void internal_green_half() { analogWrite(GREEN_ONBOARD_LED, 512 ); }
void internal_green_off() { analogWrite(GREEN_ONBOARD_LED, 1024 ); }  //1024 = OFF
// well, since millis() is 1000 in a second, and we need approx 1024 intervals for the brightness 
// we'll naturally get a 5hz cycle  at %5000 or heartbeat or pulse, we'll need to call this constantly in the main loop.
//the /2 makes it darker ,the 1024- puts it at the darker end of the spectrum as this LED is wired HIGH=OFF
void internal_green_pulse() { int m =  millis()%5000; int d = m > 2500?1:-1; int brightness = m/5*d; analogWrite(GREEN_ONBOARD_LED, brightness ); }
void red_on() { digitalWrite(RED_LED, 1 ); }  
void red_off() { digitalWrite(RED_LED, 0 ); }  
void external_green_on() { digitalWrite(GREEN_EXTERNAL_LED, 1 ); }  
void external_green_off() { digitalWrite(GREEN_EXTERNAL_LED, 0 ); }  



void setup() {

        #if HARDWARE_TYPE != HW_NOG_TH16    
        Serial.begin(19200);
        readerSerial.begin(9600); 
        #endif

        // the NOG hardware has the serrila debug console on the same hardware as the RFID reader, so needs to run at the same BAUD rate as the RFID reader. 
        #if HARDWARE_TYPE == HW_NOG_TH16    
        Serial.begin(9600);
        #endif
        
        delay(1000);
        Serial.println("begin");

        CheckFlashConfig();

        // LED and relay
        #if HARDWARE_TYPE == HW_SONOFF_CLASSIC
        pinMode(GREEN_ONBOARD_LED, OUTPUT);     // Initialize the  pin as an output
        #endif
        #if HARDWARE_TYPE == HW_NOG_TH16
        pinMode(GREEN_ONBOARD_LED, OUTPUT);     // Initialize the  pin as an output
        #endif
        pinMode(RED_LED, OUTPUT);     // Initialize the  pin as an output
        pinMode(GREEN_EXTERNAL_LED, OUTPUT);     // Initialize the  pin as an output

        //set initial LED state/s to off
        digitalWrite(RED_LED, LOW);   // Turn the LED on (Note that LOW is the voltage level
        digitalWrite(GREEN_EXTERNAL_LED, LOW);   // Turn the LED on (Note that LOW is the voltage level
        #if HARDWARE_TYPE == HW_SONOFF_CLASSIC
        digitalWrite(GREEN_ONBOARD_LED, HIGH);   // Turn the LED on (Note that LOW is the voltage level
        #endif
        #if HARDWARE_TYPE == HW_NOG_TH16
        digitalWrite(GREEN_ONBOARD_LED, HIGH);   // Turn the LED on (Note that LOW is the voltage level
        #endif

        pinMode(RELAY_ONBOARD, OUTPUT);     // Initialize the pin as an output
        pinMode(interruptPin, INPUT_PULLUP); // pushbutton on GPIO0
        
        attachInterrupt(digitalPinToInterrupt(interruptPin), handleInterrupt, FALLING);

        #ifdef USE_NEOPIXELS
        NEO.begin(); // optional RGB led/s. 
        statusLight('b'); // test cycle on boot
        delay(1000);
        statusLight('r'); // test cycle on boot
        delay(1000);
        statusLight('y'); // test cycle on boot
        delay(1000);
        statusLight('g'); // test cycle on boot
        delay(1000);
        statusLight('o'); // off
        #endif


        #ifdef USE_OLED_WEMOS
        oled.begin();     // Initialize the OLED
        oled.clear(PAGE); // Clear the display's internal memory
        oled.clear(ALL);  // Clear the library's display buffer
        oled.display();   // Display what's in the buffer (splashscreen)
        initClockVariables();
        oled.clear(ALL);
        drawFace();
        drawArms(hours, minutes, seconds);
        oled.display(); // display the memory buffer drawn
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
        
        WiFi.hostname(deviceNameLong.c_str());      // DHCP Hostname (useful for finding device for static lease)
        WiFi.config(ipa, gate, subnetmask);  // (DNS not required)
        WiFi.begin(ssid, password);

        // on wifi connect and wifi disconnect events, we get notified. 
        WiFi.onStationModeGotIP(onSTAGotIP);// As soon WiFi is connected, start NTP Client
        WiFi.onStationModeDisconnected(onSTADisconnected);


        // Wait for connection
        Serial.print("trying to connect to WIFI..... ");
        Serial.print(ssid); Serial.print(" (");Serial.print(password);  Serial.println(")");

        Serial.print("Hardware Type:");
        #if HARDWARE_TYPE == HW_SONOFF_CLASSIC
        Serial.println(F("SONOFF-CLASSIC"));        
        #endif
        #if HARDWARE_TYPE == HW_WEMOS_D1
        Serial.println(F("WEMOS-D1"));        
        #endif
        #if HARDWARE_TYPE == HW_OTHER
        Serial.println(F("UNKNOWN-HARDWARE-TYPE"));        
        #endif
               
        int tries = 0;
        while ((WiFi.status() != WL_CONNECTED ) && (tries < 50 ) ){
          Serial.println(".....");
          delay(200);
          tries++;
          if ( tries %2 ==  0 ) { 
              external_green_off();  
              #if HARDWARE_TYPE == HW_SONOFF_CLASSIC
              internal_green_off(); 
              #endif
              #if HARDWARE_TYPE == HW_NOG_TH16
              internal_green_off(); 
              #endif
              red_off();  
            } else { 
              external_green_on();  
              #if HARDWARE_TYPE == HW_SONOFF_CLASSIC
              internal_green_on(); 
              #endif
              #if HARDWARE_TYPE == HW_NOG_TH16
              internal_green_on(); 
              #endif
              red_on();  
              } 
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
        external_green_off(); 
        red_off(); 
        #if HARDWARE_TYPE == HW_SONOFF_CLASSIC
        internal_green_on();
        #endif
        #if HARDWARE_TYPE == HW_NOG_TH16
        internal_green_on();
        #endif        
        #ifdef SONGS
        play_rtttl(bootupsong);
        #endif
        
        #ifdef USE_NEOPIXELS
        statusLight('y'); // NEO goes ->yellow after wifi is connected.
        #endif

        #ifdef USE_OTA

            Serial.println("OTA ACTIVE!");
            // Port defaults to 8266
            // ArduinoOTA.setPort(8266);
          
            // Hostname defaults to esp8266-[ChipID], we set it to the access control name
            ArduinoOTA.setHostname(deviceNameLong.c_str());
          
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
        HTTP.on("/index.html", handleHTTP); 
        HTTP.on("/users", handleHTTPusers); 
        HTTP.on("/clear", handleHTTPclear); 
        HTTP.on("/open", handleHTTPopen); 
        HTTP.on("/close", handleHTTPclose); 
        //HTTP.on("/?", handleHTTPclear);  doesn't work like this, so we handle /?clear via the "/" handler, but the result is the same 
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
    #if HARDWARE_TYPE == HW_SONOFF_CLASSIC
    internal_green_on();
    #endif
    #if HARDWARE_TYPE == HW_NOG_TH16
    internal_green_on();
    #endif
    
    // brifly let RGB go yellow, incase it was actually green tostart with, so we se some state acknowledgement...
    #ifdef USE_NEOPIXELS
        statusLight('y'); 
    #endif
    delay(100); // just for a tiny bit, then GREEN as acknowledgement....
    #ifdef USE_NEOPIXELS
        statusLight('g'); // NEO goes blue->red after wifi is connected.
    #endif
    
    red_off();

    #ifdef SONGS
      play_rtttl(enabledsong);
    #endif
        
    delay(50);
    //digitalWrite(GREEN_ONBOARD_LED, HIGH); // LOW = ON for this LED, so turns green led OFF while door is OPEN. TODO maybe flash fast = good? ..? 
    // it gets pulled low elsewhere after some delay.
}

// fast-flash-30-times for denied. ( thats ~3 secs ) 
void card_ok_entry_denied() { 

    #ifdef USE_NEOPIXELS
    for ( int x = 0 ; x < 20 ; x++ ) { 
        statusLight('r');  // show red for a moment, but then return to yellow after.
        delay(50);
        statusLight('y');  // the RED is much brighter, so use POV to make the yellow appear brighter. 
        delay(200);
    }
    #endif

    #ifndef USE_NEOPIXELS
    for ( int x = 0 ; x < 30 ; x++ ) { 
    //digitalWrite(GREEN_ONBOARD_LED, HIGH);
    red_off();
    delay(50);
    //digitalWrite(GREEN_ONBOARD_LED, LOW);
    red_on();
    delay(50);
    } 
    red_off();
    #endif

    #ifdef SONGS
    play_rtttl(disabledsong);
    #endif
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
        if (millis() - timeout > 5000) { // 2000 is a bit quick for the server unless it's primed by a recent call, so 3000 is better.
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



// zero padded binary print
void printBinary(byte inByte)
{
  for (int b = 7; b >= 0; b--)
  {
    Serial.print(bitRead(inByte, b));
  }
}

// zero padded binary print of lower 1/2 
void printlower4Binary(byte inByte)
{
  for (int b = 3; b >= 0; b--)
  {
    Serial.print(bitRead(inByte, b));
  }
}

// to be valid, its got to be in either the onboard eeprom cache, OR the remove server had to respond with 'access:1'
// return values:   -1 means denied entry ,  +1 means permitted by local eeprom cache, +2 means permitted by remote server
 unsigned long typed_rfid_tag = 0;

int rfid_is_valid(int tagtype) {   // tagtypes either 5 or 12  
// rfidreadbytes[] global stored the last-read RFID tag.

    typed_rfid_tag = 0;

    if (( tagtype != 5 ) && ( tagtype != 12 )) { 
      Serial.println("rfid_is_valid() says tagtype is incorrect.");
      return -1;
    }

    if ( tagtype == 5 ) { 
    // convert raw bytes into the "long" we have in eeprom: ( ignoring the checksum for now. ) 
    //typed_rfid_tag = 0;
    typed_rfid_tag += rfidreadbytes[0] << 24;
    typed_rfid_tag += rfidreadbytes[1] << 16;
    typed_rfid_tag += rfidreadbytes[2] << 8;
    typed_rfid_tag += rfidreadbytes[3];
    }

    if ( tagtype == 12 ) { 

            // ID completely read
            byte checksum = 0;
            byte value = 0;
            String id = "";

            Serial.print("AS RAW BINARY: "); 
            for (int p = 0 ; p < 12 ; p++ ) { 
              id += ( char)rfidreadbytes[p];
               Serial.print(" ");    printBinary(rfidreadbytes[p]); // Serial.print(rfidreadbytes[p],BIN);
            }
            Serial.println(); 

            Serial.print("AS FIXED BINARY: "); 
            for (int p = 1 ; p < 9 ; p++ ) {  //offsets 1-8 is the interesting ones.

               byte b = rfidreadbytes[p];
               byte righthalf = b & 15 ;
               byte lefthalf = b >> 4 ;
               if ( lefthalf == 3  ) { // 3 = 0011 
               //printlower4Binary(lefthalf); Serial.print(" ");
                  printlower4Binary(righthalf);Serial.print(" ");
               } 
               if ( lefthalf == 4 ) {   // 0100
                  printlower4Binary(rfidreadbytes[10]);Serial.print(" ");
               }
               else Serial.print("XXXX ");
               
               //Serial.print(" "); //  printlower4Binary(rfidreadbytes[p]); // Serial.print(rfidreadbytes[p],BIN);
            }
            Serial.println(); 

            // https://efxa.org/2013/05/23/simple-function-implementation-for-parsing-rfid-tags-in-arduino/
            Serial.print("AS ASCII CHARS REPRESENTING HEX : "); 
           for (int p = 1 ; p < 9 ; p++ ) {  //offsets 1-8 is the interesting ones.

            // acceptable values are: ascii 0-9 and ascii A-F   ie '0'-'9' and 'A'-'F'  
            // '0'-'9'  => hex: 0x30-0x39 / dec: 48-57 
            // 'A'-'F'  => hex: 0x41-0x46 / dec: 65-70 

                // value
               byte v = rfidreadbytes[p];

               if ( (v < 48) ||  (v > 70 ) || ( (v>57) && (v<65)  ) ) { 
                 Serial.print("SORRY INVALID ASCII-AS-HEX char.  dec:");  Serial.print(v,DEC); Serial.print(" hex:"); Serial.println(v, HEX);
               }

                // convert hex tag ID.
                if ((v >= '0') && (v <= '9'))
                  v = v - '0';
                else if ((v >= 'A') && (v <= 'F'))
                  v = 10 + v - 'A';


              Serial.print("AS NUM. dec:"); Serial.print(v,DEC); Serial.print(" hex:"); Serial.println(v, HEX);
                  

           }
           Serial.println(); 


//            Serial.print("AS BINARY3: ");
//            printlower4Binary(rfidreadbytes[1]);Serial.print(" "); 
//            printlower4Binary(rfidreadbytes[2]);Serial.print(" "); 
//            printlower4Binary(rfidreadbytes[3]);Serial.print(" "); 
//            printlower4Binary(rfidreadbytes[4]);Serial.print(" "); 
//            
//            printlower4Binary(rfidreadbytes[5]);Serial.print(" "); 
//            printlower4Binary(rfidreadbytes[6]);Serial.print(" "); 
//            printlower4Binary(rfidreadbytes[7]);Serial.print(" "); 
//            printlower4Binary(rfidreadbytes[8]);

             Serial.println(); 


            Serial.print("AS STRING: ");  Serial.println(id);

            String middle = id.substring(4,10);

            Serial.print("AS MIDDLE STRING: ");  Serial.println(middle);
            
            int mfr = strtol ( id.substring(0,4).c_str(), NULL, 16 );
            long tag  = strtol ( middle.c_str(), NULL, 16 );
            byte chk = strtol ( id.substring(10,12).c_str(), NULL, 16 );

            Serial.print("AS LONG: ");  Serial.println(tag);

            Serial.print("AS BINARY: ");  Serial.println(tag,BIN);


            // Do checksum calculation
            int i2;   
            for(int i = 0; i < 5; i++) {
              i2 = 2*i;
              //checksum ^= hex2dec(id.substring(i2,i2+2));
            }
        //#ifdef DEBUG
            Serial.println("VERIFICATION");
            Serial.print("  ID:\t");
            Serial.println(tag);
            //Serial.print("  CHK:\t");
            //Serial.println(checksum, HEX);
        //#endif    
            if (checksum == chk) {
              Serial.println("VALID tag, chksum ok.");
            }
        
    }
    

    Serial.println("looking for tag (as DEC bytes): ");
    for(int i = 0; i < eot ; i++)
    {
      Serial.print(" ");
      Serial.print(rfidreadbytes[i],DEC);
    }
    Serial.println("\nlooking for tag (as HEX bytes): ");
    for(int i = 0; i < eot ; i++)
    {
      Serial.print(" ");
      Serial.print(rfidreadbytes[i],HEX);
    }

      Serial.print("\n looking at it (as long):");
      Serial.print(typed_rfid_tag);
      Serial.println();


    // identify offset for next tag to be written to...
    next_empty_slot = find_next_empty_slot(); 

    // print the last of eeprom stored tags, all of them: 
    display_tags_in_eeprom(); 

    // quietly scan for tag now
    bool found_in_eeprom = find_requested_tag_in_eeprom(typed_rfid_tag);
    if ( found_in_eeprom ) {
      return 1; 
    }
    // TODO, at some point after returning 'true' on the above line, we should still do a http request to the server to log the event
    // but we don't do it here so we don't hold-up the user getting in the door quickly.

    // check if remote server sats it's good..? 
    if ( remote_server_says_yes(typed_rfid_tag,42,deviceName)  == 1 ) { 
        Serial.println("remote server approved tag -OK-.");
        tagcache.rfid_tag = typed_rfid_tag;
        
        if ( found_in_eeprom == false ) {  // precaution: don't write it more than once to the eeprom
          cache_rfid( next_empty_slot,tagcache ) ;    
          next_empty_slot = -1; // forget the previously known offset, we don't wnat to point ot eh wrong place.
        }
        return 2; 
    }

  return -1;
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
   if ( abs(heap - prevheap) > 500) {  // allow substantial movements without reporting.
     Serial.printf("free heap size: %u\n", heap);
     prevheap = heap;
   }

  #ifdef ENABLE_ESTOP_AS_EGRESS_BUTTON
  if ( interruptCounter  > 0 ) { 
    Serial.print("EGRESS PRESSED!"); 
    Serial.println(interruptCounter); 
    
    card_ok_entry_permitted();  //calling another functon from interrupt context can generate crashes
    previousMillis = millis(); // this triggers a door-lock event a few seconds later.
    interruptCounter = 0;
  }
  #endif

  #if HARDWARE_TYPE == HW_SONOFF_CLASSIC
  internal_green_pulse(); // show heartbeat on internal LED when we do nothing else, if we have one.
  #endif

  #ifdef USE_OLED_WEMOS
  oled_clock_update();
  #endif

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
    Serial.println("Rebooting becuase network was offline for more than 5(door) or 50(interlock) minutes.");
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
    delay(20); // just to give a tiny bit more time for the bytes to arrive before we decide the 'read' is done.
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

      #ifdef USE_NEOPIXELS
      statusLight('g'); // NEO goes just briefly green for a sec, so user knows it was READ.
      delay(200);
      statusLight('y'); // NEO goes yellow again while we think about it.....
      #endif

      int tagtype = 0;
      // "RAW" RFID TAGS emit exactly 5 bytes, ( LIKE THE ONES RESIN-POTTED BY BUZZ )
      // simple 4+1 byte tags.
      if ( eot ==5 ) {  
        Serial.println("RAW ( 5 byte ) Tag Detected");
        tagtype = 5;
        }
      
      //MANUFACTURER "POTTED", ie beige-front, black-resin back, emit 12 bytes..
      // starts with STX byte ( 0x02 ) , ends with ETX byte ( 0x03 )
      if ( ( eot >= 10 ) && (rfidreadbytes[0] == 2) && (rfidreadbytes[eot-1]== 3 ) ) {  
        Serial.println("POTTED ( ~12 byte ) Tag Detected");  
        tagtype = 12;
        }

      // a positive response means permitted
      int how_user_was_authorised =  rfid_is_valid(tagtype);
      if ( how_user_was_authorised > 0) {  // 1 or 2
        Serial.println("...Permitted entry/usage.");
        // open the door and show user other signs ass approriate.
        card_ok_entry_permitted();
        #if HARDWARE_TYPE == HW_SONOFF_CLASSIC
        internal_green_on();
        #endif
        #if HARDWARE_TYPE == HW_NOG_TH16
        internal_green_on();
        #endif        
        //external_green_on(); already turned on 

        // if the sanned tag was from specifically the eeprom cache, still report it to the server, ignoring any result (for now)
        if ( how_user_was_authorised == 1 ) {  // 1 only
          remote_server_says_yes(typed_rfid_tag,42,deviceName);  // typed_rfid_tag is a global that's set by calling rfid_is_valid(...)
          Serial.println("remote server sent tag for logging.");
        }
        
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
    while (  readerSerial.available() ) { char t = readerSerial.read(); Serial.print("flushed:"); Serial.println(t); } // flush any remaining bytes.
    eot = 0;
  }

   // turn it off some time later. 
   unsigned long currentMillis = millis();

   //TODO - can this counter go as high as 4 or more hours if needed. - needs TEST
   if ((currentMillis - previousMillis >= interval) && (  previousMillis != 0 ) )  {

        #if INTERLOCK_OR_DOOR == ISDOOR
        Serial.println("RELOCKED-DOOR");
        #endif
        #if INTERLOCK_OR_DOOR == ISINTERLOCK
        Serial.println("RELOCKED-INTERLOCK");
        #endif
        Serial.println(interval);
    
        // set the LED with the ledState of the variable:
        //digitalWrite(13, LOW); // LED
        #if HARDWARE_TYPE == HW_SONOFF_CLASSIC
        internal_green_off();
        #endif
        #if HARDWARE_TYPE == HW_NOG_TH16
        internal_green_off();
        #endif
        
        external_green_off();
        red_off();
      digitalWrite(RELAY_ONBOARD, RELAY_LOCK);

      #ifdef USE_NEOPIXELS
          statusLight('y'); // NEO goes 
      #endif
    
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
  Serial.print("statusLight:");
  Serial.println(color);
  switch (color) {
    case 'g':
      {
        NEO.setPixelColor(0, 250, 0, 0); //green
        break;
      }
    case 'r':
      {
        NEO.setPixelColor(0, 0, 250, 0); // red
        break;
      }
    case 'b':
      {
        NEO.setPixelColor(0, 0, 0, 250); // blue
        break;
      }
    case 'y':
      {
        NEO.setPixelColor(0, 20, 20, 0); // light yellow
        break;
      }
    case 'o':
      {
        NEO.setPixelColor(0, 0, 0, 0); // off
        break;
      }
  }
  NEO.show();
}
#endif


#ifdef USE_OLED_WEMOS

void oled_clock_update() { 
  // OLED DISPLAY Check if we need to update seconds, minutes, hours:
  if (lastDraw + CLOCK_SPEED < millis())
  {
    lastDraw = millis();
    // Add a second, update minutes/hours if necessary:
    updateTime();
    
    // Draw the clock:
    oled.clear(PAGE);  // Clear the buffer
    drawFace();  // Draw the face to the buffer
    drawArms(hours, minutes, seconds);  // Draw arms to the buffer
    oled.display(); // Draw the memory buffer
  }
}

void initClockVariables()
{
  // Calculate constants for clock face component positions:
  oled.setFontType(0);
  CLOCK_RADIUS = _min(MIDDLE_X, MIDDLE_Y) - 1;
  POS_12_X = MIDDLE_X - oled.getFontWidth();
  POS_12_Y = MIDDLE_Y - CLOCK_RADIUS + 2;
  POS_3_X  = MIDDLE_X + CLOCK_RADIUS - oled.getFontWidth() - 1;
  POS_3_Y  = MIDDLE_Y - oled.getFontHeight()/2;
  POS_6_X  = MIDDLE_X - oled.getFontWidth()/2;
  POS_6_Y  = MIDDLE_Y + CLOCK_RADIUS - oled.getFontHeight() - 1;
  POS_9_X  = MIDDLE_X - CLOCK_RADIUS + oled.getFontWidth() - 2;
  POS_9_Y  = MIDDLE_Y - oled.getFontHeight()/2;
  
  // Calculate clock arm lengths
  S_LENGTH = CLOCK_RADIUS - 2;
  M_LENGTH = S_LENGTH * 0.7;
  H_LENGTH = S_LENGTH * 0.5;
}

// Simple function to increment seconds and then increment minutes
// and hours if necessary.
void updateTime()
{
  seconds++;  // Increment seconds
  if (seconds >= 60)  // If seconds overflows (>=60)
  {
    seconds = 0;  // Set seconds back to 0
    minutes++;    // Increment minutes
    if (minutes >= 60)  // If minutes overflows (>=60)
    {
      minutes = 0;  // Set minutes back to 0
      hours++;      // Increment hours
      if (hours >= 12)  // If hours overflows (>=12)
      {
        hours = 0;  // Set hours back to 0
      }
    }
  }
}

// Draw the clock's three arms: seconds, minutes, hours.
void drawArms(int h, int m, int s)
{
  double midHours;  // this will be used to slightly adjust the hour hand
  static int hx, hy, mx, my, sx, sy;
  
  // Adjust time to shift display 90 degrees ccw
  // this will turn the clock the same direction as text:
  h -= 3;
  m -= 15;
  s -= 15;
  if (h <= 0)
    h += 12;
  if (m < 0)
    m += 60;
  if (s < 0)
    s += 60;
  
  // Calculate and draw new lines:
  s = map(s, 0, 60, 0, 360);  // map the 0-60, to "360 degrees"
  sx = S_LENGTH * cos(PI * ((float)s) / 180);  // woo trig!
  sy = S_LENGTH * sin(PI * ((float)s) / 180);  // woo trig!
  // draw the second hand:
  oled.line(MIDDLE_X, MIDDLE_Y, MIDDLE_X + sx, MIDDLE_Y + sy);
  
  m = map(m, 0, 60, 0, 360);  // map the 0-60, to "360 degrees"
  mx = M_LENGTH * cos(PI * ((float)m) / 180);  // woo trig!
  my = M_LENGTH * sin(PI * ((float)m) / 180);  // woo trig!
  // draw the minute hand
  oled.line(MIDDLE_X, MIDDLE_Y, MIDDLE_X + mx, MIDDLE_Y + my);
  
  midHours = minutes/12;  // midHours is used to set the hours hand to middling levels between whole hours
  h *= 5;  // Get hours and midhours to the same scale
  h += midHours;  // add hours and midhours
  h = map(h, 0, 60, 0, 360);  // map the 0-60, to "360 degrees"
  hx = H_LENGTH * cos(PI * ((float)h) / 180);  // woo trig!
  hy = H_LENGTH * sin(PI * ((float)h) / 180);  // woo trig!
  // draw the hour hand:
  oled.line(MIDDLE_X, MIDDLE_Y, MIDDLE_X + hx, MIDDLE_Y + hy);
}

// Draw an analog clock face
void drawFace()
{
  // Draw the clock border
  oled.circle(MIDDLE_X, MIDDLE_Y, CLOCK_RADIUS);
  
  // Draw the clock numbers
  oled.setFontType(0); // set font type 0, please see declaration in SFE_MicroOLED.cpp
  oled.setCursor(POS_12_X, POS_12_Y); // points cursor to x=27 y=0
  oled.print(12);
  oled.setCursor(POS_6_X, POS_6_Y);
  oled.print(6);
  oled.setCursor(POS_9_X, POS_9_Y);
  oled.print(9);
  oled.setCursor(POS_3_X, POS_3_Y);
  oled.print(3);
}

#endif


