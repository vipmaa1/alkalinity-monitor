#include <WiFi.h>
#include <Wire.h>
#include <WebServer.h>
#include "time.h"
#include <Arduino.h>
#include <ESP32_MailClient.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include <WiFiManager.h>
#include <FS.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include <NTPClient.h>

WiFiManager wifiMGR; //WiFiManager object , Local intialization. Once its business is done, there is no need to keep it around

#define JSON_CONFIG_FILE "/test_config.json"
bool shouldSaveConfig = false ;
char emailString[50] = "KH.guardian.11@gmail.com" ;

void saveConfigFile()
{
  // Save data to JSON format
  Serial.println(F("Saving configuration....")) ;
  // Create a JSON document
  StaticJsonDocument<512> json;
  json["emailString"] = emailString ;
  // Open config file
  File configFile = SPIFFS.open(JSON_CONFIG_FILE, "w") ; // Open to write on file
  if (!configFile)
  {
    // Error, File didn't open
    Serial.println("Failed to open config file for writing") ;
  }
  // Serialize JSON data to write to file
  serializeJsonPretty(json, Serial);
  if (serializeJson(json, configFile) == 0)
  {
    // Error writing to file
    Serial.println("Failed to write to file") ;
  }
  // Close file
  configFile.close() ;
  }

bool loadConfigFile ()
// Loading existing configuration file 
{
  // Uncomment if we need to format filesystem
  // SPIFFS.format() ;

  // Read configuration from FS json
  Serial.println("Mounting system file") ;

  // May need to make it begin(true) first time you are using SPIFFS
  if (SPIFFS.begin(false) || SPIFFS.begin(true))
  {
    Serial.println("System file mounted") ; 
    if (SPIFFS.exists(JSON_CONFIG_FILE))
    {
      // The file exists, reading and loading
      Serial.println("Reading config file") ;
      File configFile = SPIFFS.open(JSON_CONFIG_FILE, "r") ;
      if (configFile)
      {
        Serial.println("Opened configuration file") ;
        StaticJsonDocument<512> json ;
        DeserializationError error = deserializeJson(json, configFile) ;
        serializeJsonPretty(json, Serial);
        if (!error)
        {
          Serial.println("Parsing JSON") ;
          strcpy(emailString, json["emailString"]) ;

          return true ; 
        }
        else 
        {
          // Error loading JSON data 
          Serial.println("Failed to laod JSON config") ;
        }
      }
    }
  }
  else 
  {
    // Error mounting file system
    Serial.println("Failed to mount System file") ;
  }

  return false ;
} 

void saveConfigCallback()
{
  // Callback notifying us of the need to save configuration
  Serial.println("Should save config") ;
  shouldSaveConfig = true ;
}

void configModeCallback(WiFiManager *myWiFiManager)
{
  // Called when config mode launched
  Serial.println("Entered configuration mode") ;
  Serial.print("SSID: ");
  Serial.println(myWiFiManager->getConfigPortalSSID()) ;
  Serial.print("Config IP Address: ") ;
  Serial.println(WiFi.softAPIP()) ;
}

#define LED_Connected 32
#define LED_BUILTIN 2
#define WIRE_SLAVE_ADDRESS 9

/* ======================================== Variables for network */

const char* MyHostName = "KH-Guardian-Station"; // The name of your ESP device that will appeare in the connected devices to your home WiFi

unsigned long last_update_time = 0;
unsigned long current_time;
unsigned long startup_time;

// unsigned long time_update_interval = 12*3600*1000; //2 times a day
unsigned long time_update_interval = 12*3600*1000; //24 times as day
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 7200;
const int   daylightOffset_sec = 3600;
String alkMonitorState = "off";

unsigned long currentTime = millis();
unsigned long previousTime = 0; 
const long timeoutTime = 2000;
String header;
String display_volume = "";

WiFiServer server(80);
// IPAddress local_IP(192, 168, 1, 111);
// IPAddress gateway(192, 168, 1, 254);
// IPAddress subnet(255, 255, 255, 0);
// IPAddress primaryDNS(8, 8, 8, 8);   //optional
// IPAddress secondaryDNS(8, 8, 4, 4); //optional

/* ========================================  Vairables for email client */
#define GMAIL_SMTP_SEVER "smtp.gmail.com"
#define GMAIL_SMTP_USERNAME "KH.guardian.11@gmail.com"
#define GMAIL_SMTP_PASSWORD "psihrnchkjkxqyia"
#define GMAIL_SMTP_PORT 465  

SMTPData data;

String sendEmail(char *subject, char *sender, char *body, char *recipient, boolean htmlFormat) {
  data.setLogin(GMAIL_SMTP_SEVER, GMAIL_SMTP_PORT, GMAIL_SMTP_USERNAME, GMAIL_SMTP_PASSWORD);
  data.setSender(sender, GMAIL_SMTP_USERNAME);
  data.setSubject(subject);
  data.setMessage(body, htmlFormat);
  data.addRecipient(emailString);
  data.setSendCallback(sendingStatus);
  // data.setDebug(true);
  if (!MailClient.sendMail(data)) 
    return MailClient.smtpErrorReason();
  
  return "";
  data.empty();
}
// Callback function to get the Email sending status
void sendingStatus(SendStatus msg) {
  Serial.println(msg.info()); //--> Print the current status
}

void Wifi_Status() {
    WiFi.setAutoReconnect(true);
    WiFi.persistent(true); 
    Serial.print("Device name: (< ");
    Serial.print(WiFi.getHostname());  
    Serial.println(" >)");
    Serial.print("Successfully connected to Wifi: (< ");
    Serial.print(WiFi.SSID());
    Serial.println(" >)");
  }


void setup()
{
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(LED_Connected, OUTPUT);

  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

  //initializing serial communication from Mega to ESP and vise versa
  Serial.begin(115200); 
  delay(10);
  // Serial2.begin(9600, SERIAL_8N1, 3, 1);
  Serial2.begin(9600, SERIAL_8N1, 16, 17);
  Wire.begin(SDA,SCL);
  while (!Serial2) delay(10);

  // Change to (true) when testing to foce configuration everytime we run
  bool forceConfig = false ;
  bool spiffsSetup = loadConfigFile() ;
  if (!spiffsSetup)
  {
    Serial.println(F("Forcing config mode as there is no saved config")) ;
    forceConfig = true ;
  }

  // Explicity set WiFi mode 
  WiFi.mode(WIFI_STA);

  wifiMGR.setDebugOutput(true);

  // wifiMGR.resetSettings(); // reset settings - wipe stored credentials for testing. comment this and upload your code

  // Set config save notify callback
  wifiMGR.setSaveConfigCallback(saveConfigCallback) ;

  // Sets callback that gets called when connecting to previoua wifi fails, and enters access point mode
  wifiMGR.setAPCallback(configModeCallback) ;

  // Custom elements
  // Custom text box to hold the email address 50 charachters max
  WiFiManagerParameter email_text_box("email_text_box", "Email address to receive results", emailString, 50); 
  wifiMGR.addParameter(&email_text_box); // add defined parameter 

  if (forceConfig)
  {
    // Run if we need a configuration
    if (!wifiMGR.startConfigPortal("KH-Guardian-Wifi-Setup"))
    {
      digitalWrite(LED_BUILTIN, LOW);
      digitalWrite(LED_Connected, LOW);
      Serial.println("Timeout, Failed to connect") ;
      delay(3000);
      ESP.restart();
      delay(5000);
    }
  }
  else 
  {
    if (!wifiMGR.autoConnect("KH-Guardian-Wifi-Setup"))
    {
      digitalWrite(LED_BUILTIN, LOW);
      digitalWrite(LED_Connected, LOW);
      Serial.println("Timeout, Failed to connect") ;
      delay(3000);
      ESP.restart();
      delay(5000);
    }
  }

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");              
    digitalWrite(LED_BUILTIN, LOW);
    digitalWrite(LED_Connected, LOW);
    if (WiFi.status() == WL_CONNECT_FAILED) {
    ESP.restart();
    }
  }

  // We are now connected to the wifi netwrok using station mode "normal wifi network"

  WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, INADDR_NONE);
  WiFi.setHostname(MyHostName);
  Wifi_Status(); // Call wifi status function

  // Dealing with the user's configuration values 
  // Copy the string value
  strncpy(emailString, email_text_box.getValue(), sizeof(emailString)) ;
  Serial.print("Email address: ") ;
  Serial.println(emailString) ;
  
  // Save the custom parameters to FS
  if (shouldSaveConfig)
  {
    saveConfigFile();
  }

  // // Dealing with the user's configuration values 
  // // Copy the string value
  // strncpy(emailString, email_text_box.getValue(), sizeof(emailString)) ;
  // Serial.print("Email address: ") ;
  // Serial.println(emailString) ;

  delay(1000);
  // Server initialization to send email notificaitons
  server.begin();
  String result = sendEmail("KH Guardian Notification", "KH Guardian", "ESP MCU Started ...." , emailString, false);

  delay(100);
  startup_time = millis();

  // End Setup
} 

bool first_start = true;

void loop()
{  
  if (WiFi.status() != WL_CONNECTED) {
      digitalWrite(LED_BUILTIN, LOW);
      digitalWrite(LED_Connected, LOW);
      WiFi.disconnect();
      WiFi.reconnect();  
    } else {
      digitalWrite(LED_BUILTIN, HIGH);
      digitalWrite(LED_Connected, HIGH);
  }    

  current_time = millis();
  // if (first_start == true || (last_update_time == 0 && (current_time - startup_time > 15*1000))) //on startup - only send NTP time update after about 15 seconds delay
  if ((first_start == true && (current_time - last_update_time > time_update_interval)) || (last_update_time == 0 && (current_time - startup_time > 1*1000))) //on startup - only send NTP time update after about 15 seconds delay
  {
    first_start = false;

    String ntp_time = getNTPTime();

    if(last_update_time==0 && ntp_time=="")
    {
      delay(1000);
      ntp_time = getNTPTime();
    }

    if(ntp_time!="")
    {
      wire_sendString("TIME:" + ntp_time + " ");
    }

    last_update_time = current_time;
  }
  
  String message = Serial2.readString();

  // if (!Serial2.available() >0 && Serial2.available() ==0){ 
  //     // do nothing
  //  } else {
  //   // read the incoming bytes: this is used to troubleshoot problems if any
  //     Serial.print("Message received: ");
  //     Serial.println(Serial2.readString());
  // }
  
  if(message !="")
  {
    if(message.indexOf("KH-MON:")!=-1)
    {
        int str_len = message.length() + 1; 
        char char_array[str_len];
        message.toCharArray(char_array, str_len);
        
        if (WiFi.status() != WL_CONNECTED)
        {
          Serial.println("WIFI connection lost - attempting to reconnect ... ");    
          WiFi.disconnect();
          WiFi.reconnect();
          if (WiFi.status() != WL_CONNECTED){
             ESP.restart();
          }
        }
        
        String result = sendEmail("KH Guardian Notification", "KH Guardian", char_array, emailString, false);
        Serial.println(result);

        // if(result!="") {
        //   Serial.println(result);
        //   ESP.restart();
        // }
    }
  }
    
  delay(100);
  // End loop
} 

void wire_sendString(String message) {
  Wire.beginTransmission(WIRE_SLAVE_ADDRESS); // transmit to device #9
  Wire.write(message.c_str());              // sends x 
  Wire.endTransmission();    // stop transmitting
} 

String getNTPTime() {
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)){
    return "";
  }

  char timeStringBuff[50]; //50 chars should be enough
  strftime(timeStringBuff, sizeof(timeStringBuff), "%H:%M:%S", &timeinfo);
  String asString(timeStringBuff);

  Serial.print("Time string: ");
  Serial.println(asString);
  return asString;
}  
