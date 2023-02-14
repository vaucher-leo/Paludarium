/*
 *  Project:  Paludarium
 *  Version:  2.0
 *  Author:   Leo Vaucher
 *  Date:     13.02.2023
 *
 *  Brief:    Manage all peripherals of a paludarium
 *
 *  TODO:     -Wait for user test
 *            -Comment code
 */

#include <krnl.h>
#include <I2C_RTC.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_SSD1306.h>
#include <EEPROM.h>

#include "logo_anim.h"

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 32 // OLED display height, in pixels
#define OLED_RESET     -1 // Reset pin # (or -1 if sharing Arduino reset pin)
#define SCREEN_ADDRESS 0x3C ///< See datasheet for Address; 0x3D for 128x64, 0x3C for 128x32

#define LOGO_HEIGHT   18
#define LOGO_WIDTH    64
#define TRANSITION_TIME 100

// stak to be used by task
#define STKSZ1 45
#define STKSZ2 170
char stak1[STKSZ1], stak2[STKSZ2], stak3[STKSZ1], stak4[STKSZ1], stak5[STKSZ1], stak6[STKSZ1], stak7[STKSZ1];

struct k_t *pt1,
  *pt2,
  *pt3,
  *pt4,
  *pt5,
  *pt6,
  *pt7; // pointer to task descriptor

enum btn_press {
  p_none = 0,
  p_bt1_s,  // Short
  p_bt1_l,  // Long
  p_bt1_r,  // Repeat
  p_bt2_s,
  p_bt2_l,
  p_bt2_r  
};

enum menus {
  m_off = 0,
  m_setup,
  m_lamp,
  m_buse,
  m_brume,
  m_bulle,
  m_time,
  m_save,
  m_exit
};

typedef struct BUTTON{
  uint8_t val:4;  // Button value
  uint8_t mode:4; // Button mode (short, long, repeat)
  uint8_t time;   // Time counter
};

typedef struct TIME{
  uint8_t hours;
  uint8_t minutes;
};

typedef struct LAMP{
  TIME onH;
  TIME offH;
  uint8_t active;
};

typedef struct BUSE{
  uint8_t tOn;
  TIME startH;
  TIME stopH;
  uint8_t active;
};

typedef struct EXTRAS{
  uint8_t nbr;
  uint8_t tOn;
  TIME times[10];
  uint8_t active;
};

//  Prototypes
void draw_logo(const unsigned char logo_bmp[]);
void boot_logo();
void input_task();
void screen_task();
void time_task();
void lamp_task();
void buse_task();
void brume_task();
void bulles_task();
void setup_menu();
void lamp_menu();
void extra_menu(EXTRAS *thing);
void buse_menu();
void time_menu();
void hourSelection();
void unavailable_menu();
void restoreFromEEPROM();
void saveToEEPROM();
void display_by_buffer(const char* const* addr);

// Devices initialization
static Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
static DS1307 RTC;

// Global vars
uint8_t menu = 0; // Menu position
uint8_t subMenu = 0; // Sub menu pos
uint8_t HorM = 0; // navigate between hour and min in hourSelection

uint8_t choosed = 0; // device choosed to edit
TIME timeSelected = {0, 0};

TIME globalTime = {0,0};
uint8_t sec = 0;

LAMP lamps[3];
uint8_t lampNbr = 0;
BUSE buse;
EXTRAS brume;
EXTRAS bulles;
uint8_t buseFlag = 0;

BUTTON bt1 = {0,0,0};
BUTTON bt2 = {0,0,0};
uint8_t press = 0;

const char aps_msg[] PROGMEM = "APS";
const char dbl_pt_msg[] PROGMEM = ":";
const char active_msg[] PROGMEM = "Active? ";
const char oui_msg[] PROGMEM = "oui";
const char non_msg[] PROGMEM = "non";
const char on_time_msg[] PROGMEM = " (On)";
const char off_time_msg[] PROGMEM = " (Off)";
const char choose_msg[] PROGMEM = "Choose: ";
const char tOn_msg[] PROGMEM = "Ton (";
const char tOn2_msg[] PROGMEM = "T on: ";
const char min_msg[] PROGMEM = "min)";
const char lPar_msg[] PROGMEM = " (";
const char h_msg[] PROGMEM = "h";
const char rPar_msg[] PROGMEM = ")";
const char soon_msg[] PROGMEM = "Soon...";
const char err_hor_msg[] PROGMEM =  "!Err hor!";
const char hOn_msg[] PROGMEM = " (hOn)";
const char full_mem_msg[] PROGMEM = "!Mem full!";

static const char *const msg[18] PROGMEM = {
  aps_msg,
  dbl_pt_msg,
  active_msg,
  oui_msg,
  non_msg,
  on_time_msg,
  off_time_msg,
  choose_msg,
  tOn_msg,
  tOn2_msg,
  min_msg,
  lPar_msg,
  h_msg,
  rPar_msg,
  soon_msg,
  err_hor_msg,
  hOn_msg,
  full_mem_msg
};

const char menu_0[] PROGMEM = "Lampe";
const char menu_1[] PROGMEM = "Buses";
const char menu_2[] PROGMEM = "Brume";
const char menu_3[] PROGMEM = "Bulles";
const char menu_4[] PROGMEM = "Heure";
const char menu_5[] PROGMEM = "Sauver";
const char menu_6[] PROGMEM = "Sortir";

const char lamp_0[] PROGMEM = "Bleu";
const char lamp_1[] PROGMEM = "Gros carre";
const char lamp_2[] PROGMEM = "Nuit";

const char extra_0[] PROGMEM = "Modifier";
const char extra_1[] PROGMEM = "Ajouter";

static const char *const menuName[7] PROGMEM = {menu_0, menu_1, menu_2, menu_3, menu_4, menu_5, menu_6};
static const char *const lampName[3] PROGMEM = {lamp_0, lamp_1, lamp_2};
static const char *const extraMenu[2] PROGMEM = {extra_0, extra_1};

char buffer[11];

uint8_t nbr_setup = 3;
uint8_t setupVal = 0;

// DEVICES
uint8_t bt1_pin = 9;
uint8_t bt2_pin = 16;
uint8_t lampPin[3] = {7,6,5};
uint8_t ledFond = 8;
uint8_t brumePin = 4;
uint8_t busePin = 3;
uint8_t bullesPin = 2;

// Main functions
void setup() {

  uint8_t k_err;

  Serial.begin(115200);
  RTC.begin();

  pinMode(bt1_pin, INPUT_PULLUP);
  pinMode(bt2_pin, INPUT_PULLUP);
  pinMode(lampPin[0], OUTPUT);
  pinMode(lampPin[1], OUTPUT);
  pinMode(lampPin[2], OUTPUT);
  pinMode(ledFond, OUTPUT);
  pinMode(brumePin, OUTPUT);
  pinMode(busePin, OUTPUT);
  pinMode(bullesPin, OUTPUT);

  // SSD1306_SWITCHCAPVCC = generate display voltage from 3.3V internally
  if(!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println(F("SSD1306 allocation failed"));
    for(;;); // Don't proceed, loop forever
  }
  
  display.clearDisplay();
  display.display();

  if (RTC.isRunning()){
    Serial.println(F("Clock checked!"));
    Serial.print(F("Time: "));
    Serial.print(RTC.getDay());
    Serial.print(F("-"));
    Serial.print(RTC.getMonth());
    Serial.print(F("-"));
    Serial.print(RTC.getYear());
    Serial.print(F(" "));
    Serial.print(RTC.getHours());
    Serial.print(F(":"));
    Serial.print(RTC.getMinutes());
    Serial.print(F(":"));
    Serial.println(RTC.getSeconds());

    globalTime.hours = RTC.getHours();
    globalTime.minutes = RTC.getMinutes();
  } else {
    display.drawRect(0, 0, display.width(), display.height(), SSD1306_WHITE);
    display.setTextSize(1);      // Normal 1:1 pixel scale
    display.setTextColor(SSD1306_WHITE); // Draw white text
    display.setCursor(10, 12);     // Start at top-left corner
    display.cp437(true);         // Use full 256 char 'Code Page 437' font
    display_by_buffer(&(msg[15]));
    display.display();
    while(1);
    //globalTime.hours = 10;
    //globalTime.minutes = 10;
  }
  
  boot_logo();
  display.setTextSize(1.2);      // Normal 1:1 pixel scale
  display.setTextColor(SSD1306_WHITE); // Draw white text
  display.setCursor(87, 8);     // Start at top-left corner
  display.cp437(true);         // Use full 256 char 'Code Page 437' font
  display_by_buffer(&(msg[0]));
  display.setCursor(87, 16);
  display.print(globalTime.hours);
  display_by_buffer(&(msg[1]));
  display.print(globalTime.minutes);
  display.display();
  
  k_init(7, 0, 0);

  delay(1000);

  lamps[1].onH.hours = 8;
  lamps[1].onH.minutes = 0;
  lamps[1].offH.hours = 22;
  lamps[1].offH.minutes = 0;
  buse.tOn = 5;
  buse.startH.hours = 8;
  buse.startH.minutes = 0;
  buse.stopH.hours = 21;
  buse.stopH.minutes = 0;

  if(EEPROM.read(0x00) == 0){
    menu = 1;
    Serial.println(F("Not configured, starting config..."));
  } else {
    Serial.println(F("Configured, running config..."));
    restoreFromEEPROM();
  }
  menu = m_setup;

  Serial.println(F("Startup complete!"));

  display.clearDisplay();
  display.display();

  pt1 = k_crt_task(menu_task, 6, stak2, STKSZ2);
  pt2 = k_crt_task(input_task, 4, stak1, STKSZ1);
  pt3 = k_crt_task(time_task, 3, stak3, STKSZ1);
  pt4 = k_crt_task(lamp_task, 5, stak4, STKSZ1);
  pt5 = k_crt_task(buse_task, 5, stak5, STKSZ1);
  pt6 = k_crt_task(brume_task, 5, stak6, STKSZ1);
  pt7 = k_crt_task(bulles_task, 5, stak7, STKSZ1);

  k_err = k_start(); // 1 milli sec tick

  Serial.print(F("Oh shit, kernel error!"));
  Serial.println(k_err);
	while (1) ;
}

void loop() {}

// Tasks functions
void input_task(){
  
  while (1){
    // Button management
    if(bt1.val == 0){
      if(digitalRead(bt1_pin) == 0){
        bt1.val = 1;
      }
    } else {
      if(digitalRead(bt1_pin) == 1){
        if(bt1.mode == 0){
          press = p_bt1_s;
        } else if(bt1.mode == 1){
          press = p_bt1_l;
        }
        digitalWrite(13, 0);
        bt1.mode = 0;
        bt1.time = 0;
        bt1.val = 0;
      }else{
        if(bt1.mode == 0){
          if(bt1.time == 8){
            bt1.mode = 1;
            digitalWrite(13, 1);
          }
        }else if(bt1.mode == 1){
          if(bt1.time == 20){
            bt1.mode = 3;
            digitalWrite(13, 0);
          }          
        }else{
          if(bt1.time >= 5){
            bt1.time = 0;
            press = p_bt1_r;
          }
        }
        bt1.time++;
      }
    }
    if(bt2.val == 0){
      if(digitalRead(bt2_pin) == 0){
        bt2.val = 1;
      }
    } else {
      if(digitalRead(bt2_pin) == 1){
        if(bt2.mode == 0){
          press = p_bt2_s;
        } else if(bt2.mode == 1){
          press = p_bt2_l;
        }
        digitalWrite(13, 0);
        bt2.mode = 0;
        bt2.time = 0;
        bt2.val = 0;
      }else{
        if(bt2.mode == 0){
          if(bt2.time == 8){
            bt2.mode = 1;
            digitalWrite(13, 1);
          }
        }else if(bt2.mode == 1){
          if(bt2.time == 20){
            bt2.mode = 3;
            digitalWrite(13, 0);
          }          
        }else{
          if(bt2.time >= 5){
            bt2.time = 0;
            press = p_bt2_r;            
          }
        }
        bt2.time++;
      }
    }
    k_sleep(50);
  }
}

void menu_task(){
  uint8_t localMenu = 0;
  display.clearDisplay();

  while(1){
    if(localMenu != 0){
      display.clearDisplay();
      if(menu != 0){
        display.drawRoundRect(0, 0, display.width(), display.height(), 4, SSD1306_WHITE);
        display.setCursor(10, 12);     // Start at top-left corner        
      }
    }
    switch(menu){
      case m_off: //running -> display nothing
        if(press != 0){
          press = 0;
          menu = 1;
          localMenu = 1;
        }
        break;
      case m_setup: // Setup menu
        localMenu = 1;
        setup_menu();
        break;
      case m_lamp: // Lampes menu
        lamp_menu();
        break;
      case m_buse: // Buses menu
        buse_menu();
        break;
      case m_brume: // Brume menu
        extra_menu(&brume);
        break;
      case m_bulle: // Bulles menu
        extra_menu(&bulles);
        break;
      case m_time:
        time_menu();
        break;
      case m_save: // Save data to EEPROM
        saveToEEPROM();
        break;
      case m_exit: // exit config
        menu = 0;
        break;
    }
    if(localMenu != 0){
      if(menu == 0){
        localMenu = 0;
        display.clearDisplay();
      }
      display.display();      
    }
    k_sleep(200);
  }
}

void time_task(){

  while (1){
    if(sec >= 59){
      sec = 0;
      if(globalTime.minutes == 59){
        globalTime.minutes = 0;
        if(globalTime.hours == 23){
          globalTime.hours = 0;
        } else{
          globalTime.hours++;
        }
      } else {
        globalTime.minutes++;
      }
    } else {
      sec++;
    }
    k_sleep(1000); // Wait 1 sec
  }
}

void lamp_task(){
  while(1){
    for(int i=0;i<3;i++){
      if(lamps[i].active){
        if(globalTime.hours == lamps[i].onH.hours && lamps[i].onH.minutes >= globalTime.minutes){
          digitalWrite(lampPin[i], 0);
          lampNbr |= 1<<i;
        }else if(globalTime.hours > lamps[i].onH.hours && globalTime.hours < lamps[i].offH.hours){
            digitalWrite(lampPin[i], 0);
            lampNbr |= 1<<i;
        } else if(globalTime.hours == lamps[i].offH.hours){
          if(globalTime.minutes < lamps[i].offH.minutes){
            digitalWrite(lampPin[i], 0);
            lampNbr |= 1<<i;
          } else{
            digitalWrite(lampPin[i], 1);
            lampNbr &= ~(1<<i);
          }
        }
      }
      else{
        digitalWrite(lampPin[i], 1);
      }        
    }
    if(lampNbr == 0){
      digitalWrite(ledFond, 0);
    } else{
      digitalWrite(ledFond, 1);
    }
    k_sleep(10000);
  }
}

void buse_task(){
  uint8_t buseflag = 0;

  while(1){    
    if(globalTime.hours >= buse.startH.hours && globalTime.hours <= buse.stopH.hours && buse.active && buseFlag == 0){
      if(globalTime.minutes == buse.startH.minutes){
        digitalWrite(busePin, 0);
        k_sleep(buse.tOn * 1000);
        digitalWrite(busePin, 1);
        buseFlag = 1;
      }
    }else{
      digitalWrite(busePin, 1);
      if(globalTime.minutes >= buse.startH.minutes+(buse.tOn/60)){
        buseflag = 0;
      }
    }
    k_sleep(10000);
  }
}

void brume_task(){

  while(1){
    if(brume.nbr != 0 && brume.active == 1){
      for(int i=0;i<brume.nbr;i++){
        if(brume.times[i].hours == globalTime.hours){
          if(brume.times[i].minutes >= globalTime.minutes){
            digitalWrite(brumePin, 0);
          }
        }
        if(brume.times[i].minutes + brume.tOn >= 60){
          if((brume.times[i].hours+1) == globalTime.hours){
            if(globalTime.minutes >= ((brume.times[i].minutes + brume.tOn)-60)){
              digitalWrite(brumePin, 1);
            }
          }
        } else{
          if(brume.times[i].hours >= globalTime.hours){
            if(globalTime.minutes >= (brume.times[i].minutes+brume.tOn)){
              digitalWrite(brumePin, 1);
            }
          }
        }
      }
    } else {
      digitalWrite(brumePin, 1);
    }
    k_sleep(10000);
  }
}

void bulles_task(){
  while(1){
    if(bulles.nbr != 0 && bulles.active){
      for(int i=0;i<bulles.nbr;i++){
        if(globalTime.hours == bulles.times[i].hours){
          if(globalTime.minutes == bulles.times[i].minutes){
            digitalWrite(bullesPin, 0);
          }
        }
        if(bulles.times[i].minutes + bulles.tOn >= 60){
          if((bulles.times[i].hours+1) == globalTime.hours){
            if(((bulles.times[i].minutes + bulles.tOn)-60) == globalTime.minutes){
              digitalWrite(bullesPin, 1);
            }
          }
        } else{
          if(bulles.times[i].hours == globalTime.hours){
            if((bulles.times[i].minutes+bulles.tOn) == globalTime.minutes){
              digitalWrite(bullesPin, 1);
            }
          }
        }
      }
    }else {
      digitalWrite(bullesPin, 1);
    }
    k_sleep(10000);
  }
}

// Menu functions

void setup_menu(){
  //Serial.println("Setup Screen");
  nbr_setup = 6;
  
  if(press == p_bt2_s){
    if(setupVal == nbr_setup){
      setupVal = 0;
    } else {
      setupVal++;
    }
    press = p_none;
  }
  strcpy_P(buffer, (char *)pgm_read_word(&(menuName[setupVal])));
  display.write(buffer);
  if (press == p_bt1_s){
    press = p_none;
    if(setupVal+2 == m_time){
      timeSelected = globalTime;
    }
    menu += (setupVal + 1);
    setupVal = 0;
  }
}

void lamp_menu(){
  //Serial.println("Lamp Screen");
  nbr_setup = 2;
  
  switch(subMenu){
    case 0: // Lamp choice
      if(press == p_bt2_s){
        if(setupVal == nbr_setup){
          setupVal = 0;
        } else {
          setupVal++;
        }
        press = p_none;
      }else if (press == p_bt1_s){
        press = p_none;
        choosed = setupVal;
        subMenu++;
      }else if(press == p_bt1_l){
        press = p_none;
        menu = m_setup;
      }
      display_by_buffer(&(lampName[setupVal]));
     break;

    case 1:
      display_by_buffer(&(msg[2]));
      if(lamps[choosed].active){
        display_by_buffer(&(msg[3]));
      }else {
        display_by_buffer(&(msg[4]));
      }
      if(press == p_bt2_s){
        press = p_none;
        lamps[choosed].active = lamps[choosed].active?0:1;
      }else if(press == p_bt1_s){
        press = p_none;
        timeSelected = lamps[choosed].onH;
        subMenu += lamps[choosed].active?1:-1;
      }else if(press == p_bt1_l){
        press = p_none;
        subMenu = 0;
      }
      break;
     
    case 2: // get on hour
      hourSelection();
      display_by_buffer(&(msg[5]));
      if (press == p_bt1_s){
        press = p_none;
        lamps[choosed].onH = timeSelected;
        HorM = 0;
        timeSelected = lamps[choosed].offH;
        subMenu++;
      }else if(press == p_bt1_l){
        press = p_none;
        if(HorM){
          HorM = 0;
        }else{
          subMenu--;
        }
      }
      break;
      
    case 3: // get off hour
      hourSelection();
      display_by_buffer(&(msg[6]));
      if (press == p_bt1_s){
        press = p_none;
        lamps[choosed].offH = timeSelected;
        HorM = 0;
        menu = 1;
        subMenu = 0;
        choosed = 0;
        setupVal = 0;
      }else if(press == p_bt1_l){
        press = p_none;
        if(HorM){
          HorM = 0;
        }else{
          subMenu--;
        }
      }
      break;
  }
}

void extra_menu(EXTRAS *thing){
  
  if(thing->nbr == 0 && subMenu == 0){ // No config-> jump to add ton then redirect to hOn
    subMenu = 3;
    nbr_setup = 59;  
    setupVal = thing->tOn;
  }
  switch(subMenu){
    case 0: // Add or edit
      nbr_setup = 1;
      if(press == p_bt2_s){
        if(setupVal == nbr_setup){
          setupVal = 0;
        } else {
          setupVal++;
        }
        press = p_none;          
      }else if (press == p_bt1_s){
        press = p_none;
        // store edit or add (by preselecting the array id and cheching if under tot nbr)
        subMenu += setupVal+1;
      }
      display_by_buffer(&(extraMenu[setupVal]));
    break;
    
    case 1: // Edit
      nbr_setup = thing->nbr;
      //display_by_buffer(&(msg[7]));
      if(press == p_bt2_s){
        if(setupVal == nbr_setup){
          setupVal = 0;
        } else {
          setupVal++;
        }
        press = p_none;        
      }
      if (press == p_bt1_s){
        press = p_none; 
        if(setupVal == nbr_setup){
          subMenu+=2;
          setupVal = thing->tOn;
        } else{
          choosed = setupVal;          
          subMenu += 3;
          timeSelected = thing->times[choosed];
        }
      }
      if(setupVal == nbr_setup){
          display_by_buffer(&(msg[8]));
          display.print(thing->tOn);
          display_by_buffer(&(msg[10]));
      } else {
          display.print(setupVal+1);
          display_by_buffer(&(msg[11]));
          display.print(thing->times[setupVal].hours);
          display_by_buffer(&(msg[12]));
          display.print(thing->times[setupVal].minutes);
          display_by_buffer(&(msg[13]));
      }
      break;
      
    case 2: // Add
      if(thing->nbr < 10){
        choosed = thing->nbr;
        thing->nbr++;
        subMenu+=3;
        timeSelected = TIME{8,0};   
      } else{
        subMenu += 4;
      }
      break;
      
    case 3: // Time on
      display_by_buffer(&(msg[9]));
      if(press == p_bt2_s || press == p_bt2_r){
        if(setupVal == nbr_setup){
          setupVal = 0;
        } else {
          setupVal++;
        }
        press = p_none;
      }else if (press == p_bt2_l){
        press = p_none;
        setupVal = 0;
      }else if (press == p_bt1_s){
        press = p_none;
        thing->tOn = setupVal;
        subMenu++;
      }else if(press == p_bt1_l){
        press = p_none;
        setupVal = 0;
        menu = m_setup;
      }
      display.print(setupVal);
     break;
     
    case 4: // Active?
      display_by_buffer(&(msg[2]));
      if(thing->active){
        display_by_buffer(&(msg[3]));
      }else {
        display_by_buffer(&(msg[4]));
      }
      if(press == p_bt2_s){
        press = p_none;
        thing->active = thing->active?0:1;
      }else if(press == p_bt1_s){
        press = p_none;
        timeSelected = thing->times[choosed];
        if(thing->active && thing->nbr == 0){
            subMenu = 2;
        }else {
          menu = m_setup;
          subMenu = 0;
          choosed = 0;
          setupVal = 0;
        }
      }else if(press == p_bt1_l){
        press = p_none;
        menu = m_setup;
      }
      break;

    case 5: // get on hour
      hourSelection();
      display_by_buffer(&(msg[16]));
      if (press == p_bt1_s){
        press = p_none;
        thing->times[choosed] = timeSelected;
        HorM = 0;
        menu = m_setup;
        subMenu = 0;
        choosed = 0;
        setupVal = 0;
      }else if(press == p_bt1_l){
        press = p_none;
        if(HorM){
          HorM = 0;
        }else{
          subMenu--;
        }
      }
      break;
      
    case 6:   // Memory full
      display_by_buffer(&(msg[17]));
      if (press == p_bt1_s || press == p_bt2_s){
        press = p_none;
        HorM = 0;
        menu = m_setup;
        subMenu = 0;
        choosed = 0;
        setupVal = 0;
      }      
      break;
  }
}

void buse_menu(){
  nbr_setup = 200;
  
  switch(subMenu){
    case 0:
      display_by_buffer(&(msg[2]));
      if(buse.active){
        display_by_buffer(&(msg[3]));
      }else {
        display_by_buffer(&(msg[4]));
      }
      if(press == p_bt2_s){
        press = p_none;
        buse.active = buse.active?0:1;
      }else if(press == p_bt1_s){
        press = p_none;
        if(buse.active){
          subMenu++;
        }else {
          menu = m_setup;
        }
      }else if(press == p_bt1_l){
        press = p_none;
        menu = m_setup;
      }
      break;

    case 1: // Time on
      display_by_buffer(&(msg[9]));
      if(press == p_bt2_s){
        if(setupVal == nbr_setup){
          setupVal = 0;
        } else {
          setupVal++;
        }
        press = p_none;
      }else if(press == p_bt2_r){
        setupVal+=5;        
        if(setupVal == nbr_setup){
          setupVal = 0;
        }
        press = p_none;
      }else if (press == p_bt1_s){
        press = p_none;
        buse.tOn = setupVal;
        timeSelected = buse.startH;
        subMenu++;
      }else if(press == p_bt1_l){
        press = p_none;
        menu = m_setup;
      }
      display.print(setupVal);
     break;
     
    case 2: // get on hour
      hourSelection();
      display_by_buffer(&(msg[5]));
      if (press == p_bt1_s){
        press = p_none;
        buse.startH = timeSelected;
        timeSelected = buse.stopH;
        HorM = 0;
        subMenu++;
      }else if(press == p_bt1_l){
        press = p_none;
        if(HorM){
          HorM = 0;
        }else{
          subMenu--;
        }
      }
      break;
      
    case 3: // get off hour
      hourSelection();
      display_by_buffer(&(msg[6]));
      if (press == p_bt1_s){
        press = p_none;
        buse.stopH = timeSelected;
        HorM = 0;
        menu = 1;
        subMenu = 0;
        choosed = 0;
        setupVal = 0;
      }else if(press == p_bt1_l){
        press = p_none;
        if(HorM){
          HorM = 0;
        }else{
          subMenu--;
        }
      }
      break;
  }
}

void hourSelection(){
  //display.setCursor(10, 12);
  if(HorM == 0){
    if(press == p_bt2_s || press == p_bt2_r){
      if(timeSelected.hours == 23){
        timeSelected.hours = 0;
      } else {
        timeSelected.hours += 1;
      }
      press = p_none;
    }else if (press == p_bt1_s){
      press = p_none;
      HorM++;
    }
    display.drawLine(10,21,20,21,SSD1306_WHITE);
  } else {
    if(press == p_bt2_s){
      timeSelected.minutes+=1;
      if(timeSelected.minutes > 59){
        timeSelected.minutes = 0;
      }
      press = p_none;
    } else if(press == p_bt2_r){
      timeSelected.minutes+=5;
      if(timeSelected.minutes > 59){
        timeSelected.minutes -= 60;
      }
      press = p_none;
    }
    display.drawLine(40,21,50,21,SSD1306_WHITE);
  }
  if(timeSelected.hours<10)
    display.print('0');
  display.print(timeSelected.hours);
  display.print(F(" : "));
  if(timeSelected.minutes<10)
    display.print('0');
  display.print(timeSelected.minutes);
}

void time_menu(){
  hourSelection();
  if(press == p_bt1_s){
    press = p_none;
    HorM = 0;    
    globalTime = timeSelected;
    RTC.setTime(globalTime.hours, globalTime.minutes, sec);
    sec = 0;
    menu = m_setup;
  }else if(press == p_bt1_l){
    press = p_none;
    if(HorM){
      HorM = 0;
    }else {
      menu = m_setup;
    }
  }
}

void restoreFromEEPROM(){
  unsigned int memAddress = 1;

  // Read lamps
  Serial.println(F("Lamps:"));
  for(uint8_t i=0; i<3; i++){
    EEPROM.get(memAddress, lamps[i]);
    memAddress += sizeof(LAMP);
    Serial.print(i);
    Serial.print(F(": A("));
    Serial.print(lamps[i].active);
    Serial.print(F("), On("));
    Serial.print(lamps[i].onH.hours);
    Serial.print(F(":"));
    Serial.print(lamps[i].onH.minutes);
    Serial.print(F("), Off("));
    Serial.print(lamps[i].offH.hours);
    Serial.print(F(":"));
    Serial.print(lamps[i].offH.minutes);
    Serial.println(F(")"));
    if(lamps[i].active > 1){
      lamps[i].active = 0;
      Serial.println(F("Invalid, disabling..."));
    }
  }
  // Read buses
  EEPROM.get(memAddress, buse);
  memAddress += sizeof(BUSE);
  Serial.println(F("\nBuses:"));
  Serial.print(F("A("));
  Serial.print(buse.active);
  Serial.print(F("), tOn("));
  Serial.print(buse.tOn);
  Serial.print(F("), On("));
  Serial.print(buse.startH.hours);
  Serial.print(F(":"));
  Serial.print(buse.startH.minutes);
  Serial.print(F("), Off("));
  Serial.print(buse.stopH.hours);
  Serial.print(F(":"));
  Serial.print(buse.stopH.minutes);
  Serial.println(F(")"));
  if(buse.active > 1 || buse.tOn > 200){
    buse.active = 0;
    buse.tOn = 0;
    Serial.println(F("Invalid, disabling..."));
  }

  // Read brume
  EEPROM.get(memAddress, brume);
  memAddress += sizeof(EXTRAS);
  Serial.println(F("\nBrume:"));
  Serial.print(F("A("));
  Serial.print(brume.active);
  Serial.print(F("), tOn("));
  Serial.print(brume.tOn);
  Serial.print(F("), Nbr("));
  Serial.print(brume.nbr);
  Serial.println(F(")"));
  if(brume.active > 1 || brume.nbr > 10 || brume.tOn > 200){
    brume.active = 0;
    brume.nbr = 0;
    brume.tOn = 0;
    Serial.println(F("Invalid, disabling..."));
  }
  // Read bulles
  EEPROM.get(memAddress, bulles);
  memAddress += sizeof(EXTRAS);
  Serial.println(F("\nBulles:"));
  Serial.print(F("A("));
  Serial.print(bulles.active);
  Serial.print(F("), tOn("));
  Serial.print(bulles.tOn);
  Serial.print(F("), Nbr("));
  Serial.print(bulles.nbr);
  Serial.println(F(")"));
  if(bulles.active > 1 || bulles.nbr > 10 || bulles.tOn > 200){
    bulles.active = 0;
    bulles.nbr = 0;
    bulles.tOn = 0;
    Serial.println(F("Invalid, disabling..."));
  }
  Serial.print(F("\nTot: "));
  Serial.print(memAddress);
  Serial.println(F(" bytes"));
}

void saveToEEPROM(){
  unsigned int memAddress = 0;
  // Update data status
  EEPROM.update(memAddress, 1);
  memAddress++;

  // Write lamps (4B)
  for(uint8_t i=0; i<3; i++){
    EEPROM.put(memAddress, lamps[i]);
    memAddress += sizeof(LAMP);
  }

  // Write brume
  EEPROM.put(memAddress, buse);
  memAddress += sizeof(BUSE);

  //Write brume
  EEPROM.put(memAddress, brume);
  memAddress += sizeof(EXTRAS);

  //Write bulles
  EEPROM.get(memAddress, bulles);
  
  menu = m_setup;  
}

void draw_logo(const unsigned char logo_bmp[]){
  display.clearDisplay();
  display.drawBitmap(
    20,
    (display.height() - LOGO_HEIGHT) / 2,
    logo_bmp, LOGO_WIDTH, LOGO_HEIGHT, 1);
  display.display();
}

void boot_logo(){
  draw_logo(logo_anim1);
  delay(TRANSITION_TIME);
  draw_logo(logo_anim2);
  delay(TRANSITION_TIME);
  draw_logo(logo_anim3);
  delay(TRANSITION_TIME);
  draw_logo(logo_anim4);
  delay(TRANSITION_TIME);
  draw_logo(logo_anim5);
  delay(TRANSITION_TIME);
  draw_logo(logo_anim6);
  delay(TRANSITION_TIME);
  draw_logo(logo_anim7);
  delay(TRANSITION_TIME);
  draw_logo(logo_full);
  delay(TRANSITION_TIME);

  uint8_t line_width = display.width()-10;
  uint8_t pos1, pos2;
  for(int i = 1; i<=10; i++){
    pos1 = 10 + i*(float(line_width)/10);
    pos2 = 10 +line_width - i*(float(line_width)/10);
    display.drawLine(10, 2, pos1, 2, SSD1306_WHITE);
    display.drawLine(10, 3, pos1, 3, SSD1306_WHITE);
    display.drawLine(10, 4, pos1, 4, SSD1306_WHITE);

    display.drawLine(10+line_width, 27, pos2, 27, SSD1306_WHITE);
    display.drawLine(10+line_width, 28, pos2, 28, SSD1306_WHITE);
    display.drawLine(10+line_width, 29, pos2, 29, SSD1306_WHITE);
    display.display(); // Update screen with each newly-drawn line
    delay(TRANSITION_TIME/2);
  }
}

void display_by_buffer(const char* const* addr){
  strcpy_P(buffer, (char *)pgm_read_word(addr));
  display.write(buffer);
}
