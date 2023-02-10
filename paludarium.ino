/******************************************************************************
 * 
 * Paludarium manager
 * 
 * Brief: Controlls different outputs to activate different devices at
 * certains times of days choosed by user.
 * 
 * Version: 1.1
 * Date: 27.08.2022
 * Author: Leo Vaucher
 * Todo: Allumer même si temps déjà passé
 *  
******************************************************************************/

#include <RTC.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
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

typedef struct TIME{
  uint8_t hours;
  uint8_t minutes;
};

typedef struct LAMP{
  TIME onH;
  TIME offH;
};

typedef struct BUSE{
  uint8_t tOn;
  TIME startH;
  TIME stopH;
};

typedef struct EXTRAS{
  uint8_t nbr;
  uint8_t tOn;
  TIME times[10];
};

//  Prototypes
void draw_logo(const unsigned char logo_bmp[]);
void boot_logo();

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
static DS1307 RTC;

uint8_t menu = 0; // Menu position
uint8_t subMenu = 0; // Sub menu pos
uint8_t HorM = 0; // navigate between hour and min in hourSelection

uint8_t choosed = 0; // device choosed to edit
TIME timeSelected = {0, 0};

int hours = 0;
int minutes = 0;

LAMP lamps[3];
uint8_t lampNbr = 0;
BUSE buse;
uint8_t startBuse = 0;
uint16_t buseCounter = 0;
EXTRAS brume;
EXTRAS bulles;
uint8_t buseFlag = 0;

// DEVICES
uint8_t bt1 = 9; // Enter
uint8_t bt2 = 16;// Next or +
uint8_t lampPin[3] = {7,6,5}; //Bleu/Carré/Nuit
uint8_t ledFond = 8; // à ajouter
uint8_t brumePin = 4;
uint8_t busePin = 3;
uint8_t bullesPin = 2;

void setup() {
  Serial.begin(115200);
  RTC.begin();

  pinMode(bt1, INPUT_PULLUP);
  pinMode(bt2, INPUT_PULLUP);

  pinMode(bullesPin, OUTPUT);
  pinMode(busePin, OUTPUT);
  pinMode(brumePin, OUTPUT);
  pinMode(lampPin[2], OUTPUT);
  pinMode(lampPin[1], OUTPUT);
  pinMode(lampPin[0], OUTPUT);
  pinMode(ledFond, OUTPUT);

  digitalWrite(bullesPin, 1);
  digitalWrite(busePin, 1);
  digitalWrite(brumePin, 1);
  digitalWrite(lampPin[2], 1);
  digitalWrite(lampPin[1], 1);
  digitalWrite(lampPin[0], 1);
  digitalWrite(ledFond, 0);

  // SSD1306_SWITCHCAPVCC = generate display voltage from 3.3V internally
  if(!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println(F("SSD1306 allocation failed"));
    for(;;); // Don't proceed, loop forever
  }
  
  display.clearDisplay();
  display.display();

  if (RTC.isRunning()){
    Serial.println("Clock checked!");
    Serial.print("Time: ");
    Serial.print(RTC.getDay());
    Serial.print("-");
    Serial.print(RTC.getMonth());
    Serial.print("-");
    Serial.print(RTC.getYear());
    Serial.print(" ");
    Serial.print(RTC.getHours());
    Serial.print(":");
    Serial.print(RTC.getMinutes());
    Serial.print(":");
    Serial.print(RTC.getSeconds());
    Serial.print("\n");

    hours = RTC.getHours();
    minutes = RTC.getMinutes();
  } else {
    display.drawRect(0, 0, display.width(), display.height(), SSD1306_WHITE);
    display.setTextSize(1);      // Normal 1:1 pixel scale
    display.setTextColor(SSD1306_WHITE); // Draw white text
    display.setCursor(10, 12);     // Start at top-left corner
    display.cp437(true);         // Use full 256 char 'Code Page 437' font
    display.write("! Erreur horloge !");
    display.display();
    while(1);
  }
  
  boot_logo();
  display.setTextSize(1.2);      // Normal 1:1 pixel scale
  display.setTextColor(SSD1306_WHITE); // Draw white text
  display.setCursor(87, 8);     // Start at top-left corner
  display.cp437(true);         // Use full 256 char 'Code Page 437' font
  display.write("APS");
  display.setCursor(87, 16);
  display.print(hours);
  display.write(":");
  display.print(minutes);
  display.display();

  delay(1000);

  Serial.println("Startup complete!");

  if(EEPROM.read(0x00) == 0){
    menu = 1;
    Serial.println("Not configured, starting config...");
  } else {
    Serial.println("Configured, running config...");
    restoreFromEEPROM();
  }

  display.clearDisplay();
  display.display();

  lamps[1].onH.hours = 8;
  lamps[1].onH.minutes = 0;
  lamps[1].offH.hours = 22;
  lamps[1].offH.minutes = 0;
  buse.tOn = 5;
  buse.startH.hours = 8;
  buse.startH.minutes = 0;
  buse.stopH.hours = 21;
  buse.stopH.minutes = 0;

}

uint8_t btval1 = 0;
uint8_t btval2 = 0;
uint8_t press1 = 0;
uint8_t press2 = 0;

const char* menuName[] = {"Lampe","Buses","Brume","Bulles","Sauver","Sortir"};
const char* lampName[] = {"Bleu","Gros carre","Nuit"};
const char* extraMenu[] = {"Modifier", "Ajouter"};

uint8_t nbr_setup = 3;
uint8_t setupVal = 0;

unsigned long actualTime = millis();

void loop() {
  display.clearDisplay();
  hours = RTC.getHours();
  minutes = RTC.getMinutes();
  
  manageTasks();
  
  manageButtons();

  // Menu
  switch(menu){
    case 0: //running -> display nothing
      display.clearDisplay();
      if(press1 || press2){
        press1 = 0;
        press2 = 0;
        menu = 1;
      }
      break;
    case 1: // Setup menu
      setup_menu();
      break;
    case 2: // Lampes menu
      lamp_menu();
      break;
    case 3: // Buses menu
      buse_menu();
      break;
    case 4: // Brume menu
      extra_menu(&brume);
      break;
    case 5: // Bulles menu
      extra_menu(&bulles);
      break;
    case 6: // Save data to EEPROM
      saveToEEPROM();
      break;
    case 7: // exit config
      menu = 0;
      break;
  }
  
  display.display();
  if(millis()-actualTime > 100){
    actualTime = millis();
  }
  delay(100 - (millis()-actualTime));
  actualTime = millis();
}

void setup_menu(){
  //Serial.println("Setup Screen");
  nbr_setup = 5;
  display.drawRoundRect(0, 0, display.width(), display.height(), 4, SSD1306_WHITE);
  display.setCursor(10, 12);     // Start at top-left corner
  if(press2){
    if(setupVal == nbr_setup){
      setupVal = 0;
    } else {
      setupVal++;
    }
    press2 = 0;
  }
  display.write(menuName[setupVal]);
  if (press1){
    press1 = 0;
    menu += (setupVal + 1);
    setupVal = 0;
  }
}

void lamp_menu(){
  //Serial.println("Lamp Screen");
  nbr_setup = 2;
  display.drawRoundRect(0, 0, display.width(), display.height(), 4, SSD1306_WHITE);
  display.setCursor(10, 12);     // Start at top-left corner
  switch(subMenu){
    case 0: // Lamp choice
      if(press2){
        if(setupVal == nbr_setup){
          setupVal = 0;
        } else {
          setupVal++;
        }
        press2 = 0;
      }
      if (press1){
        press1 = 0;
        choosed = setupVal;
        subMenu++;
      }
      display.write(lampName[setupVal]);
     break;
     
    case 1: // get on hour
      hourSelection();
      display.print(" (On)");
      if (press1){
        press1 = 0;
        lamps[choosed].onH = timeSelected;
        HorM = 0;
        subMenu++;
      }
      break;
      
    case 2: // get off hour
      hourSelection();
      display.print(" (Off)");
      if (press1){
        press1 = 0;
        lamps[choosed].offH = timeSelected;
        HorM = 0;
        menu = 1;
        subMenu = 0;
        choosed = 0;
        setupVal = 0;
      }
      break;
  }
}

void extra_menu(EXTRAS *thing){
  nbr_setup = 1;
  display.drawRoundRect(0, 0, display.width(), display.height(), 4, SSD1306_WHITE);
  display.setCursor(10, 12);     // Start at top-left corner
  if(thing->nbr == 0 && subMenu == 0){ // No config-> jump to add ton then redirect to hOn
    subMenu = 3;
  }
  switch(subMenu){
    case 0: // Add or edit
      if(press2){
        if(setupVal == nbr_setup){
          setupVal = 0;
        } else {
          setupVal++;
        }
        press2 = 0;          
      }
      if (press1){
        press1 = 0;
        // store edit or add (by preselecting the array id and cheching if under tot nbr)
        subMenu += setupVal+1;
      }
      display.write(extraMenu[setupVal]);
    break;
    
    case 1: // Edit
      nbr_setup = thing->nbr;
      display.print("Choose: ");
      if(press2){
        if(setupVal == nbr_setup){
          setupVal = 0;
        } else {
          setupVal++;
        }
        press2 = 0;        
      }
      if (press1){
        press1 = 0;
        if(setupVal == nbr_setup){
          subMenu+=2;
        } else{
          choosed = setupVal;
          subMenu += 3;
          timeSelected = thing->times[choosed];
        }
      }
      if(setupVal == nbr_setup){
          display.write("Ton (");
          display.write(thing->tOn);
          display.write("min)");
      } else {
          display.write(nbr_setup+1);
          display.write(" (");
          display.write(thing->times[nbr_setup].hours);
          display.write("h");
          display.write(thing->times[nbr_setup].minutes);
          display.write(")");
      }
      break;
      
    case 2: // Add
      if(thing->nbr < 10){
        choosed = thing->nbr;
        thing->nbr++;
        subMenu+=2;        
      } else{
        subMenu += 3;
      }
      break;

    case 3: // Ton
      nbr_setup = 59;
      display.print("T on: ");
      if(press2){
        if(setupVal == nbr_setup){
          setupVal = 0;
        } else {
          setupVal++;
        }
        press2 = 0;
      }
      if (press1){
        press1 = 0;
        thing->tOn = setupVal;
        if(thing->nbr == 0){
          subMenu = 2;  // First config -> add new hOn
        } else {
          HorM = 0;
          menu = 1;
          subMenu = 0;
          choosed = 0;
          setupVal = 0;
        }
      }
      display.print(setupVal);
      break;

    case 4: // Hon
      hourSelection();
      display.print(" (hOn)");
      if (press1){
        press1 = 0;
        thing->times[choosed] = timeSelected;
        HorM = 0;
        menu = 1;
        subMenu = 0;
        choosed = 0;
        setupVal = 0;
      }
      break;

    case 5:   // Memory full
      display.print("!Memory full!");
      if (press1 || press2){
        press1 = 0;
        press2 = 0;
        HorM = 0;
        menu = 1;
        subMenu = 0;
        choosed = 0;
        setupVal = 0;
      }      
      break;
  }
}

void buse_menu(){
  nbr_setup = 200;
  display.drawRoundRect(0, 0, display.width(), display.height(), 4, SSD1306_WHITE);
  display.setCursor(10, 12);     // Start at top-left corner
  switch(subMenu){
    case 0: // Time on
      display.print("T on: ");
      if(press2){
        if(setupVal >= nbr_setup){
          setupVal = 0;
        } else {
          setupVal+=5;
        }
        press2 = 0;
      }
      if (press1){
        press1 = 0;
        buse.tOn = setupVal;
        subMenu++;
      }
      display.print(setupVal);
      display.print("s");
     break;
     
    case 1: // get on hour
      hourSelection();
      display.print(" (On)");
      if (press1){
        press1 = 0;
        buse.startH = timeSelected;
        HorM = 0;
        subMenu++;
      }
      break;
      
    case 2: // get off hour
      hourSelection();
      display.print(" (Off)");
      if (press1){
        press1 = 0;
        buse.stopH = timeSelected;
        HorM = 0;
        menu = 1;
        subMenu = 0;
        choosed = 0;
        setupVal = 0;
      }
      break;
  }
}

void hourSelection(){
  display.setCursor(10, 12);
  if(HorM == 0){
    if(press2){
      if(timeSelected.hours == 23){
        timeSelected.hours = 0;
      } else {
        timeSelected.hours++;
      }
      press2 = 0;
    }
    if (press1){
      press1 = 0;
      HorM++;
    }
    display.drawLine(10,21,20,21,SSD1306_WHITE);
  } else {
    if(press2){
      if(timeSelected.minutes == 55){
        timeSelected.minutes = 0;
      } else {
        timeSelected.minutes+=5;
      }
      press2 = 0;
    }
    display.drawLine(40,21,50,21,SSD1306_WHITE);
  }
  if(timeSelected.hours<10)
    display.print('0');
  display.print(timeSelected.hours);
  display.print(" : ");
  if(timeSelected.minutes<10)
    display.print('0');
  display.print(timeSelected.minutes);
}

void restoreFromEEPROM(){
  int memAddress = 1;

  // Read lamps
  for(uint8_t i=0; i<3; i++){
    EEPROM.get(memAddress, lamps[i]);
    memAddress += sizeof(LAMP);    
  }
  
  // Read buses
  EEPROM.get(memAddress, buse);
  memAddress += sizeof(BUSE);

  // Read brume
  EEPROM.get(memAddress, brume);
  memAddress += sizeof(EXTRAS);

  // Read bulles
  EEPROM.get(memAddress, bulles);
}

void saveToEEPROM(){
  int memAddress = 0;
  // Update data status
  EEPROM.update(memAddress, 1);
  memAddress++;

  // Write lamps (4B)
  for(uint8_t i=0; i<3; i++){
    EEPROM.update(memAddress, lamps[i].onH.hours);
    memAddress++;
    EEPROM.update(memAddress, lamps[i].onH.minutes);
    memAddress++;
    EEPROM.update(memAddress, lamps[i].offH.hours);
    memAddress++;
    EEPROM.update(memAddress, lamps[i].offH.minutes);   
    memAddress++; 
  }

  // Write buse
  EEPROM.update(memAddress, buse.tOn);
  memAddress++;
  EEPROM.update(memAddress, buse.startH.hours);
  memAddress++;
  EEPROM.update(memAddress, buse.startH.minutes);
  memAddress++;
  EEPROM.update(memAddress, buse.stopH.hours);
  memAddress++;
  EEPROM.update(memAddress, buse.stopH.minutes);
  memAddress++;

  //Write brume
  EEPROM.update(memAddress, brume.nbr);
  memAddress++;
  EEPROM.update(memAddress, brume.tOn);
  memAddress++;
  for(uint8_t i=0; i<10; i++){
    EEPROM.update(memAddress, brume.times[i].hours);
    memAddress++;
    EEPROM.update(memAddress, brume.times[i].minutes);
    memAddress++;
  }

  //Write bulles
  EEPROM.update(memAddress, bulles.nbr);
  memAddress++;
  EEPROM.update(memAddress, bulles.tOn);
  memAddress++;
  for(uint8_t i=0; i<10; i++){
    EEPROM.update(memAddress, bulles.times[i].hours);
    memAddress++;
    EEPROM.update(memAddress, bulles.times[i].minutes);
    memAddress++;
  }
}

void manageButtons(){
  // Button management
  if(btval1 == 1){
    if(digitalRead(bt1) == 0){
      btval1 = 0;
      press1 = 1;
    }
  } else {
    btval1 = digitalRead(bt1);
  }
  if(btval2 == 1){
    if(digitalRead(bt2) == 0){
      btval2 = 0;
      press2 = 1;
    }
  } else {
    btval2 = digitalRead(bt2);
  }
}

void manageTasks(){
  // Lamps
  for(int i=0;i<3;i++){
    if(lamps[i].onH.hours == hours){
      if(lamps[i].onH.minutes == minutes){
        digitalWrite(lampPin[i], 0);
        lampNbr |= 1<<i;
        //Led fond
      }
    }
    if(lamps[i].offH.hours == hours){
      if(lamps[i].offH.minutes == minutes){
        digitalWrite(lampPin[i], 1);
        lampNbr &= ~(1<<i);
      }
    }
  }
  if(lampNbr == 0){
    digitalWrite(ledFond, 0);
  } else{
    digitalWrite(ledFond, 1);
  }
  // Brume
  if(brume.nbr != 0){
    for(int i=0;i<brume.nbr;i++){
      if(brume.times[i].hours == hours){
        if(brume.times[i].minutes == minutes){
          digitalWrite(brumePin, 0);
        }
      }
      if(brume.times[i].minutes + brume.tOn >= 60){
        if((brume.times[i].hours+1) == hours){
          if(((brume.times[i].minutes + brume.tOn)-60) == minutes){
            digitalWrite(brumePin, 1);
          }
        }
      } else{
        if(brume.times[i].hours == hours){
          if((brume.times[i].minutes+brume.tOn) == minutes){
            digitalWrite(brumePin, 1);
          }
        }
      }
    }
  }

  // Buse
  if(hours >= buse.startH.hours && hours <= buse.stopH.hours){
    if(minutes == buse.startH.minutes && buseFlag == 0){
      startBuse = 1;
      buseFlag = 1;
      digitalWrite(busePin, 0);
    } else if(minutes == buse.startH.minutes+1){
      buseFlag = 0;
    }
    if(startBuse){
      if(buseCounter == buse.tOn*10){
        digitalWrite(busePin, 1);
        buseCounter = 0;     
        startBuse = 0;   
      } else{
        buseCounter++;
      }
    }
  }

  // Bulles
  if(bulles.nbr != 0){
    for(int i=0;i<bulles.nbr;i++){
      if(bulles.times[i].hours == hours){
        if(bulles.times[i].minutes == minutes){
          digitalWrite(bullesPin, 0);
          digitalWrite(ledFond, 1);
        }
      }
      if(bulles.times[i].minutes + bulles.tOn >= 60){
        if((bulles.times[i].hours+1) == hours){
          if(((bulles.times[i].minutes + bulles.tOn)-60) == minutes){
            digitalWrite(bullesPin, 1);
            digitalWrite(ledFond, 0);
          }
        }
      } else{
        if(bulles.times[i].hours == hours){
          if((bulles.times[i].minutes+bulles.tOn) == minutes){
            digitalWrite(bullesPin, 1);
            digitalWrite(ledFond, 0);
          }
        }
      }
    }
  }
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

  int line_width = display.width()-10;
  int pos1, pos2;
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
