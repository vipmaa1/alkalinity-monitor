bool Compensate_KH;
bool DEBUG_MODE = false;
const int DELAY_STANDARD = 500;
const int DELAY_SMALL = 20;
const float ACID_CONCENTRATION = 0.025;

//DISPLAY FONT SIZE 2: 10 SYMBOLS IN A ROW, 2 ROWS
//DISPLAY FONT SIZE 1: 21 SYMBOLS IN A ROW, 4 ROWS

#include <Wire.h>
#include "MemoryInfo.h"
#include <DS3231.h>
#include <TMCStepper.h>
#include <OneWire.h>
#include <Wire.h>
// #include <Adafruit_SSD1306.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH1106.h>
#include <EEPROM.h>
#include "Adafruit_seesaw.h"
#include <seesaw_neopixel.h>

#define PH_ENDPOINT 4.4
#define MAX_ACID_VOLUME_PER_TEST 30.0
#define LED_timeReceived 31

#define LAST_DKH_TEST_EEPROM_POSITION 256

#define DOSER_ID_1 1
#define DOSER_ID_2 2
#define DOSER_ID_3 3
#define DOSER_ID_4 4

#define PIN_PH A1
#define PIN_RELAY 8
#define PIN_VINT 30

#define PIN_ENABLE_M1 15  // Enable - Reagemt Pump
#define PIN_DIR_M1 11     // Direction
#define PIN_STEP_M1 13    // Step

#define PIN_ENABLE_M2 5  // Enable - Sample water Pump
#define PIN_DIR_M2 7     // Direction
#define PIN_STEP_M2 3    // Step

#define PIN_ENABLE_M3 37  // Enable Waste water Pump
#define PIN_DIR_M3 33     // Direction
#define PIN_STEP_M3 35    // Step

#define PIN_ENABLE_M4          43 // Enable Dossing Pump
#define PIN_DIR_M4             39 // Direction
#define PIN_STEP_M4            41 // Step

#define PUMP_1_FWD_DIR true
#define PUMP_2_FWD_DIR true
#define PUMP_3_FWD_DIR false
#define PUMP_4_FWD_DIR true

#define SW_RX 63             // TMC2208/TMC2224 SoftwareSerial receive pin
#define SW_TX 40             // TMC2208/TMC2224 SoftwareSerial transmit pinf
#define DRIVER_ADDRESS 0b00  // TMC2209 Driver address according to MS1 and MS2
#define R_SENSE 0.11f        // Match to your driver, SilentStepStick series use 0.11
#define SCREEN_ROWS 8
#define maxItemLength 18

#define WIRE_SLAVE_ADDRESS 9
#define SS_SWITCH 24
#define SS_NEOPIX 6
#define SEESAW_ADDR 0x36

#define STD_STIR_TIME         20 
#define STD_STIR_TIME_PH6     25 
#define STD_STIR_TIME_PH5     30 
#define STD_STIR_TIME_PH4     40 

Adafruit_seesaw ss;

seesaw_NeoPixel sspixel = seesaw_NeoPixel(1, SS_NEOPIX, NEO_GRB + NEO_KHZ800);
int32_t encoder_position;

#define OLED_RESET -1
Adafruit_SH1106 lcd(OLED_RESET); /* Object of class Adafruit_SSD1306 */
// Adafruit_SSD1306 lcd(128, 64);

struct DateStruct {
  int y = 22;
  int m = 8;
  int d = 30;
};

struct TimeStruct 
{
  int h = 22;
  int m = 8;
  int s = 30;
};

struct DateTimeDKHStruct {
  byte t_month = 1;
  byte t_day = 1;
  byte t_hour = 0;
  byte t_minute = 0;
  float dkh = 0.00;
};

#define MENU_ITEMS_COUNT 15
char menu[][maxItemLength] = { "KH Settings", "Start Test", "Calibrate PH", "Calibrate Pumps", "Prime Acid Line", "Prime Sample Line", "Prime KH Line", "Prime Waste Line", "Refill Re Agent", "Pumps Speed", "Daily Tests", "Set Date", "Set Time", "Debug Mode", "Exit"};

#define SUB_MENU1_ITEMS_COUNT 7
char sub_menu1[][maxItemLength] = { "  ", "Show settings", "Re Agent", "Sample Water", "Waste Water", "Dosser", "Exit", "   " };

#define SUB_MENU2_ITEMS_COUNT 5
char sub_menu2[][maxItemLength] = { "  ", "Show settings", "PH 4", "PH 7", "Exit", "   ", "   ", "   " };

#define SUB_MENU3_ITEMS_COUNT 5
char sub_menu3[][maxItemLength] = { "  ", "Water Volume", "+ 0.1 DKH / 100L", "Needed KH", "Exit", "   ", "   ", "   " };

// #define SUB_MENU_Priming_ITEMS_COUNT 5
// char sub_menu4[][maxItemLength] = { "  ", "ReAgent", "Sample Water", "KH Doser", "Exit", "   ", "   ", "   " };

float CALIBRATION_VOLUME_ML = 25.0;

TMC2209Stepper driver(SW_RX, SW_TX, R_SENSE, DRIVER_ADDRESS);
DS3231 rtc(SDA, SCL);
Time t;

int display_state;
int display_state_previous;

float d1_calibration_turns = 126.0;
float d2_calibration_turns = 138.0;
float d3_calibration_turns = 130.0;
float d4_calibration_turns = 130.0;

byte schedule_tests_per_day = 3;
byte pump_speed = 2;

float water_Volume = 450; // Total water volume or Total System volume
float Add_KH_Per_100ML = 1; // Number of MLs needed from your KH solution to increase system KH by 0.1 DKH for each 100L of tank total water volume
float needed_KH = 8; // Desired KH to keep

DateStruct refill_date;
DateStruct new_date;
TimeStruct new_time;
DateTimeDKHStruct last_dkh_test;

float reagent_volume = 500;
float used_reagent_volume;

float voltage_4PH = 0.96;
float voltage_7PH = 2.17;
float initial_ph;
float test_ph;

#define BUFFER_SIZE 512

volatile char receive_buffer[BUFFER_SIZE];
volatile boolean receive_flag = false;
unsigned long startup_time_millis;
int last_hour_checked = -1;
int last_day_reagent_checked = -1;
bool toggle_activity;

String msg; 

void setup() 
{
  // Setup Serial Monitor
  rtc.begin();

  Serial.begin(9600);
  loadSettings(DEBUG_MODE);
  //saveLastDKH();
  loadLastDKH();

  Wire.begin(WIRE_SLAVE_ADDRESS);
  Wire.onReceive(receiveEvent);
  resetBuffer();

  //This is used to reset pumps settings and to be re-commented after uploading the code then upload it once more
  // d1_calibration_turns = 150;
  // d2_calibration_turns = 150;
  // d3_calibration_turns = 150;
  // d4_calibration_turns = 150;
  // saveSettings();

  while (!Serial) delay(10);
  ss.begin(SEESAW_ADDR);
  sspixel.begin(SEESAW_ADDR);

  sspixel.setBrightness(20);
  sspixel.show();
  ss.pinMode(SS_SWITCH, INPUT_PULLUP);
  encoder_position = ss.getEncoderPosition();
  ss.setGPIOInterrupts((uint32_t)1 << SS_SWITCH, 1);
  ss.enableEncoderInterrupt();

  // initialize with the I2C addr 0x7A or 0x3C or 0x3D
  lcd.begin(SH1106_SWITCHCAPVCC, 0x3C); /* Initialize display with address 0x3C */
  // lcd.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  lcd.clearDisplay();
  lcd.setCursor(0, 0);
  lcd.setTextSize(2);
  lcd.setTextColor(WHITE);

  pinMode(LED_timeReceived, OUTPUT);
  pinMode(PIN_ENABLE_M1, OUTPUT);
  pinMode(PIN_STEP_M1, OUTPUT);
  pinMode(PIN_DIR_M1, OUTPUT);
  digitalWrite(PIN_ENABLE_M1, LOW);  // Enable driver in hardware

  pinMode(PIN_ENABLE_M2, OUTPUT);
  pinMode(PIN_STEP_M2, OUTPUT);
  pinMode(PIN_DIR_M2, OUTPUT);
  digitalWrite(PIN_ENABLE_M2, LOW);  // Enable driver in hardware

  pinMode(PIN_ENABLE_M3, OUTPUT);
  pinMode(PIN_STEP_M3, OUTPUT);
  pinMode(PIN_DIR_M3, OUTPUT);
  digitalWrite(PIN_ENABLE_M3, LOW);  // Enable driver in hardware

  pinMode(PIN_ENABLE_M4, OUTPUT);
  pinMode(PIN_STEP_M4, OUTPUT);
  pinMode(PIN_DIR_M4, OUTPUT);
  digitalWrite(PIN_ENABLE_M4, LOW);  // Enable driver in hardware

  pinMode(PIN_RELAY, OUTPUT);
  pinMode(PIN_VINT, OUTPUT);

  driver.begin();            //  SPI: Init CS pins and possible SW SPI pins
                             // UART: Init SW UART (if selected) with default 115200 baudrate
  driver.toff(5);            // Enables driver in software
  driver.rms_current(1200);  // Set motor RMS current
  driver.microsteps(16);     // Set microsteps to 1/16th

  driver.pwm_autoscale(true);  // Needed for stealthChop

  // // to be used to manually update close and then to be recommented back again then upload the code
  // rtc.setDOW(SUNDAY);     // Set Day-of-Week to SUNDAY
  // rtc.setTime(18, 36, 30);     // Set the time to 12:00:00 (24hr format)
  // rtc.setDate(27, 8, 2023);   // Set the date to January 1st, 2014

  Serial.println("");
  Serial.println("KH-MON: KH Guardian - Mega MCU started ....");

  startup_time_millis = millis();

}  // END Setup

int h;
int m;
int s;
int d;
unsigned long current_time_millis;
float implied_calibr_turns;
float dosed_volume;
int subItemSelected;

byte doser_id = 0;
float selected_calibr_turns;

float voltage;
float ph;

String msg_1;
String msg_2;
String msg_3;
String msg_4;

char float_bufer[10];
bool first_start = true;
int last_hour_time_sync = -1;

void loop() 
{
  t = rtc.getTime();
  h = (int)t.hour;
  m = (int)t.min;
  s = (int)t.sec;
  d = t.date;

  current_time_millis = millis();

  if (first_start == true || (m == 0 && (h == 6 || h == 12 || h == 18 || h == 24) && last_hour_time_sync != h)) {  
  // if ((first_start == true && (current_time_millis - startup_time_millis > 30000)) || (m == 0 && (h == 6 || h == 18) && last_hour_time_sync != h)) {  

    first_start = false;
    last_hour_time_sync = h;
    resetBuffer();  //just in case

    Serial.print(">>>-Requesting TIME ..");
    Serial.println("");
    int cnt = 0; 
    // while (cnt < 120) {
    while (cnt < 40) {
      if (receive_flag) {
        if (receive_buffer[0] == 'T' && receive_buffer[1] == 'I' && receive_buffer[2] == 'M' && receive_buffer[3] == 'E' && receive_buffer[4] == ':') {
          int h_rec;
          int m_rec;
          int s_rec;

          h_rec = (receive_buffer[5] - '0') * 10; h_rec = h_rec + (receive_buffer[6] - '0');
          m_rec = (receive_buffer[8] - '0') * 10; m_rec = m_rec + (receive_buffer[9] - '0');
          s_rec = (receive_buffer[11] - '0') * 10; s_rec = s_rec + (receive_buffer[12] - '0');

          long time_diff = (h_rec - h) * 3600 + (m_rec - m) * 60 + (s_rec - s);
          
          if (time_diff < 0) { time_diff = -time_diff; }

          if (h_rec <= 23 && m_rec <= 59 && s_rec <= 60 && h_rec >= 0 && m_rec >= 0 && s_rec >= 0) 
          {
            rtc.setTime(h_rec, m_rec, s_rec);
            digitalWrite(LED_timeReceived, HIGH);
            msg = "KH-MON: Time updated successfully \n> (" + String(time_diff) + ") Seconds RTC deviation";            
            Serial.println(msg);
          } 
          else 
          {
            //do nothing
            digitalWrite(LED_timeReceived, LOW);
          }
          resetBuffer();
          cnt = 120;
        }
      }
      lcd.setTextSize(2);
      lcd.clearDisplay();
      lcd.setCursor(0, 0);
      msg = "\nTime Check \n " + String(cnt);

      lcd.println(msg);
      lcd.display();
      cnt++;
      delay(500);
    }
  }

  toggle_activity = !toggle_activity;

  if (last_day_reagent_checked != d && h == 10) {
    last_day_reagent_checked = d;
    if (used_reagent_volume > 0.75 * reagent_volume) {
      Serial.println("KH-MON: LOW REAGENT WARNING!, current usage is " + String(used_reagent_volume / reagent_volume * 100, 0) + "%");
    }
  }

  current_time_millis = millis();

  if (last_hour_checked != h )  //check if alk monitor needs to be run
  {
    if (m == 20) {
      last_hour_checked = h;
      if (
        ((h == 6) && schedule_tests_per_day == 1)
        || ((h == 6 || h == 12) && schedule_tests_per_day == 2)
        || ((h == 6 || h == 12 || h == 18) && schedule_tests_per_day == 3)
        || ((h == 6 || h == 10 || h == 14 || h == 18) && schedule_tests_per_day == 4)
        || ((h == 6 || h == 9 || h == 12 || h == 15 || h == 18) && schedule_tests_per_day == 5)
        || ((h == 6 || h == 9 || h == 12 || h == 15 || h == 18 || h == 21) && schedule_tests_per_day == 6)
        || ((h == 6 || h == 9 || h == 12 || h == 15 || h == 18 || h == 21 || h == 24) && schedule_tests_per_day == 7)
        || ((h == 3 || h == 6 || h == 9 || h == 12 || h == 15 || h == 18 || h == 21 || h == 24) && schedule_tests_per_day == 8)
        || ((h == 1 || h == 3 || h == 6 || h == 9 || h == 12 || h == 15 || h == 18 || h == 21 || h == 24) && schedule_tests_per_day == 9)
        || ((h == 1 || h == 3 || h == 5 || h == 8 || h == 10 || h == 13 || h == 16 || h == 18 || h == 21 || h == 24) && schedule_tests_per_day == 10)) {
        runAlkalinityTest();
      }
    }
  }

  lcd.setTextSize(2);
  lcd.setCursor(0,0);
  lcd.clearDisplay();

  if(s>=0 && s<10 || s>=30 && s<40)
  {
    display_state = 1;
  }
  else
  if(s>=10 && s<20 || s>=40 && s<50)  
  {
    display_state = 2;
    ph = getPH(50); 
    // ph = getPH(250); 

  }
  else
  {
    display_state = 3;
  }

  if(display_state != display_state_previous)
  {
    lcd.setTextSize(2);
    lcd.setCursor(0,0);
    lcd.clearDisplay();
  }
  display_state_previous = display_state;
  lcd.setTextSize(2);
  lcd.setCursor(0,0);
  lcd.clearDisplay();

  switch(display_state)
  {
    case 1:
      lcd.print("\n");
      lcd.print("\n");
      if(h<10){lcd.print("0");}
      lcd.print(h);lcd.print(":");
      if(m<10)
      {
        lcd.print("0");
      }
      lcd.print(m);lcd.print(":");
      if(s<10)
      {
        lcd.print("0");
      }
      lcd.print(s);
      break;
    case 2:
      lcd.print("\n");
      lcd.print("\n");
      if(toggle_activity)
      {
        msg = "PH: ";
      } 
      else 
      {
        msg = "ph: ";
      }
      msg = msg + String(ph,2);
      lcd.print(msg);
      break;
    case 3:
      lcd.print("\n");
      lcd.print("LAST:");
      if(last_dkh_test.t_hour<10)
      {
        lcd.print("0");
      }
      lcd.print(last_dkh_test.t_hour);lcd.print(":");
      if(last_dkh_test.t_minute<10) 
      {
        lcd.print("0");
      } 
      lcd.print(last_dkh_test.t_minute);
      
      if(toggle_activity)
      {
        msg = "DKH: ";
      } 
      else 
      {
        msg = "dkh: ";
      }
      msg = msg+ String(last_dkh_test.dkh,2); 
      lcd.print(msg);
      break;
    default:
      lcd.print("\n");
      lcd.print("\n");
      break;
  }
  lcd.print("\n");
  lcd.print("\n");
  lcd.display();

  // Enter the settings menu if select Switch is pressed
  if (ss.digitalRead(SS_SWITCH) == 0) {
    while (ss.digitalRead(SS_SWITCH) == 0)
      ;  //wait till switch is released.
    int itemSelected = displayMenu(menu, MENU_ITEMS_COUNT); //{ "KH Settings", "Start Test", "Calibrate PH", "Calibrate Pumps", "Prime Acid Line", "Prime Sample Line", "Prime KH Line", "Prime Waste Line", "Refill Re Agent", "Pumps Speed", "Daily Tests", "Set Date", "Set Time", "Debug Mode", "Exit"};
    switch (itemSelected)  
    {
      case 0:  //KH Settings
        subItemSelected = displayMenu(sub_menu3, SUB_MENU3_ITEMS_COUNT);  //{"  ", "Water Volume", "0.1 DKH/100L ", "Needed KH", "Exit", "   ", "   ", "   "};
        switch (subItemSelected) {
          case 0:
            break;
          case 1:
            water_Volume = chooseWaterVolume(water_Volume,10);
            saveSettings();
            loadSettings(DEBUG_MODE);
            break;
          case 2:
            Add_KH_Per_100ML = chooseKHPer100ML(Add_KH_Per_100ML,0.1);
            saveSettings();
            loadSettings(DEBUG_MODE);           
            break;
          case 3:
            needed_KH = chooseNeededKH(needed_KH,0.1);
            saveSettings();
            loadSettings(DEBUG_MODE);               
            break;
          case 4:
            break;
        }
        saveSettings();
        loadSettings(DEBUG_MODE);
        break;
      case 1:  //Start Test
        runAlkalinityTest();
        break;
      case 2:  //Calibrate PH
        subItemSelected = displayMenu(sub_menu2, SUB_MENU2_ITEMS_COUNT);  //{"Show settings","PH 4","PH 7","Exit","   ","   ","   ","   "};
        switch (subItemSelected) {
          case 0:
            // Do Nothing
            break;
          case 1:
            showPHSettings();
            break;
          case 2:
            voltage_4PH = calibrate_PH(true);
            break;
          case 3:
            voltage_7PH = calibrate_PH(false);
            break;
          case 4:
            break;
        }
        saveSettings();
        loadSettings(DEBUG_MODE);
        break;
      case 3:    //Calibrate pumps
        subItemSelected = displayMenu(sub_menu1, SUB_MENU1_ITEMS_COUNT);  //{"Show settings","Reagent","Sample Water","Waste Water","KH Doser","Exit","   "};
        switch (subItemSelected) {
          case 0:
            // Do Nothing
            break;
          case 1:
            showPumpSettings();
            break;
          case 2:
            digitalWrite(PIN_VINT, HIGH);
            msg_1 = "\nCalibrating";
            msg_2 = "    ";
            msg_3 = "Reagent pump";
            msg_4 = "Please wait ...";
            delayDisplayCountdown(3);
            lcd.display();
            doser_id = DOSER_ID_1;
            selected_calibr_turns = d1_calibration_turns;
            break;
          case 3:
            digitalWrite(PIN_VINT, HIGH);
            msg_1 = "\nCalibrating";
            msg_2 = "    ";
            msg_3 = "Sample pump";
            msg_4 = "Please wait ...";            
            delayDisplayCountdown(3);
            lcd.display();
            doser_id = DOSER_ID_2;
            selected_calibr_turns = d2_calibration_turns;
            break;
          case 4:
            digitalWrite(PIN_VINT, HIGH);
            msg_1 = "\nCalibrating";
            msg_2 = "    ";
            msg_3 = "Waste pump";
            msg_4 = "Please wait ..."; 
            delayDisplayCountdown(3);
            lcd.display();
            doser_id = DOSER_ID_3;
            selected_calibr_turns = d3_calibration_turns;
            break;
          case 5:
            digitalWrite(PIN_VINT, HIGH);
            msg_1 = "\nCalibrating";
            msg_2 = "    ";
            msg_3 = "Dosing pump";
            msg_4 = "Please wait ..."; 
            delayDisplayCountdown(3);
            lcd.display();
            doser_id = DOSER_ID_4;
            selected_calibr_turns = d4_calibration_turns;
            break;
          default:
            break;
        }
        if (doser_id != 0) {
          dose(doser_id, CALIBRATION_VOLUME_ML, false);
          dosed_volume = chooseVolume(CALIBRATION_VOLUME_ML, 0.1);
          implied_calibr_turns = selected_calibr_turns * CALIBRATION_VOLUME_ML / dosed_volume;
          switch (doser_id) {
            case DOSER_ID_1:
              d1_calibration_turns = implied_calibr_turns;
              digitalWrite(PIN_VINT, LOW);
              break;
            case DOSER_ID_2:
              d2_calibration_turns = implied_calibr_turns;
              digitalWrite(PIN_VINT, LOW);
              break;
            case DOSER_ID_3:
              d3_calibration_turns = implied_calibr_turns;
              digitalWrite(PIN_VINT, LOW);
              break;
            case DOSER_ID_4:
              d4_calibration_turns = implied_calibr_turns;
              digitalWrite(PIN_VINT, LOW);
              doser_id = 0;
              break;
            default:
              break;
          }
          doser_id = 0;
          saveSettings();
          loadSettings(DEBUG_MODE);
        }
        break;
      case 4:  //Prime Acid line
            digitalWrite(PIN_VINT, HIGH);
            initial_ph = getPH(100);
            // initial_ph = getPH(250);
            lcd.print("\n");
            msg_1 = "\nBase PH: "+String(initial_ph,2);
            msg_2 = "";
            msg_3 = "";
            msg_4 = "";
            delayDisplayCountdown(5);

            addAcid(3, false);
            used_reagent_volume = used_reagent_volume + 3; 
            saveSettings();
            stir(STD_STIR_TIME);

            test_ph = getPH(100);
            // test_ph = getPH(250);

            lcd.print("\n");
            msg_1 = "\nTest PH: "+String(test_ph,2);
            msg_2 = "";
            msg_3 = "";
            msg_4 = "";
            delayDisplayCountdown(5);
            digitalWrite(PIN_VINT, LOW);
            break;
      case 5: // Prime Sample Water Pump
            digitalWrite(PIN_VINT, HIGH);
            msg_1 = "\nPriming";
            msg_2 = "    ";
            msg_3 = "Sample Water";
            msg_4 = "Please wait ...";
            delayDisplayCountdown(3);
            addWater(6);
            lcd.display();
            stir(STD_STIR_TIME);
            digitalWrite(PIN_VINT, LOW);        
            break;
      case 6: // Prime KH Dosing Pump
            digitalWrite(PIN_VINT, HIGH);
            msg_1 = "\nPriming";
            msg_2 = "    ";
            msg_3 = "KH Doser";
            msg_4 = "Please wait ...";
            delayDisplayCountdown(3);
            addKH(6);
            lcd.display();
            stir(STD_STIR_TIME);
            digitalWrite(PIN_VINT, LOW);     
            break;   
      case 7: // Prime Waste Pump
            digitalWrite(PIN_VINT, HIGH);
            msg_1 = "\nPriming";
            msg_2 = "    ";
            msg_3 = "Waste Pump";
            msg_4 = "Please wait ...";
            delayDisplayCountdown(3);
            removeWater(6);
            lcd.display();
            stir(STD_STIR_TIME);
            digitalWrite(PIN_VINT, LOW);     
            break;              
      case 8:  //Refill Acid continer
            reagent_volume = chooseVolume(round((reagent_volume - used_reagent_volume) / 50.0) * 50, 50);
            refill_date.d = t.date;
            refill_date.m = t.mon;
            refill_date.y = (byte)(t.year - 2000);
            used_reagent_volume = 0;
            saveSettings();
            loadSettings(DEBUG_MODE);
            break;
      case 9:  //Set Pump Speed
            pump_speed = chooseSpeed(pump_speed);
            saveSettings();
            loadSettings(DEBUG_MODE);
            break;
      case 10:  //Set frequency - Daily tests
            schedule_tests_per_day = chooseFrequency(schedule_tests_per_day, 1, 10);
            saveSettings();
            break;
      case 11: //Set Date
            new_date.y = chooseInteger(t.year, 2023, 2040);
            new_date.m = chooseInteger(t.mon, 1, 12);
            new_date.d = chooseInteger(t.date, 1, 31);
            rtc.setDate(new_date.d, new_date.m, new_date.y);
            break;      
      case 12: //Set Time
            new_time.h = chooseInteger(t.hour, 0, 23);
            new_time.m = chooseInteger(t.min, 0, 59);
            new_time.s = chooseInteger(t.sec, 0, 60);
            rtc.setTime(new_time.h, new_time.m, new_time.s);
            break;      
      case 13:  //Debug Mode
            DEBUG_MODE = chooseTrueFalse(DEBUG_MODE);
            break;
      default:
            doser_id = 0;  //exit selected
            break;
    }
  }
  delay(DELAY_STANDARD);
}  //END LOOP()

void runAlkalinityTest() 
{
  digitalWrite(PIN_VINT, HIGH);
  t = rtc.getTime();
  byte t_year = (byte)(t.year - 2000);
  byte t_month = t.mon;
  byte t_day = t.date;
  byte t_hour = t.hour;
  byte t_min = t.min;
  byte t_sec = t.sec;
  float ph = 10;
  float dkh;
  float dkh_std = 0.0;
  float DKH_to_Compensate = (needed_KH - dkh_std) / 0.1; // MLs needed from KH Solution to increase 100L of tank water by KH difference (Needed KH - Resulted KG) 
  float Water_Volume_Calculation = water_Volume / 100; // Used to know the number of times to add 0.1 DKH to your total system
  float KH_Dosing_Amount = DKH_to_Compensate * Water_Volume_Calculation; // Total MLs of KH solution to be added to keep the needed KH
  float test_result[2][50];
  int iter_count = 0;

  msg = "KH-MON: 20"+ String(t_year)+"-"; 
  if(t_month<10){msg = msg+"0";}
    msg = msg + String(t_month) + "-";
  if(t_day<10){msg = msg + "0";}
    msg = msg + String(t_day) +  " / ";
  if(t_hour<10){msg = msg + "0";}
    msg = msg + String(t_hour) + ":";
  if(t_min<10){msg = msg + "0";}
    msg = msg + String(t_min);
  msg = msg + "  Test in progress ..";
  
  initial_ph = getPH(250);
  Serial.print("New test initiated..");
  Serial.print("\n");
  Serial.print("\n- Current PH : ");
  Serial.println(initial_ph);
  Serial.print("\n- Last KH : ");
  Serial.print(last_dkh_test.dkh);
  Serial.println(" DKH");
  Serial.print("\n");
  Serial.println(msg);

  lcd.setTextSize(1);
  lcd.print("\n");
  msg_1 = "\nBase PH: "+String(initial_ph,2);
  msg_2 = "";
  msg_3 = "";
  msg_4 = "";
  delayDisplayCountdown(5);

  addAcid(2, false); // amount of acid to be added to make sure that reaction chamber PH will change to make sure that priming acid line process is ok

  stir(STD_STIR_TIME);
  lcd.print("\n");
  msg_1 = "\nPriming acid";
  msg_2 = "line..";
  msg_3 = "";
  msg_4 = "";
  delayDisplayCountdown(5);

  test_ph = getPH(250);

  lcd.print("\n");
  msg_1 = "\nBase PH: "+ String(test_ph,2);
  msg_2 = "";
  msg_3 = "";
  msg_4 = "";
  delayDisplayCountdown(5);

  if (initial_ph - test_ph < 0.1) {
    msg = "KH-MON: WARNING! Acid priming failed";
    Serial.print(msg);
    Serial.println();
    Serial.print("\n- Initial PH: ");
    Serial.println(initial_ph);
    Serial.print("\n- Test PH: ");
    Serial.println(test_ph);
    Serial.print("\nPriming was not successful while checking PH  ");

    lcd.print("\n");
    msg_1 = "\nWARNING !! ";
    msg_2 = "Acid priming";
    msg_3 = "Failed";
    msg_4 = "";
    delayDisplayCountdown(5);
  }

  lcd.clearDisplay();
  lcd.setCursor(0, 0);
  lcd.print("\n");
  lcd.println("\nEmptying ...");
  lcd.println("reaction chamber");
  lcd.display();

  removeWater(135);

  lcd.clearDisplay();
  lcd.setCursor(0, 0);
  lcd.print("\n");
  lcd.println("\nPriming ...");
  lcd.println("inlet tubing");
  lcd.display();

  addWater(25);
  
  removeWater(35);
  
  lcd.clearDisplay();
  lcd.setCursor(0, 0);
  lcd.print("\n");
  lcd.println("\nAdding ...");
  lcd.println("100ml sample water");
  lcd.display();

  addWater(100);
  
  stir(STD_STIR_TIME);
  lcd.clearDisplay();
  lcd.setCursor(0, 0);
  lcd.print("\n");
  lcd.println("\nChecking base PH");
  lcd.display();

  initial_ph = getPH(1000);
  test_result[0][iter_count] = 0.0;
  test_result[1][iter_count] = initial_ph;
  iter_count++;

  lcd.print("\n");
  msg_1 = "\nBase PH: "+ String(initial_ph,2);
  msg_2 = "";
  msg_3 = "";
  msg_4 = "";
  delayDisplayCountdown(5);

  float total_acid_added = 0;
  bool continue_test = true;

  float acid_prev; float ph_prev; int stir_seconds; 

  lcd.print("\n");
  msg_1 = "\nStarting test";
  msg_2 = "";
  msg_3 = "";
  msg_4 = "";
  delayDisplayCountdown(0);

  lcd.print("\n");

  while (continue_test) {
    float addAcidVol; int ph_sample_size; long delay_sec;
    if (ph > 7) 
    {
      addAcidVol = 4;
      ph_sample_size = 50;
      delay_sec = 5;
      stir_seconds = STD_STIR_TIME;
    } 
    else 
    if (ph > 6) 
    {
      addAcidVol = 2;
      ph_sample_size = 150;
      delay_sec = 5;
      stir_seconds = STD_STIR_TIME_PH6;
    } 
    else 
    if (ph > 5) 
    {
      addAcidVol = 1;
      ph_sample_size = 200;
      delay_sec = 5;
      stir_seconds = STD_STIR_TIME_PH5;
    } 
    else 
    if (ph > 4.85) 
    {
      addAcidVol = 0.142856; //0.1 DKH;
      ph_sample_size = 250;
      delay_sec = 5;
      stir_seconds = STD_STIR_TIME_PH4;
    } 
    else 
    {
      addAcidVol = 0.142856*0.5; //0.05 DKH
      ph_sample_size = 400;
      delay_sec = 5;
      stir_seconds = STD_STIR_TIME_PH4;
    };

    acid_prev = total_acid_added;
    ph_prev = ph;

    total_acid_added = total_acid_added + addAcidVol;

    addAcid(addAcidVol, true);

    stir(stir_seconds);
    delayDisplayCountdown(delay_sec);

    ph = getPH(ph_sample_size);

    if(iter_count<50)
    {
      test_result[0][iter_count] = total_acid_added;
      test_result[1][iter_count] = ph;
    };

    if (ph < PH_ENDPOINT) {
      continue_test = false;
    } else {
      continue_test = true;
    }

    dkh = getDKH(100, total_acid_added, ACID_CONCENTRATION);

    msg_1 = "\nStep :"+ String(iter_count);
    msg_2 = "PH   :" + String(ph,2);
    msg_3 = "Acid :" + String(total_acid_added,2) + " ml";
    msg_4 = "DKH  :" + String(dkh,2);
    delayDisplayCountdown(0);

    if (total_acid_added > MAX_ACID_VOLUME_PER_TEST) {
      continue_test = false;
      dkh = 0.0;
      Compensate_KH = false;
      Serial.print("Test has stopped , Too many acid added .. <<KH-Guardian>> troubleshooting is advised !");
      Serial.println();
      Serial.print("\n- Current KH : ");
      Serial.print(getDKH(100, total_acid_added, ACID_CONCENTRATION));
      Serial.println(" DKH");
      Serial.print("\n- Current PH : ");
      Serial.println(getPH(ph_sample_size));
    } else {
      Compensate_KH = true;
    }
    iter_count = iter_count+1;
  }

  addAcid(0.0, false);  //release acid pump holding torque

  float ph_left = ph; float ph_right = ph_prev;
  float acid_left = total_acid_added; float acid_right = acid_prev;

  float acid_std = (PH_ENDPOINT - ph_left) / (ph_right - ph_left) * acid_right + (PH_ENDPOINT - ph_right) / (ph_left - ph_right) * acid_left;

  if (dkh != 0) {
    dkh_std = getDKH(100, acid_std, ACID_CONCENTRATION);
  }

  msg = "\nKH-MON: Test results - 20"+  String(t_year)+ "-";
  if(t_month<10){msg = msg + "0";}
    msg = msg + String(t_month) + "-";
  if(t_day<10){msg = msg + "0";} 
    msg = msg + String(t_day)+ " / ";
  if(t_hour<10){msg = msg + "0";}
    msg = msg + String(t_hour)+ ":";
  if(t_min<10){msg = msg + "0";}
    msg = msg + String(t_min);
  msg = msg + "  DKH: " + String(dkh_std,2);
  
  // Serial.println(msg);
  // Serial.print("Step# : ACID VOL : PH");

  // for(int ii=0;ii<max(iter_count,50);ii++)
  // {
  //   Serial.print(ii);Serial.print(" : "); Serial.print(test_result[0][ii],2); Serial.print(" : "); Serial.println(test_result[1][ii],2);
  // }

  // Doser will run after full test completed 

  Serial.print(msg);
  Serial.println();
  Serial.print("\nReaction REAGENT used during the test: ");
  Serial.print(total_acid_added);
  Serial.println(" MLs");
  Serial.println();
  Serial.print("KH Compenstion : ");

  if (!Compensate_KH) { 
      Serial.println(" 0 DKH");
  } else {
    if ((needed_KH - dkh_std) > 0.5) {
      Serial.print(needed_KH - dkh_std);
      Serial.println(" DKH");
      Serial.print("\nHowever KH injection reduced to 0.5 DKH to avoid high risk of sudden KH swings");
    } else {
      if ((needed_KH - dkh_std) <= 0) {
        Serial.println(" 0 DKH - Current KH is already greater than Needed KH");
    } else {
      if ((needed_KH - dkh_std) > 0 && (needed_KH - dkh_std) <= 0.5) {
        Serial.print(needed_KH - dkh_std);
        Serial.println(" DKH");
      }
    }
    }
  }

  last_dkh_test.t_month = t_month;
  last_dkh_test.t_day = t_day;
  last_dkh_test.t_hour = t_hour;
  last_dkh_test.t_minute = t_min;
  last_dkh_test.dkh = dkh_std;
  saveLastDKH();
  loadLastDKH();

  lcd.print("\n");
  msg_1 = "\nEnd of test:";
  msg_2 = "DKH   : "+ String(dkh_std, 2);
  msg_3 = "EP PH : " + String(ph, 2);
  msg_4 = "Steps : " + String(iter_count);
  delayDisplayCountdown(60);

  lcd.clearDisplay();
  lcd.setCursor(0, 0);
  lcd.print("\n");
  lcd.print("\nSaving in EEPROM..");
  lcd.display();

  used_reagent_volume = used_reagent_volume + total_acid_added + 2.0;
  saveSettings();
  delay(1000);

  lcd.clearDisplay();
  lcd.setCursor(0, 0);
  lcd.print("\n");
  lcd.println("\nCleaning ...");
  lcd.print("Reaction chamber");
  lcd.display();

  removeWater(130 + total_acid_added);

  if (Compensate_KH) { 
    if ((needed_KH - dkh_std) <= 0) {
      lcd.clearDisplay();
      lcd.setCursor(0, 0);
      lcd.print("\n");
      lcd.println("\nNo KH");
      lcd.println("\nCompensation");
      lcd.println("Needed");
      lcd.display();
    } else {
      if ((needed_KH - dkh_std) > 0.5) {
        float new_KH_Dosing_Amount = 0.5/0.1; 
        lcd.clearDisplay();
        lcd.setCursor(0, 0);
        lcd.print("\n");
        lcd.print("KH needed : ");
        lcd.print((needed_KH - dkh_std));
        lcd.print("\n");
        lcd.print("Reducing KH");
        lcd.println("\nCompensation");
        lcd.println("\nto 0.5 DKH");
        lcd.display();
        addKH(new_KH_Dosing_Amount * Water_Volume_Calculation);
    } else {
      if ((needed_KH - dkh_std) > 0 && (needed_KH - dkh_std) <= 0.5) {
        lcd.clearDisplay();
        lcd.setCursor(0, 0);
        lcd.print("\n");
        lcd.println("\nCompensating ...");
        lcd.print("\n");
        lcd.print(needed_KH - last_dkh_test.dkh);
        lcd.println(" DKH");
        lcd.display();
        addKH((needed_KH - last_dkh_test.dkh) / 0.1 * Water_Volume_Calculation);  // In case KH compensation will not exceed 0.5 DKH per test
      }
    }
    }
  }

  delay(5000);
  
  lcd.clearDisplay();
  lcd.setCursor(0, 0);
  lcd.print("\n");
  lcd.println("\nAdding ...");
  lcd.print("tank water");
  lcd.display();

  addWater(100);

  stir(STD_STIR_TIME);
  delay(1000);
  digitalWrite(PIN_VINT, LOW);
}

float calibrate_PH(bool is_ph_4) {
  lcd.display();
  lcd.setTextSize(1);
  float voltage_avg = 0.0;
  float this_voltage;
  float curr_ph;
  byte iteration_count = 50;
  byte sample_size = 100;

  for (int i = 0; i < iteration_count; i++) {
    this_voltage = getVoltage(sample_size);
    voltage_avg += this_voltage / iteration_count;
    curr_ph = voltageToPH(this_voltage);
    lcd.clearDisplay();
    lcd.setCursor(0, 0);
    lcd.display();  
    lcd.print("\n");
    msg = "VOL:  ";
    msg = msg + String(this_voltage,3);
    lcd.print(msg);

    if(is_ph_4)
    {
      lcd.print("\n");
      msg = "PH4:  ";
    }
    else
    {
      lcd.print("\n");
      msg = "PH7:  ";   
    }
    msg = msg + String(curr_ph,2);
    lcd.print(msg);
    lcd.print("\n");
    lcd.print("ITR:  ");
    lcd.print(i);
    lcd.print(" / ");
    lcd.println(iteration_count);

    lcd.display();
    delay(500);
  }
  return voltage_avg;
}

void dose(int doser_id, float volume, bool hold_torque) {
  bool forward_dir;
  byte PIN_ENABLE;
  byte PIN_STEP;
  byte PIN_DIR;
  float calibration_turns;
  switch (doser_id) {
    case DOSER_ID_1:
      PIN_ENABLE = PIN_ENABLE_M1;
      PIN_STEP = PIN_STEP_M1;
      PIN_DIR = PIN_DIR_M1;
      forward_dir = PUMP_1_FWD_DIR;
      calibration_turns = d1_calibration_turns;
      break;
    case DOSER_ID_2:
      PIN_ENABLE = PIN_ENABLE_M2;
      PIN_STEP = PIN_STEP_M2;
      PIN_DIR = PIN_DIR_M2;
      forward_dir = PUMP_2_FWD_DIR;
      calibration_turns = d2_calibration_turns;
      break;
    case DOSER_ID_3:
      PIN_ENABLE = PIN_ENABLE_M3;
      PIN_STEP = PIN_STEP_M3;
      PIN_DIR = PIN_DIR_M3;
      forward_dir = PUMP_3_FWD_DIR;
      calibration_turns = d3_calibration_turns;
      break;
    case DOSER_ID_4:
      PIN_ENABLE = PIN_ENABLE_M4;
      PIN_STEP = PIN_STEP_M4;
      PIN_DIR = PIN_DIR_M4;
      forward_dir = PUMP_4_FWD_DIR;
      calibration_turns = d4_calibration_turns;
      break;
  }
  int delay_ms;
  switch (pump_speed) {
    case 1:
      delay_ms = 66;
      break;
    case 2:
      delay_ms = 100;
      break;
    case 3:
      delay_ms = 150;
      break;
    case 4:
      delay_ms = 225;
      break;
    default:
      delay_ms = 600;
      break;
  }
  if (doser_id == DOSER_ID_1) {
    //for acid pump - disregard speed setting
    delay_ms = 225;
  }

  if (forward_dir) {
    digitalWrite(PIN_DIR, true);
  } else {
    digitalWrite(PIN_DIR, false);
  }

  digitalWrite(PIN_ENABLE, LOW);
  float n_turns = volume / CALIBRATION_VOLUME_ML * calibration_turns;
  for (long i = (long)(1600.0 * n_turns); i > 0; i--) {
    digitalWrite(PIN_STEP, HIGH);
    delayMicroseconds(delay_ms);
    digitalWrite(PIN_STEP, LOW);
    delayMicroseconds(delay_ms);
  }
  if (!hold_torque) {
    digitalWrite(PIN_ENABLE, HIGH);
  }
}

float chooseVolume(float current_dosing_amount, float increment) {
  lcd.setTextSize(2);
  lcd.clearDisplay();
  lcd.setCursor(0, 0);
  lcd.display();
  encoder_position = ss.getEncoderPosition();
  int enc_pos = encoder_position;
  float new_dosing_amount;
  int prev_enc_pos;
  prev_enc_pos = -9999;
  
  do {
    encoder_position = ss.getEncoderPosition();
    lcd.setCursor(0, 0);
    if(prev_enc_pos!=encoder_position)
    {
      prev_enc_pos = encoder_position;
      lcd.clearDisplay();
      lcd.print("\n");
      new_dosing_amount = current_dosing_amount + (encoder_position - enc_pos) * increment;
      new_dosing_amount = round(new_dosing_amount * 10.0) / 10.0;
      if (new_dosing_amount <= 0) {
        int enc_pos = encoder_position;
        new_dosing_amount = 0;
        lcd.println("\nOFF");
      } else {
        msg = "\n";
        msg = msg + String(new_dosing_amount+0.0001,2);
        lcd.print(msg);
      };
      lcd.setCursor(0, 0);
      lcd.display();
    }
  } while (ss.digitalRead(SS_SWITCH));
  while (!ss.digitalRead(SS_SWITCH));  //wait till switch is released
  return max(new_dosing_amount, 0.0);
}

float chooseWaterVolume(float current_water_volume, float increment) {
  lcd.setTextSize(2);
  lcd.clearDisplay();
  lcd.setCursor(0, 0);
  lcd.display();
  encoder_position = ss.getEncoderPosition();
  int enc_pos = encoder_position;
  float new_water_volume;
  int prev_enc_pos;
  prev_enc_pos = -9999;
  
  do {
    encoder_position = ss.getEncoderPosition();
    lcd.setCursor(0, 0);
    if(prev_enc_pos!=encoder_position)
    {
      prev_enc_pos = encoder_position;
      lcd.clearDisplay();
      lcd.print("\n");
      new_water_volume = current_water_volume + (encoder_position - enc_pos) * increment;
      new_water_volume = round(new_water_volume * 10.0) / 10.0;
      if (new_water_volume <= 0) {
        int enc_pos = encoder_position;
        new_water_volume = 0;
        lcd.println("\nOFF");
      } else {
        msg = "\n";
        msg = msg + String(new_water_volume+0.0001,2);
        lcd.print(msg);
      };
      lcd.setCursor(0, 0);
      lcd.display();
    }
  } while (ss.digitalRead(SS_SWITCH));
  while (!ss.digitalRead(SS_SWITCH));  //wait till switch is released
  return max(new_water_volume, 0.0);
}

float chooseKHPer100ML(float current_KH_Per_100ML, float increment) {
  lcd.setTextSize(2);
  lcd.clearDisplay();
  lcd.setCursor(0, 0);
  lcd.display();
  encoder_position = ss.getEncoderPosition();
  int enc_pos = encoder_position;
  float new_KH_Per_100ML;
  int prev_enc_pos;
  prev_enc_pos = -9999;
  
  do {
    encoder_position = ss.getEncoderPosition();
    lcd.setCursor(0, 0);
    if(prev_enc_pos!=encoder_position)
    {
      prev_enc_pos = encoder_position;
      lcd.clearDisplay();
      lcd.print("\n");
      new_KH_Per_100ML = current_KH_Per_100ML + (encoder_position - enc_pos) * increment;
      new_KH_Per_100ML = round(new_KH_Per_100ML * 10.0) / 10.0;
      if ( new_KH_Per_100ML <= 0) {
        int enc_pos = encoder_position;
         new_KH_Per_100ML = 0;
        lcd.println("\nOFF");
      } else {
        msg = "\n";
        msg = msg + String( new_KH_Per_100ML+0.0001,2);
        lcd.print(msg);
      };
      lcd.setCursor(0, 0);
      lcd.display();
    }
  } while (ss.digitalRead(SS_SWITCH));
  while (!ss.digitalRead(SS_SWITCH));  //wait till switch is released
  return max(new_KH_Per_100ML, 0.0);
}

float chooseNeededKH(float current_needed_KH, float increment) {
  lcd.setTextSize(2);
  lcd.clearDisplay();
  lcd.setCursor(0, 0);
  lcd.display();
  encoder_position = ss.getEncoderPosition();
  int enc_pos = encoder_position;
  float new_needed_KH;
  int prev_enc_pos;
  prev_enc_pos = -9999;
  
  do {
    encoder_position = ss.getEncoderPosition();
    lcd.setCursor(0, 0);
    if(prev_enc_pos!=encoder_position)
    {
      prev_enc_pos = encoder_position;
      lcd.clearDisplay();
      lcd.print("\n");
      new_needed_KH = current_needed_KH + (encoder_position - enc_pos) * increment;
      new_needed_KH = round(new_needed_KH * 10.0) / 10.0;
      if (new_needed_KH <= 0) {
        int enc_pos = encoder_position;
         new_needed_KH = 0;
        lcd.println("\nOFF");
      } else {
        msg = "\n";
        msg = msg + String( new_needed_KH+0.0001,2);
        lcd.print(msg);
      };
      lcd.setCursor(0, 0);
      lcd.display();
    }
  } while (ss.digitalRead(SS_SWITCH));
  while (!ss.digitalRead(SS_SWITCH));  //wait till switch is released
  return max(new_needed_KH, 0.0);
}

bool chooseTrueFalse(bool current_choice) {
  lcd.setTextSize(2);
  lcd.clearDisplay();
  lcd.setCursor(0, 0);
  lcd.display();
  int encoder_start = ss.getEncoderPosition();
  bool new_choice = current_choice;
  do {
    int encoder_current = ss.getEncoderPosition();
    lcd.clearDisplay();
    lcd.setCursor(0, 0);
    int cnt = abs((encoder_start - encoder_current) % 2);
    if (cnt == 0) {
      new_choice = current_choice;
    } else {
      new_choice = !current_choice;
    }
    if (new_choice) {
      lcd.print("\nTRUE");
    } else {
      lcd.print("\nFALSE");
    }
    lcd.display();
  } while (ss.digitalRead(SS_SWITCH));
  while (!ss.digitalRead(SS_SWITCH));  //wait till switch is released
  return new_choice;
}

byte chooseFrequency(byte current_freq, byte freq_min, byte freq_max) {
  lcd.setTextSize(2);
  lcd.clearDisplay();
  lcd.setCursor(0, 0);
  lcd.display();
  encoder_position = ss.getEncoderPosition();
  int enc_pos = encoder_position;
  byte new_freq;
  do {
    encoder_position = ss.getEncoderPosition();
    lcd.clearDisplay();
    lcd.setCursor(0, 0);
    new_freq = current_freq + (encoder_position - enc_pos);
    if (new_freq <= freq_min) {
      new_freq = freq_min;
      encoder_position = ss.getEncoderPosition();
    } else if (new_freq >= freq_max) {
      new_freq = freq_max;
      encoder_position = ss.getEncoderPosition();
    }
    lcd.print("\n");
    lcd.print(new_freq);
    lcd.display();
  } while (ss.digitalRead(SS_SWITCH));
  while (!ss.digitalRead(SS_SWITCH));  //wait till switch is released
  return new_freq;
}

byte chooseSpeed(byte current_speed) {
  lcd.setTextSize(2);
  lcd.clearDisplay();
  lcd.setCursor(0, 0);
  lcd.display();
  encoder_position = ss.getEncoderPosition();
  int enc_pos = encoder_position;
  byte new_speed;
  do {
    encoder_position = ss.getEncoderPosition();
    lcd.clearDisplay();
    lcd.setCursor(0, 0);
    new_speed = current_speed + (encoder_position - enc_pos);
    if (new_speed < 1) {
      new_speed = 1;
      encoder_position = ss.getEncoderPosition();
    } else if (new_speed > 4) {
      new_speed = 4;
      encoder_position = ss.getEncoderPosition();
    }
    lcd.print("\n");
    lcd.print(new_speed);
    lcd.display();
  } while (ss.digitalRead(SS_SWITCH));
  while (!ss.digitalRead(SS_SWITCH));  //wait till switch is released
  return new_speed;
}

int chooseInteger(int current_val,int min_val, int max_val ) // Set Date/Time Menu
{
    lcd.setTextSize(2);
    lcd.clearDisplay();
    lcd.setCursor(0, 0);
    lcd.display();
    encoder_position = ss.getEncoderPosition();
    int enc_pos = encoder_position;    
    int new_val;
    do
    {
      encoder_position = ss.getEncoderPosition();
      lcd.clearDisplay();
      lcd.setCursor(0, 0);
      new_val = current_val + (encoder_position - enc_pos);
      if(new_val<min_val)
      {
        new_val = min_val;
        encoder_position = ss.getEncoderPosition();
        enc_pos--;
      }
      else
      if(new_val>max_val)
      {
        new_val = max_val;
        enc_pos++;
        encoder_position = ss.getEncoderPosition();
      }
    lcd.print("\n");
    lcd.print(new_val);
    lcd.display();
    } while(ss.digitalRead(SS_SWITCH));
    while(!ss.digitalRead(SS_SWITCH)); //wait till switch is released     
    return new_val;
}

int displayMenu(char menuInput[][maxItemLength], int menuLength) {
  int enc_pos;
  lcd.setTextSize(1);
  lcd.clearDisplay();
  lcd.setCursor(0, 0);
  lcd.display();

  do {
    lcd.clearDisplay();
    lcd.setCursor(0, 0);
    encoder_position = ss.getEncoderPosition();
    enc_pos = abs(encoder_position % menuLength);
    int start_pos = 0;
    if (enc_pos >= SCREEN_ROWS) {
      start_pos = enc_pos - SCREEN_ROWS + 1;
    }

    for (int cnt = start_pos; cnt < start_pos + SCREEN_ROWS; cnt++) {
      if (cnt == enc_pos) {
        lcd.print("->");
      } else {
        lcd.print("  ");
      }
      lcd.println(menuInput[cnt]);
    }
    lcd.display();
  } while (ss.digitalRead(SS_SWITCH));
  while (!ss.digitalRead(SS_SWITCH));  //wait till switch is released
  lcd.setTextSize(2);
  return enc_pos;
}

void showPHSettings() {
  lcd.setTextSize(1);
  lcd.clearDisplay();
  lcd.setCursor(0, 0);
  lcd.display();
  do {
    lcd.setCursor(0, 0);
    lcd.print("\n");
    lcd.print("\nPH 4 : ");
    msg = String(voltage_4PH,3) + "v";
    lcd.print(msg);
    lcd.print("\n");
    lcd.print("\nPH 7 : ");
    msg_1 = String(voltage_7PH,3)+ "v";
    lcd.print(msg_1);
    lcd.display();
    delay(100);
  } while (ss.digitalRead(SS_SWITCH));
  while (!ss.digitalRead(SS_SWITCH))
    ;  //wait till switch is released
  lcd.setTextSize(2);
  return;
}


void showPumpSettings() {
  lcd.setTextSize(1);
  lcd.clearDisplay();
  lcd.setCursor(0, 0);
  lcd.display();
  do {
    lcd.setCursor(0, 0);
    lcd.print("\n");
    msg = "High pump:";
    msg = msg + String(d1_calibration_turns,2);
    lcd.print(msg);
    lcd.print("\n");
    msg = "Mid  pump:";
    msg = msg + String(d2_calibration_turns,2);
    lcd.print(msg);
    lcd.print("\n");
    msg = "Low  pump:";
    msg = msg + String(d3_calibration_turns,2);
    lcd.print(msg);
    lcd.print("\n");
    msg = "Dosing  pump:";
    msg = msg + String(d4_calibration_turns,2);
    lcd.print(msg);

    lcd.display();
    delay(100);
  } while (ss.digitalRead(SS_SWITCH));
  while (!ss.digitalRead(SS_SWITCH));  //wait till switch is released
  lcd.setTextSize(2);
  return;
}

void loadSettings(bool print_results) {
  int eprom_pos = 0;
  d1_calibration_turns = EEPROM.get(eprom_pos, d1_calibration_turns); eprom_pos += sizeof(d1_calibration_turns);
  d2_calibration_turns = EEPROM.get(eprom_pos, d2_calibration_turns); eprom_pos += sizeof(d2_calibration_turns);
  d3_calibration_turns = EEPROM.get(eprom_pos, d3_calibration_turns); eprom_pos += sizeof(d3_calibration_turns);
  d4_calibration_turns = EEPROM.get(eprom_pos, d4_calibration_turns); eprom_pos += sizeof(d4_calibration_turns);

  voltage_4PH = EEPROM.get(eprom_pos, voltage_4PH); eprom_pos += sizeof(voltage_4PH);
  voltage_7PH = EEPROM.get(eprom_pos, voltage_7PH); eprom_pos += sizeof(voltage_7PH);

  pump_speed = EEPROM.get(eprom_pos, pump_speed); eprom_pos += sizeof(pump_speed);
  reagent_volume = EEPROM.get(eprom_pos, reagent_volume); eprom_pos += sizeof(reagent_volume);
  refill_date.y = EEPROM.get(eprom_pos, refill_date.y); eprom_pos += sizeof(refill_date.y);
  refill_date.m = EEPROM.get(eprom_pos, refill_date.m); eprom_pos += sizeof(refill_date.m);
  refill_date.d = EEPROM.get(eprom_pos, refill_date.d); eprom_pos += sizeof(refill_date.d);
  used_reagent_volume = EEPROM.get(eprom_pos, used_reagent_volume); eprom_pos += sizeof(used_reagent_volume);
  schedule_tests_per_day = EEPROM.get(eprom_pos, schedule_tests_per_day); eprom_pos += sizeof(schedule_tests_per_day);

  water_Volume = EEPROM.get(eprom_pos, water_Volume); eprom_pos += sizeof(water_Volume);
  Add_KH_Per_100ML = EEPROM.get(eprom_pos, Add_KH_Per_100ML); eprom_pos += sizeof(Add_KH_Per_100ML);
  needed_KH = EEPROM.get(eprom_pos, needed_KH); eprom_pos += sizeof(needed_KH);

  if(print_results)
  {
    Serial.print("\n");
    Serial.println(">>KH Guardian saved settings<<"); 
    Serial.print("> Re-Agent Pump:"); Serial.println(d1_calibration_turns,1);
    Serial.print("> Sample Water Pump:"); Serial.println(d2_calibration_turns,1);
    Serial.print("> Waste Pump:"); Serial.println(d3_calibration_turns,1);
    Serial.print("> Dosing Pump:"); Serial.println(d4_calibration_turns,1);
    Serial.print("> Pumps Speed:"); Serial.println(pump_speed);
    Serial.print("> Acid Refill date: "); Serial.print("20");Serial.print(refill_date.y);Serial.print("-");Serial.print(refill_date.m);Serial.print("-");Serial.println(refill_date.d);
    Serial.print("> Scheduled tests per day: ");Serial.println(schedule_tests_per_day);
    Serial.print("> Acid Concentration: ");Serial.print(ACID_CONCENTRATION,2);Serial.println(" %");
    Serial.print("> PH Calibration 4: ");Serial.print(voltage_4PH,2);Serial.println(" V");
    Serial.print("> PH Calibration 7: ");Serial.print(voltage_7PH,2);Serial.println(" V");
    Serial.print("> Water Volume: ");Serial.print(water_Volume,2);Serial.println(" liters");
    Serial.print("> KH Solution to increase 0.1 DKH for each 100 liters: ");Serial.print(Add_KH_Per_100ML,2);Serial.println(" ml");
    Serial.print("> Needed KH: ");Serial.print(needed_KH,2);Serial.println(" DKH");        
  }    
}

void saveSettings() {
  int eprom_pos = 0;
  EEPROM.put(eprom_pos, d1_calibration_turns); eprom_pos += sizeof(d1_calibration_turns);
  EEPROM.put(eprom_pos, d2_calibration_turns); eprom_pos += sizeof(d2_calibration_turns);
  EEPROM.put(eprom_pos, d3_calibration_turns); eprom_pos += sizeof(d3_calibration_turns);
  EEPROM.put(eprom_pos, d4_calibration_turns); eprom_pos += sizeof(d4_calibration_turns);

  EEPROM.put(eprom_pos, voltage_4PH); eprom_pos += sizeof(voltage_4PH);
  EEPROM.put(eprom_pos, voltage_7PH); eprom_pos += sizeof(voltage_7PH);

  EEPROM.put(eprom_pos, pump_speed); eprom_pos += sizeof(pump_speed);
  EEPROM.put(eprom_pos, reagent_volume); eprom_pos += sizeof(reagent_volume);
  EEPROM.put(eprom_pos, refill_date.y); eprom_pos += sizeof(refill_date.y);
  EEPROM.put(eprom_pos, refill_date.m); eprom_pos += sizeof(refill_date.m);
  EEPROM.put(eprom_pos, refill_date.d); eprom_pos += sizeof(refill_date.d);
  EEPROM.put(eprom_pos, used_reagent_volume); eprom_pos += sizeof(used_reagent_volume);
  EEPROM.put(eprom_pos, schedule_tests_per_day); eprom_pos += sizeof(schedule_tests_per_day);

  EEPROM.put(eprom_pos, water_Volume); eprom_pos += sizeof(water_Volume);
  EEPROM.put(eprom_pos, Add_KH_Per_100ML); eprom_pos += sizeof(Add_KH_Per_100ML);
  EEPROM.put(eprom_pos, needed_KH); eprom_pos += sizeof(needed_KH);      
}

void receiveEvent(int how_many) {
  int cnt = 0;
  while (Wire.available()) {            // slave may send less than requested
    receive_buffer[cnt] = Wire.read();  // receive a byte as character
    cnt++;
  }
  receive_buffer[cnt] = '\0';
  receive_flag = true;
  //delay(100);
}

void resetBuffer(void) {
  receive_flag = false;
  for (int i = 0; i < BUFFER_SIZE; i++) {
    receive_buffer[i] = ' ';
  }
}

float getDKH(float sample_vol, float acid_vol, float acid_str){
  return acid_vol / sample_vol * 2800.0 * acid_str ;
}

void stir(long secs)
{
    digitalWrite(PIN_RELAY,HIGH);
    long delay_time = secs*1000;
    delay(delay_time);
    digitalWrite(PIN_RELAY,LOW);
}

void addAcid(float volume_ml, bool hold_torque) {
  dose(DOSER_ID_1, volume_ml, hold_torque);
}

void removeWater(float volume_ml) {
  dose(DOSER_ID_3, volume_ml, false);
}

void addWater(float volume_ml) {
  dose(DOSER_ID_2, volume_ml, false);
}

void addKH(float volume_ml) {
  dose(DOSER_ID_4, volume_ml, false);
}

void delayDisplayCountdown(int seconds) {
  lcd.setTextSize(1);
  for (int ii = seconds; ii > 0; ii--) {
    lcd.clearDisplay();
    lcd.setCursor(0, 0);
    lcd.print(msg_1);
    lcd.print("  ");
    lcd.print("[");
    if (ii < 10) {
      lcd.print("00");
    } else if (ii < 100) {
      lcd.print("0");
    }
    lcd.print(ii);
    lcd.println("]");

    lcd.println(msg_2);
    lcd.println(msg_3);
    lcd.println(msg_4);
    lcd.display();
    delay(1000);
  }
  lcd.clearDisplay();
  lcd.setCursor(0, 0);
  lcd.println(msg_1);
  lcd.println(msg_2);
  lcd.println(msg_3);
  lcd.println(msg_4);
  lcd.display();
}

float getPH(int sample_size) {
  return voltageToPH(getVoltage(sample_size));
}

float voltageToPH(float voltage) {
  float ph = (voltage - voltage_7PH) / (voltage_4PH - voltage_7PH) * 4.0 + (voltage - voltage_4PH) / (voltage_7PH - voltage_4PH) * 7.0;
  return ph;
}

float getVoltage(int sample_size) {
  int this_voltage;
  float avg_voltage = 0.0;
  int idx = 0;
  while (idx < sample_size) {
    static unsigned long samplingTime = millis();
    if (abs(millis() - samplingTime) > 20) {
      this_voltage = analogRead(PIN_PH);
      avg_voltage = avg_voltage + this_voltage;
      samplingTime = millis();
      idx++;
    }
  }
  avg_voltage = avg_voltage / (sample_size + 0.0) * 5.0 / 1024.0;
  return avg_voltage;
}

void saveLastDKH() {
  int eprom_pos = LAST_DKH_TEST_EEPROM_POSITION;
  EEPROM.put(eprom_pos, last_dkh_test.t_month); eprom_pos += sizeof(last_dkh_test.t_month);
  EEPROM.put(eprom_pos, last_dkh_test.t_day); eprom_pos += sizeof(last_dkh_test.t_day);
  EEPROM.put(eprom_pos, last_dkh_test.t_hour);  eprom_pos += sizeof(last_dkh_test.t_hour);
  EEPROM.put(eprom_pos, last_dkh_test.t_minute); eprom_pos += sizeof(last_dkh_test.t_minute);
  EEPROM.put(eprom_pos, last_dkh_test.dkh); eprom_pos += sizeof(last_dkh_test.dkh);
  delay(500);
}
void loadLastDKH() {
  int eprom_pos = LAST_DKH_TEST_EEPROM_POSITION;
  last_dkh_test.t_month = EEPROM.get(eprom_pos, last_dkh_test.t_month); eprom_pos += sizeof(last_dkh_test.t_month);
  last_dkh_test.t_day = EEPROM.get(eprom_pos, last_dkh_test.t_day); eprom_pos += sizeof(last_dkh_test.t_day);
  last_dkh_test.t_hour = EEPROM.get(eprom_pos, last_dkh_test.t_hour); eprom_pos += sizeof(last_dkh_test.t_hour);
  last_dkh_test.t_minute = EEPROM.get(eprom_pos, last_dkh_test.t_minute); eprom_pos += sizeof(last_dkh_test.t_minute);
  last_dkh_test.dkh = EEPROM.get(eprom_pos, last_dkh_test.dkh); eprom_pos += sizeof(last_dkh_test.dkh);
  delay(500);
}
