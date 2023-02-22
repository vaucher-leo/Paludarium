/*
 *  Project:  Paludarium
 *  Version:  2.1
 *  Author:   Leo Vaucher
 *  Date:     22.02.2023
 *
 *  Brief:    Manage all peripherals of a paludarium
 *            Devices: Lamps, Buses, Brume, Bulles
 *            Extra functionnalities: save/restore data on EEPROM
 *                                    Display & Setup time
 *                                    Go back in setup with long press on S1
 *
 *  Description:
 *            Made with a coop kernel. Thanks to jnd-aau for his awesome work!
 *                Available here -> https://github.com/jdn-aau/krnl
 *            Each device has it's own task to manage when to enable/disable it.
 *            The time is also managed by a task of higher priority and has been
 *              calibrated by measuring time between 2 task loops.
 *            The las is the user input task that manage the different button
 *              presses that have been done by user.(short, long, repeat)
 *            A user interface allow user to easily setup every devic, time and
 *              store everything in EEPROM.
 *            The external RTC allows to keep correct time after a power loss
 *              and is used as a reference for local time. Is also set to user
 *              time when the time is set up in menu.
 *
 *            All of this is designed to work with the following components:
 *              -Arduino nano
 *              -2 buttons (connected to pin & resistor to GND, reverse logic)
 *              -I2C RTC (DS1307)
 *              -I2C Screen (SSD1307 128x32px)
 *              -Relay board for most devices working in reverse logic
 *
 *  Additionnal infos:
 *            Due to the little ram available on the Atmega 328p (2kB), I did a
 *            little optimisation to make the whole program fit in. (the screen
 *            needs a buffer of ~512B and each task has a stack) That implies
 *            that it works like it is and will still work if adding code but
 *            may stop working if some global vars(or local vars in new functions).
 *            If the device keeps rebboting, stay on a black screen(serial errror
 *            SSD1306 allocation failed) or crash randomly a certain times, it is
 *            very likely that there is not enough ram available.
 *            One possible optimization is to change the screen usage to a ASCII
 *            only lib to get rid of the 512B buffer. But you will not be able
 *            to draw fancy things like the boot animation.
 *
 *  TODO:     -Wait for user test
 *            -Write a user manual?
 */

#include <krnl.h>
#include <I2C_RTC.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_SSD1306.h>
#include <EEPROM.h>

#include "logo_anim.h"

#define SCREEN_WIDTH 128     // OLED display width, in pixels
#define SCREEN_HEIGHT 32     // OLED display height, in pixels
#define OLED_RESET -1        // Reset pin # (or -1 if sharing Arduino reset pin)
#define SCREEN_ADDRESS 0x3C  ///< See datasheet for Address; 0x3D for 128x64, 0x3C for 128x32

#define LOGO_HEIGHT 18  // Startup logo dimensions
#define LOGO_WIDTH 64
#define TRANSITION_TIME 100


//  Kernel vars & defs

// stack to be used by task
#define STKSZ1 45
#define STKSZ2 170
char stak1[STKSZ1], stak2[STKSZ2], stak3[STKSZ1], stak4[STKSZ1], stak5[STKSZ1], stak6[STKSZ1], stak7[STKSZ1];

struct k_t *pt1,
  *pt2,
  *pt3,
  *pt4,
  *pt5,
  *pt6,
  *pt7;  // pointer to task descriptor


// Enums & Structs

//  Types of button presses
enum btn_press {
  p_none = 0,
  p_bt1_s,  // Short
  p_bt1_l,  // Long
  p_bt1_r,  // Repeat
  p_bt2_s,
  p_bt2_l,
  p_bt2_r
};

// All menus available
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

// Button type
typedef struct BUTTON {
  uint8_t val : 4;   // Button value
  uint8_t mode : 4;  // Button mode (short, long, repeat)
  uint8_t time;      // Time counter
};

// Time type
typedef struct TIME {
  uint8_t hours;
  uint8_t minutes;
};

//  Lamp type
typedef struct LAMP {
  TIME onH;
  TIME offH;
  uint8_t active;
};

//  Buse type
typedef struct BUSE {
  uint8_t tOn;
  TIME startH;
  TIME stopH;
  uint8_t active;
};

// Exta type (Brume&Bulles)
typedef struct EXTRAS {
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
void display_by_buffer(const char *const *addr);


// Devices initialization

static Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
static DS1307 RTC;


// Global vars

uint8_t menu = 0;     // Menu position
uint8_t subMenu = 0;  // Sub menu pos
uint8_t HorM = 0;     // Navigate between hour and min in hourSelection

uint8_t choosed = 0;           // Device choosed to edit
TIME timeSelected = { 0, 0 };  // Time selection var

TIME globalTime = { 0, 0 };  // Actual time
uint8_t sec = 0;

// Peripherals
LAMP lamps[3];
uint8_t lampNbr = 0;
BUSE buse;
EXTRAS brume;
EXTRAS bulles;
uint8_t buseFlag = 0;

// Buttons
BUTTON bt1 = { 0, 0, 0 };
BUTTON bt2 = { 0, 0, 0 };
uint8_t press = 0;

//  Text messages stored in flash

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
const char err_hor_msg[] PROGMEM = "!Err hor!";
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

static const char *const menuName[7] PROGMEM = { menu_0, menu_1, menu_2, menu_3, menu_4, menu_5, menu_6 };
static const char *const lampName[3] PROGMEM = { lamp_0, lamp_1, lamp_2 };
static const char *const extraMenu[2] PROGMEM = { extra_0, extra_1 };

char buffer[11];  // Text buffer

uint8_t nbr_setup = 3;
uint8_t setupVal = 0;

// Devices
uint8_t bt1_pin = 9;
uint8_t bt2_pin = 16;
uint8_t lampPin[3] = { 7, 6, 5 };
uint8_t ledFond = 8;
uint8_t brumePin = 4;
uint8_t busePin = 3;
uint8_t bullesPin = 2;

// Main functions
void setup() {

  uint8_t k_err;

  Serial.begin(115200);
  RTC.begin();

  // GPIO
  pinMode(bt1_pin, INPUT_PULLUP);
  pinMode(bt2_pin, INPUT_PULLUP);
  pinMode(lampPin[0], OUTPUT);
  pinMode(lampPin[1], OUTPUT);
  pinMode(lampPin[2], OUTPUT);
  pinMode(ledFond, OUTPUT);
  pinMode(brumePin, OUTPUT);
  pinMode(busePin, OUTPUT);
  pinMode(bullesPin, OUTPUT);
  pinMode(10, OUTPUT);

  // Screen
  // SSD1306_SWITCHCAPVCC = generate display voltage from 3.3V internally
  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println(F("SSD1306 allocation failed"));
    for (;;)
      ;  // Don't proceed, loop forever
  }

  display.clearDisplay();
  display.display();

  // Get time from RTC if available
  if (RTC.isRunning()) {
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
    display.setTextSize(1);               // Normal 1:1 pixel scale
    display.setTextColor(SSD1306_WHITE);  // Draw white text
    display.setCursor(10, 12);            // Start at top-left corner
    display.cp437(true);                  // Use full 256 char 'Code Page 437' font
    display_by_buffer(&(msg[15]));
    display.display();
    while (1)
      ;
  }

  // Display boot logo animation
  boot_logo();
  display.setTextSize(1.2);             // Normal 1:1 pixel scale
  display.setTextColor(SSD1306_WHITE);  // Draw white text
  display.setCursor(87, 8);             // Start at top-left corner
  display.cp437(true);                  // Use full 256 char 'Code Page 437' font
  display_by_buffer(&(msg[0]));
  display.setCursor(87, 16);
  display.print(globalTime.hours);
  display_by_buffer(&(msg[1]));
  display.print(globalTime.minutes);
  display.display();

  // Init kernel
  k_init(7, 0, 0);

  delay(1000);

  // Setup basic devices by user choice
  lamps[1].onH.hours = 8;
  lamps[1].onH.minutes = 0;
  lamps[1].offH.hours = 22;
  lamps[1].offH.minutes = 0;
  buse.tOn = 5;
  buse.startH.hours = 8;
  buse.startH.minutes = 0;
  buse.stopH.hours = 21;
  buse.stopH.minutes = 0;

  // Resore config from EEPROM if available
  if (EEPROM.read(0x00) != 2) {
    menu = m_setup;
    Serial.println(F("Not configured, starting config..."));
  } else {
    Serial.println(F("Configured, running config..."));
    restoreFromEEPROM();
    menu = m_off;
  }

  Serial.println(F("Startup complete!"));

  display.clearDisplay();
  display.display();

  // Kernel tasks

  pt1 = k_crt_task(menu_task, 6, stak2, STKSZ2);
  pt2 = k_crt_task(input_task, 4, stak1, STKSZ1);
  pt3 = k_crt_task(time_task, 3, stak3, STKSZ1);
  pt4 = k_crt_task(lamp_task, 5, stak4, STKSZ1);
  pt5 = k_crt_task(buse_task, 5, stak5, STKSZ1);
  pt6 = k_crt_task(brume_task, 5, stak6, STKSZ1);
  pt7 = k_crt_task(bulles_task, 5, stak7, STKSZ1);

  k_err = k_start();  // Start kernel

  Serial.print(F("Oh shit, kernel error!"));
  Serial.println(k_err);
  while (1)
    ;
}

void loop() {}  // Never used


// Tasks functions

//  Input task
//  Manage user input by determining different
//  button presses.
void input_task() {

  while (1) {

    // Button1 management

    if (bt1.val == 0) {
      // Button released
      if (digitalRead(bt1_pin) == 0) {
        bt1.val = 1;
      }
    } else {
      if (digitalRead(bt1_pin) == 1) {
        // Button released
        if (bt1.mode == 0) {
          // Short press
          press = p_bt1_s;
        } else if (bt1.mode == 1) {
          // Long press
          press = p_bt1_l;
        }
        digitalWrite(13, 0);  // Switch off led
        bt1.mode = 0;         // Reset button values
        bt1.time = 0;
        bt1.val = 0;
      } else {
        // Button pressed
        if (bt1.mode == 0) {
          // Short time
          if (bt1.time == 8) {
            // Change to long press after a certain time
            bt1.mode = 1;
            digitalWrite(13, 1);  // Switch on led to indicate long press
          }
        } else if (bt1.mode == 1) {
          // Long time
          if (bt1.time == 20) {
            // Change to repeat mode after a certain time
            bt1.mode = 3;
            digitalWrite(13, 0);  // Switch off led
          }
        } else {
          if (bt1.time >= 5) {
            // Repeat press after a certain time
            bt1.time = 0;
            press = p_bt1_r;
          }
        }
        bt1.time++;  // Incement button time
      }
    }

    // Button 2 management

    if (bt2.val == 0) {
      if (digitalRead(bt2_pin) == 0) {
        bt2.val = 1;
      }
    } else {
      if (digitalRead(bt2_pin) == 1) {
        if (bt2.mode == 0) {
          press = p_bt2_s;
        } else if (bt2.mode == 1) {
          press = p_bt2_l;
        }
        digitalWrite(13, 0);
        bt2.mode = 0;
        bt2.time = 0;
        bt2.val = 0;
      } else {
        if (bt2.mode == 0) {
          if (bt2.time == 8) {
            bt2.mode = 1;
            digitalWrite(13, 1);
          }
        } else if (bt2.mode == 1) {
          if (bt2.time == 20) {
            bt2.mode = 3;
            digitalWrite(13, 0);
          }
        } else {
          if (bt2.time >= 5) {
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

//  Menu task
//  Manage menu on screen and devices setup
//  with button presses
void menu_task() {

  uint8_t localMenu = 0;
  display.clearDisplay();

  while (1) {
    if (localMenu != 0) {
      display.clearDisplay();
      if (menu != 0) {
        // Draw outer rounded rectangle when screen is on
        display.drawRoundRect(0, 0, display.width(), display.height(), 4, SSD1306_WHITE);
        display.setCursor(10, 12);  //Text cursor on middle left
      }
    }
    switch (menu) {
      case m_off:  //running -> display nothing
        if (press != 0) {
          press = 0;
          menu = 1;
          localMenu = 1;
        }
        break;
      case m_setup:  // Setup menu
        localMenu = 1;
        setup_menu();
        break;
      case m_lamp:  // Lamps menu
        lamp_menu();
        break;
      case m_buse:  // Buses menu
        buse_menu();
        break;
      case m_brume:  // Brume menu
        extra_menu(&brume);
        break;
      case m_bulle:  // Bulles menu
        extra_menu(&bulles);
        break;
      case m_time:
        time_menu();
        break;
      case m_save:  // Save data to EEPROM
        saveToEEPROM();
        break;
      case m_exit:  // exit config
        menu = 0;
        break;
    }

    // Clear screen only once when exit
    if (localMenu != 0) {
      if (menu == 0) {
        localMenu = 0;
        display.clearDisplay();
      }
      display.display();
    }
    k_sleep(200);
  }
}

//  Time task
//  Keep track of time
//  Reset itself to RTC when too much diff
void time_task() {

  while (1) {
    if (sec >= 59) {
      // Time increment
      sec = 0;
      if (globalTime.minutes == 59) {
        globalTime.minutes = 0;
        if (globalTime.hours == 23) {
          globalTime.hours = 0;
        } else {
          globalTime.hours++;
        }
      } else {
        globalTime.minutes++;
      }
    } else {
      sec++;
    }
    // Reset to RCT if needed (more than 1min diff)
    if (RTC.getMinutes() != globalTime.minutes) {
      if (RTC.getMinutes() - globalTime.minutes > 1 || globalTime.minutes - RTC.getMinutes() > 1) {
        globalTime.minutes = RTC.getMinutes();
      }
    }

    // Used to measure time for better precision
    //digitalWrite(10, digitalRead(10)==1?0:1);

    k_sleep(977);  // Wait 1 sec (measured)
  }
}

//  Lamp task
//  Manage lamps states by enabling/disabling outputs
//  Depending on setup
//  Setup:  start time & stop time, enabled from start to stop time
void lamp_task() {
  while (1) {
    for (int i = 0; i < 3; i++) {
      // For each lamp
      if (lamps[i].active) {
        // When lamp enabled, check if it needs to be disabled
        if (globalTime.hours == lamps[i].onH.hours && lamps[i].onH.minutes >= globalTime.minutes) {
          digitalWrite(lampPin[i], 0);
          lampNbr |= 1 << i;
        } else if (globalTime.hours > lamps[i].onH.hours && globalTime.hours < lamps[i].offH.hours) {
          digitalWrite(lampPin[i], 0);
          lampNbr |= 1 << i;
        } else if (globalTime.hours == lamps[i].offH.hours) {
          if (globalTime.minutes < lamps[i].offH.minutes) {
            digitalWrite(lampPin[i], 0);
            lampNbr |= 1 << i;
          } else {
            digitalWrite(lampPin[i], 1);
            lampNbr &= ~(1 << i);
          }
        }
      } else {
        // Disable lamp
        digitalWrite(lampPin[i], 1);
      }
    }

    // Enable "ledFond" when at least one other lamp is on
    if (lampNbr == 0) {
      digitalWrite(ledFond, 0);
    } else {
      digitalWrite(ledFond, 1);
    }

    k_sleep(10000);
  }
}

//  Buse task
//  Manage buse state by user setup
//  Setup:  start time & stop time, enable buse for tOn sec
//          each hour between start & stop time
void buse_task() {
  uint8_t buseflag = 0;

  while (1) {
    if (globalTime.hours >= buse.startH.hours && globalTime.hours <= buse.stopH.hours && buse.active && buseFlag == 0) {
      // Time to enable (hour)
      if (globalTime.minutes == buse.startH.minutes) {
        // Time to enable (min)
        digitalWrite(busePin, 0);  // Enable
        k_sleep(buse.tOn * 1000);  // Wait tOn sec
        digitalWrite(busePin, 1);  // Disable
        buseFlag = 1;
      }
    } else {
      // Make sure it is disabled
      digitalWrite(busePin, 1);
      if (globalTime.minutes >= buse.startH.minutes + (buse.tOn / 60)) {
        // Flag to not enable it multiple times in the same min
        buseflag = 0;
      }
    }
    k_sleep(10000);
  }
}

//  Brume task
//  Manage brume state by user setup
//  Setup:  Array of times to enable.
//          For each time defined, enable for tOn min.
void brume_task() {

  while (1) {
    if (brume.nbr != 0 && brume.active == 1) {
      // If device enabled
      for (int i = 0; i < brume.nbr; i++) {
        if (brume.times[i].hours == globalTime.hours) {
          if (brume.times[i].minutes >= globalTime.minutes) {
            // When time, enable it
            digitalWrite(brumePin, 0);
          }
        }
        if (brume.times[i].minutes + brume.tOn >= 60) {
          if ((brume.times[i].hours + 1) == globalTime.hours) {
            if (globalTime.minutes >= ((brume.times[i].minutes + brume.tOn) - 60)) {
              // Disable at the end of time
              digitalWrite(brumePin, 1);
            }
          }
        } else {
          if (brume.times[i].hours >= globalTime.hours) {
            if (globalTime.minutes >= (brume.times[i].minutes + brume.tOn)) {
              // Disable at the end of time
              digitalWrite(brumePin, 1);
            }
          }
        }
      }
    } else {
      // Disabled
      digitalWrite(brumePin, 1);
    }

    k_sleep(10000);
  }
}

//  Bules task
//  Manage bulles by user setup (same as brume)
//  Setup:  Same as brume
void bulles_task() {
  while (1) {
    if (bulles.nbr != 0 && bulles.active) {
      // If device enabled
      for (int i = 0; i < bulles.nbr; i++) {
        if (globalTime.hours == bulles.times[i].hours) {
          if (globalTime.minutes == bulles.times[i].minutes) {
            // When time, enable it
            digitalWrite(bullesPin, 0);
          }
        }
        if (bulles.times[i].minutes + bulles.tOn >= 60) {
          if ((bulles.times[i].hours + 1) == globalTime.hours) {
            if (((bulles.times[i].minutes + bulles.tOn) - 60) == globalTime.minutes) {
              // Disable at the end of time
              digitalWrite(bullesPin, 1);
            }
          }
        } else {
          if (bulles.times[i].hours == globalTime.hours) {
            if ((bulles.times[i].minutes + bulles.tOn) == globalTime.minutes) {
              // Disable at the end of time
              digitalWrite(bullesPin, 1);
            }
          }
        }
      }
    } else {
      // Disabled
      digitalWrite(bullesPin, 1);
    }

    k_sleep(10000);
  }
}

// Menu functions

//  Setup menu
//  Display all configurable options (menuName)
//  Scroll through selection with button2
//  When button1 is pressed, change to selected menu
void setup_menu() {
  //Serial.println("Setup Screen");
  nbr_setup = 6;

  if (press == p_bt2_s) {
    // Next menu
    if (setupVal == nbr_setup) {
      setupVal = 0;
    } else {
      setupVal++;
    }
    press = p_none;
  }

  // Print menu name on screen
  strcpy_P(buffer, (char *)pgm_read_word(&(menuName[setupVal])));
  display.write(buffer);

  if (press == p_bt1_s) {
    // Change to selected menu
    press = p_none;
    if (setupVal + 2 == m_time) {
      timeSelected = globalTime;
    }
    menu += (setupVal + 1);
    setupVal = 0;
  }
}

// Lamp menu
// Setup lamp config with user input
// Setup order: lamp choice
//              enabled?
//              on time
//              off time
void lamp_menu() {
  //Serial.println("Lamp Screen");
  nbr_setup = 2;

  switch (subMenu) {
    case 0:  // Lamp choice
      if (press == p_bt2_s) {
        if (setupVal == nbr_setup) {
          setupVal = 0;
        } else {
          setupVal++;
        }
        press = p_none;
      } else if (press == p_bt1_s) {
        press = p_none;
        choosed = setupVal;
        subMenu++;
      } else if (press == p_bt1_l) {
        press = p_none;
        menu = m_setup;
      }
      display_by_buffer(&(lampName[setupVal]));
      break;

    case 1:  // Enabled?
      display_by_buffer(&(msg[2]));
      if (lamps[choosed].active) {
        display_by_buffer(&(msg[3]));
      } else {
        display_by_buffer(&(msg[4]));
      }
      if (press == p_bt2_s) {
        press = p_none;
        lamps[choosed].active = lamps[choosed].active ? 0 : 1;
      } else if (press == p_bt1_s) {
        press = p_none;
        timeSelected = lamps[choosed].onH;
        subMenu += lamps[choosed].active ? 1 : -1;
      } else if (press == p_bt1_l) {
        press = p_none;
        subMenu = 0;
      }
      break;

    case 2:  // get on hour
      hourSelection();
      display_by_buffer(&(msg[5]));
      if (press == p_bt1_s) {
        press = p_none;
        lamps[choosed].onH = timeSelected;
        HorM = 0;
        timeSelected = lamps[choosed].offH;
        subMenu++;
      } else if (press == p_bt1_l) {
        press = p_none;
        if (HorM) {
          HorM = 0;
        } else {
          subMenu--;
        }
      }
      break;

    case 3:  // get off hour
      hourSelection();
      display_by_buffer(&(msg[6]));
      if (press == p_bt1_s) {
        press = p_none;
        lamps[choosed].offH = timeSelected;
        HorM = 0;
        menu = 1;
        subMenu = 0;
        choosed = 0;
        setupVal = 0;
      } else if (press == p_bt1_l) {
        press = p_none;
        if (HorM) {
          HorM = 0;
        } else {
          subMenu--;
        }
      }
      break;
  }
}

// Extra menu
// Setup extra device with user input
// Setup order: Add or edit (first time direct to add)
//              Choose time to edit (edit)
//              Time on (edit tOn or first time)
//              Active?
//              On hour
// When choosed add and number of times saved is at max
// display an error message
void extra_menu(EXTRAS *thing) {

  if (thing->nbr == 0 && subMenu == 0) {  // No config-> jump to add ton then redirect to hOn
    subMenu = 3;
    nbr_setup = 59;
    setupVal = thing->tOn;
  }
  switch (subMenu) {
    case 0:  // Add or edit
      nbr_setup = 1;
      if (press == p_bt2_s) {
        if (setupVal == nbr_setup) {
          setupVal = 0;
        } else {
          setupVal++;
        }
        press = p_none;
      } else if (press == p_bt1_s) {
        press = p_none;
        // store edit or add (by preselecting the array id and cheching if under tot nbr)
        subMenu += setupVal + 1;
      }
      display_by_buffer(&(extraMenu[setupVal]));
      break;

    case 1:  // Edit
      nbr_setup = thing->nbr;
      //display_by_buffer(&(msg[7]));
      if (press == p_bt2_s) {
        if (setupVal == nbr_setup) {
          setupVal = 0;
        } else {
          setupVal++;
        }
        press = p_none;
      }
      if (press == p_bt1_s) {
        press = p_none;
        if (setupVal == nbr_setup) {
          subMenu += 2;
          setupVal = thing->tOn;
        } else {
          choosed = setupVal;
          subMenu += 3;
          timeSelected = thing->times[choosed];
        }
      }
      if (setupVal == nbr_setup) {
        display_by_buffer(&(msg[8]));
        display.print(thing->tOn);
        display_by_buffer(&(msg[10]));
      } else {
        display.print(setupVal + 1);
        display_by_buffer(&(msg[11]));
        display.print(thing->times[setupVal].hours);
        display_by_buffer(&(msg[12]));
        display.print(thing->times[setupVal].minutes);
        display_by_buffer(&(msg[13]));
      }
      break;

    case 2:  // Add
      if (thing->nbr < 10) {
        choosed = thing->nbr;
        thing->nbr++;
        subMenu += 3;
        timeSelected = TIME{ 8, 0 };
      } else {
        subMenu += 4;
      }
      break;

    case 3:  // Time on
      display_by_buffer(&(msg[9]));
      if (press == p_bt2_s || press == p_bt2_r) {
        if (setupVal == nbr_setup) {
          setupVal = 0;
        } else {
          setupVal++;
        }
        press = p_none;
      } else if (press == p_bt2_l) {
        press = p_none;
        setupVal = 0;
      } else if (press == p_bt1_s) {
        press = p_none;
        thing->tOn = setupVal;
        subMenu++;
      } else if (press == p_bt1_l) {
        press = p_none;
        setupVal = 0;
        menu = m_setup;
      }
      display.print(setupVal);
      break;

    case 4:  // Active?
      display_by_buffer(&(msg[2]));
      if (thing->active) {
        display_by_buffer(&(msg[3]));
      } else {
        display_by_buffer(&(msg[4]));
      }
      if (press == p_bt2_s) {
        press = p_none;
        thing->active = thing->active ? 0 : 1;
      } else if (press == p_bt1_s) {
        press = p_none;
        timeSelected = thing->times[choosed];
        if (thing->active && thing->nbr == 0) {
          subMenu = 2;
        } else {
          menu = m_setup;
          subMenu = 0;
          choosed = 0;
          setupVal = 0;
        }
      } else if (press == p_bt1_l) {
        press = p_none;
        menu = m_setup;
      }
      break;

    case 5:  // get on hour
      hourSelection();
      display_by_buffer(&(msg[16]));
      if (press == p_bt1_s) {
        press = p_none;
        thing->times[choosed] = timeSelected;
        HorM = 0;
        menu = m_setup;
        subMenu = 0;
        choosed = 0;
        setupVal = 0;
      } else if (press == p_bt1_l) {
        press = p_none;
        if (HorM) {
          HorM = 0;
        } else {
          subMenu--;
        }
      }
      break;

    case 6:  // Memory full
      display_by_buffer(&(msg[17]));
      if (press == p_bt1_s || press == p_bt2_s) {
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

// Buse menu
// Setup buse by user input
// Setup order: Active?
//              Time on
//              Start hour
//              Stop hour
void buse_menu() {
  nbr_setup = 200;

  switch (subMenu) {
    case 0:  // Active?
      display_by_buffer(&(msg[2]));
      if (buse.active) {
        display_by_buffer(&(msg[3]));
      } else {
        display_by_buffer(&(msg[4]));
      }
      if (press == p_bt2_s) {
        press = p_none;
        buse.active = buse.active ? 0 : 1;
      } else if (press == p_bt1_s) {
        press = p_none;
        if (buse.active) {
          subMenu++;
        } else {
          menu = m_setup;
        }
      } else if (press == p_bt1_l) {
        press = p_none;
        menu = m_setup;
      }
      break;

    case 1:  // Time on
      display_by_buffer(&(msg[9]));
      if (press == p_bt2_s) {
        if (setupVal == nbr_setup) {
          setupVal = 0;
        } else {
          setupVal++;
        }
        press = p_none;
      } else if (press == p_bt2_r) {
        setupVal += 5;
        if (setupVal == nbr_setup) {
          setupVal = 0;
        }
        press = p_none;
      } else if (press == p_bt1_s) {
        press = p_none;
        buse.tOn = setupVal;
        timeSelected = buse.startH;
        subMenu++;
      } else if (press == p_bt1_l) {
        press = p_none;
        menu = m_setup;
      }
      display.print(setupVal);
      break;

    case 2:  // get on hour
      hourSelection();
      display_by_buffer(&(msg[5]));
      if (press == p_bt1_s) {
        press = p_none;
        buse.startH = timeSelected;
        timeSelected = buse.stopH;
        HorM = 0;
        subMenu++;
      } else if (press == p_bt1_l) {
        press = p_none;
        if (HorM) {
          HorM = 0;
        } else {
          subMenu--;
        }
      }
      break;

    case 3:  // get off hour
      hourSelection();
      display_by_buffer(&(msg[6]));
      if (press == p_bt1_s) {
        press = p_none;
        buse.stopH = timeSelected;
        HorM = 0;
        menu = 1;
        subMenu = 0;
        choosed = 0;
        setupVal = 0;
      } else if (press == p_bt1_l) {
        press = p_none;
        if (HorM) {
          HorM = 0;
        } else {
          subMenu--;
        }
      }
      break;
  }
}

// Hour selection
// Function to select a time in hour - min
// Uses HorM to change between hour and minutes and
// timeSelected to store the time that is ...selected :)
//
// Manage itself the buttons press and displays the values on the screen
void hourSelection() {
  //display.setCursor(10, 12);
  if (HorM == 0) {
    // Hour
    if (press == p_bt2_s || press == p_bt2_r) {
      // Next hour
      if (timeSelected.hours == 23) {
        timeSelected.hours = 0;
      } else {
        timeSelected.hours += 1;
      }
      press = p_none;
    } else if (press == p_bt1_s) {
      // Validate hour, change to min
      press = p_none;
      HorM++;
    }
    display.drawLine(10, 21, 20, 21, SSD1306_WHITE);
  } else {
    // Min
    if (press == p_bt2_s) {
      // next min
      timeSelected.minutes += 1;
      if (timeSelected.minutes > 59) {
        timeSelected.minutes = 0;
      }
      press = p_none;
    } else if (press == p_bt2_r) {
      // Speed increment when longlong press
      timeSelected.minutes += 5;
      if (timeSelected.minutes > 59) {
        timeSelected.minutes -= 60;
      }
      press = p_none;
    }
    // Validation press by bt1 needs to be managed by calling function
    // so it can do whatever it wants next
    display.drawLine(40, 21, 50, 21, SSD1306_WHITE);
  }
  // Screen formatting
  if (timeSelected.hours < 10)
    display.print('0');
  display.print(timeSelected.hours);
  display.print(F(" : "));
  if (timeSelected.minutes < 10)
    display.print('0');
  display.print(timeSelected.minutes);
}

// Time menu
// Setup actual time with user input
// Change local time AND RTC time.
// When time is validated (press1 on minutes selection),
// the seconds are set to 0.
void time_menu() {
  hourSelection();
  if (press == p_bt1_s) {
    // Time validated, store it
    press = p_none;
    HorM = 0;
    globalTime = timeSelected;
    RTC.setTime(globalTime.hours, globalTime.minutes, sec);
    sec = 0;
    menu = m_setup;
  } else if (press == p_bt1_l) {
    // Long press -> back 1 step
    press = p_none;
    if (HorM) {
      HorM = 0;
    } else {
      menu = m_setup;
    }
  }
}

// Restore from EEPROM
// Get all config thathave been stored in EEPROM
// and load it in working vars.
// Prints every readed values to serial to be able to check everything.
// Check some values to see if they are valid. If not, disable the device.
void restoreFromEEPROM() {
  unsigned int memAddress = 1;

  // Read lamps
  Serial.println(F("Lamps:"));
  for (uint8_t i = 0; i < 3; i++) {
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
    if (lamps[i].active > 1) {
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
  if (buse.active > 1 || buse.tOn > 200) {
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
  if (brume.active > 1 || brume.nbr > 10 || brume.tOn > 200) {
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
  if (bulles.active > 1 || bulles.nbr > 10 || bulles.tOn > 200) {
    bulles.active = 0;
    bulles.nbr = 0;
    bulles.tOn = 0;
    Serial.println(F("Invalid, disabling..."));
  }
  Serial.print(F("\nTot: "));
  Serial.print(memAddress);
  Serial.println(F(" bytes"));
}

// Save to EEPROM
// Save working config to EEPROM to be able to restopre it on power on.
// Just writes every var on it's location in EEPROM and go back to setup menu
void saveToEEPROM() {
  unsigned int memAddress = 0;
  // Update data status
  EEPROM.update(memAddress, 2);
  memAddress++;

  // Write lamps (4B)
  for (uint8_t i = 0; i < 3; i++) {
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

// Draw logo
// Functon used to draw the boot logo animation.
void draw_logo(const unsigned char logo_bmp[]) {
  display.clearDisplay();
  display.drawBitmap(
    20,
    (display.height() - LOGO_HEIGHT) / 2,
    logo_bmp, LOGO_WIDTH, LOGO_HEIGHT, 1);
  display.display();
}

// Boot logo
// Boot logo animation sequence
void boot_logo() {
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

  // Animate 2 lines (up & down), one from L->R and the other R->L
  uint8_t line_width = display.width() - 10;
  uint8_t pos1, pos2;
  for (int i = 1; i <= 10; i++) {
    pos1 = 10 + i * (float(line_width) / 10);
    pos2 = 10 + line_width - i * (float(line_width) / 10);
    display.drawLine(10, 2, pos1, 2, SSD1306_WHITE);
    display.drawLine(10, 3, pos1, 3, SSD1306_WHITE);
    display.drawLine(10, 4, pos1, 4, SSD1306_WHITE);

    display.drawLine(10 + line_width, 27, pos2, 27, SSD1306_WHITE);
    display.drawLine(10 + line_width, 28, pos2, 28, SSD1306_WHITE);
    display.drawLine(10 + line_width, 29, pos2, 29, SSD1306_WHITE);
    display.display();  // Update screen with each newly-drawn line
    delay(TRANSITION_TIME / 2);
  }
}

// Display by Buffer
// Used to diplay any string stored in PROGMEM on screen by copying it
// into a buffer first
void display_by_buffer(const char *const *addr) {
  strcpy_P(buffer, (char *)pgm_read_word(addr));
  display.write(buffer);
}

// END