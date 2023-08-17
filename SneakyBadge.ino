/*

Copyright (C) 2023 by Gigs gigbadge@gmail.com

Author: Gigs
License: CC BY-NC 4.0 https://creativecommons.org/licenses/by-nc/4.0/

For commercial use please seek permission at the above email.  If it's not a wholesale copy of what I've made, I'd likely say yes.  I just don't want to compete with my own product being sold at hackerboxes.

Build notes:  Built for Earle Philhower's RP2040 board library

Preferences-> board manager URL https://github.com/earlephilhower/arduino-pico/releases/download/global/package_rp2040_index.json

Lib prereqs:  ss_oled, BitBang_I2C

**You must patch BitBang_I2C.cpp:**

After line 586 which starts out:
#if defined(TEENSYDUINO) || defined(ARDUINO_ARCH_MBED) || defined( __AVR__ ) 

Append || defined(ARDUINO_ARCH_RP2040) on the end of the line 586

then Insert
 
#if defined(ARDUINO_ARCH_RP2040)
       pWire->setSDA((int)pI2C->iSDA);
       pWire->setSCL((int)pI2C->iSCL);
#endif
 
After line 586

Philhower's RP2040 board library doesn't 
provide a two parameter initialization for the I2C Wire

It should look like this when you are done:
#if !defined( _LINUX_ ) && !defined( __AVR_ATtiny85__ )
#if defined(TEENSYDUINO) || defined(ARDUINO_ARCH_MBED) || defined( __AVR__ ) || defined( NRF52 ) || defined ( ARDUINO_ARCH_NRF52840 ) || defined(ARDUINO_ARCH_NRF52) || defined(ARDUINO_ARCH_SAM) || defined(ARDUINO_ARCH_RP2040) 
#if defined(ARDUINO_ARCH_RP2040)
       pWire->setSDA((int)pI2C->iSDA);
       pWire->setSCL((int)pI2C->iSCL);
#endif
#ifdef ARDUINO_ARCH_MBED 

You need the LittleFS upload tool from Philhower as well.  
In the data/ directory you need a dictionary, one word per line, all caps, sorted alphabetically, named dict.txt

*/

#include <ctype.h>
#include <ss_oled.h> //https://github.com/bitbap20
#include <EEPROM.h>
#include "LittleFS.h" 


#define TOP_LED_PIN 20
#define BOTTOM_LED_PIN 21
#define BUTT_RIGHT 28
#define BUTT_LEFT 27
#define BUTT_DOWN 26
#define BUTT_UP 22

#define DEBUG false


//OLED
#define SDA_PIN 0
#define SCL_PIN 1      

#define RESET_PIN -1 // Set this to -1 to disable the GPIO pin number connected to the reset line of your display
#define OLED_ADDR_LEFT 0x3c //0x3c/0x78 0011 1100 stock screen (left)
#define OLED_ADDR_RIGHT 0x3d //0x3d/0x7a 0011 1101 reistor swapped (right)
#define FLIP180 0 // don't rotate the display
#define INVERT 0  // don't invert the display
#define OLEDRESOLUTION OLED_132x64 //it is actually 128x64 but setting that causes everything to shift
SSOLED ssoled; //SSOLED structure. Each structure is about 56 bytes
//buffer holds 8 rows of pixels per array index (LSB=top), starting at top left, moving right, and wrapping down 8 pixels to the block of pixels
#define USE_BACKBUFFER
#define MAX_LENGTH 21 //maximum length of oled line of text

static uint8_t zerobuffer[1024]; //128*64/8
static uint8_t onebuffer[1024];  //128*64/8


char topword[8] = {'\0'};
char last_topword[8] = {'\0'};
char bottomword[8] = {'\0'};
char last_bottomword[8] = {'\0'};
char lettertable[32]={'P','D','V','O','L','H','Z',' ','Q','C','T','U','K','J',' ','A','N','F','W','I','M','G','X','E','R','B','S','Y',' ',' ',' ','*'};



enum Mode {
  MODE_DEFAULT,
  MODE_MENU,
  MODE_MENU_SETTINGS,
  MODE_MENU_SETTINGS_BRIGHTNESS,
  MODE_MENU_SETTINGS_PARTYSPEED,
  MODE_MENU_SETTINGS_LEDMODE,
  MODE_SNURDLE_MENU,
  MODE_SNURDLE,
  MODE_SNURDLE_ALPHABET,
  MODE_MYSTERY,
  MODE_SCREENOFF,
  MODE_BATTLE_MENU,
  MODE_BATTLE,
  MODE_CREDITS
};

Mode current_mode = MODE_DEFAULT;


char const *main_menu[] = {
  "Settings",
  "Play Snurdle",
  "Play Battle",
  "Mystery",
  "Credits"
};

char const *settings_menu[] = {
  "LED Brightness",
  "LED Mode",
  "LED Party Toggle",
  "LED Party Speed",
  "Factory Reset"
};

bool sure=false;

char const *ledmode_menu[] = {
  "Both on",
  "Top on",
  "Bottom on",
  "Off"
};

const char * credits[] = {
    "Credits",
    "HW/SW Design: Gigs",
    "Art: Forge",
    "Assemble/test: KT",
    "",
    "Special Thanks:",
    "Mom,Seeess,Orblivion"
};

#define credits_size (sizeof (credits) / sizeof (const char *))   
bool credits_dirty=false;

short main_menu_selected=0;
short settings_menu_selected=0;
short ledmode_menu_selected=0;
short default_mode_screen=0;
bool default_dirty=true;
bool just_booted=true;  //this is only for showing the basic instruction screen
bool mystery_dirty=false;
#define ATTRACT_DELAY 5000
bool fs_fail=false;

enum Ledmode {
  BOTH_ON,
  TOP_ON,
  BOTTOM_ON,
  OFF
};

//LED Party
bool led_party=false;
int fade_speed = 5; // Speed of fading
unsigned long fade_interval = 20;  // Interval between each brightness change
unsigned long previous_millis_1 = 0;
unsigned long previous_millis_2 = 0;
int brightness_1 = 0;
int brightness_2 = 0;
bool fading_in_1 = true;
bool fading_in_2 = true;

 //Snurdle
short current_snurdle_guess=0;
char snurdle_guesses[6][MAX_LENGTH] = {'\0'};
bool snurdle_animate=false;
#define MAX_SNURDLE 65535
const char* bitmap_file_name = "/bitmap.bin";
const int bitmap_size = (MAX_SNURDLE / 8) + 1; // Size in bytes
uint8_t bitmap[bitmap_size] = {0};
enum Display {
  LOWER,
  UPPER,
  REVERSE,
  PLAYED
  };
Display snurdle_alpha[26];

//Battle
long battle_topscore=0;
long battle_bottomscore=0;
bool battle_dirty=true;
bool battle_sure=false;
#define MAX_PLAYED_WORDS 20
typedef struct {
  char player1_words[MAX_PLAYED_WORDS][MAX_LENGTH];
  char player2_words[MAX_PLAYED_WORDS][MAX_LENGTH];
  int count;
} WordTracker;
WordTracker tracker;

//general debounce/repeat delay
#define DEBOUNCE_DELAY 3
#define REPEAT_DELAY 500


#define SETTINGS_VERSION 1018
struct settings
{
  bool init;
  byte brightness;
  long snurdle_score;
  long settings_version;
  unsigned short last_snurdle_word;
  Ledmode led_mode;
  long battle_top_highscore;
  long battle_bottom_highscore;
};

struct settings currentsettings;

//dictionary file object
File dictionary;
File sndictionary;


//Protoypes for utility functions
char* longtostring(long);
char* shorttostring(short);

//core functions
void configure_leds();
void write_settings();
void draw_menu(const char **, short, short);
void factory_reset();
void fade_led(int pin, int& brightness, bool& fading_in);
void display_default_mode();
void print_credits(const char** thecredits);

//easter eggs
void too_many_secrets();
void psycho();

//Word related
bool is_word_safe(const char*);
void get_random_word(int, char*, int, bool);
bool check_dictionary(const char*);
void readletters();
void remove_asterisks(char*);
void replace_asterisks(char*);
void readbuttons();
int score_word(const char*);
void scramble_string(char*, uint8_t);
char** copy_string_array(const char** array, int size); //warning, malloc!
void scramble_strings(char** strings, int num_strings, uint8_t parameter);

//prints a word and shakes it, normal font
void shake_word(short line, char* theword);

//OLED related
#define SMALL_LINE 20
#define NORMAL_LINE 16
#define LARGE_LINE 8
void pad_line(char*, short);
void setupOledScreens();
void flashLEDs();
void clear_screen();
void show_image(const char*);
void invert_screen();
void clear_screen_fancy();

//Snurdle related
void display_snurdle();
bool write_snurdle_line(const char* guess, const char* the_word, short y, bool animate);
void change_snurdle();

void mark_used(uint16_t);
bool is_used(uint16_t);
void save_bitmap();
void load_bitmap();
void clear_bitmap();

//battle related
void tracker_init(WordTracker* tracker);
bool has_been_played(const WordTracker* tracker, const char* word, int player);
void insert_played_word(WordTracker* tracker, const char* word, int player);

void setup(){
  pinMode(LED_BUILTIN, OUTPUT);
  
  pinMode(TOP_LED_PIN, OUTPUT);
  pinMode(BOTTOM_LED_PIN, OUTPUT);
  analogWrite(BOTTOM_LED_PIN, 250);
  analogWrite(TOP_LED_PIN, 250);

  pinMode(BUTT_RIGHT, INPUT_PULLUP);
  pinMode(BUTT_LEFT, INPUT_PULLUP);
  pinMode(BUTT_DOWN, INPUT_PULLUP);
  pinMode(BUTT_UP, INPUT_PULLUP);

  //demux select bus pins
  //the letters are on two separate busses
  //B0 tile select is pin 4,5,6 tile data comes out 10,11,12,13,14
  //B1 tile select is pin 7,8,9 tile data comes out 15,16,17,18,19
  pinMode(4, OUTPUT);
  pinMode(5, OUTPUT);
  pinMode(6, OUTPUT);

  pinMode(7, OUTPUT);
  pinMode(8, OUTPUT);
  pinMode(9, OUTPUT);

  //demux data pins
  pinMode(10, INPUT_PULLUP);
  pinMode(11, INPUT_PULLUP);
  pinMode(12, INPUT_PULLUP);
  pinMode(13, INPUT_PULLUP);
  pinMode(14, INPUT_PULLUP);

  pinMode(15, INPUT_PULLUP);
  pinMode(16, INPUT_PULLUP);
  pinMode(17, INPUT_PULLUP);
  pinMode(18, INPUT_PULLUP);
  pinMode(19, INPUT_PULLUP);
  

  Serial.begin(115200); //USB serial port bps 115200/8/n/1

  setupOledScreens();

  //FILE SYSTEM SETUP
  LittleFSConfig cfg;
  cfg.setAutoFormat(false);
  LittleFS.setConfig(cfg);
  if (!(LittleFS.begin()))
  {
    LittleFS.format();
    Serial.println("First boot, formatting LittleFS");
  }
  dictionary=LittleFS.open("/dict.txt","r");
  if (!dictionary)
  {
    Serial.println("Dict File open fail");
    fs_fail=true;
    oledWriteString(&ssoled, 0, 0, 4, (char*)"Dict File open fail", FONT_SMALL, 0, 1);
    flashLEDs();
  }  

  sndictionary=LittleFS.open("/sndict.txt","r");
  if (!sndictionary)
  {
    Serial.println("SnDict File open fail");
    fs_fail=true;
    oledWriteString(&ssoled, 0, 0, 4, (char*)"SnDict File open fail", FONT_SMALL, 0, 1);
    flashLEDs();
  }  


  show_image("/splash.bin");
  delay(1000);
   
  clear_screen();
  oledSetTextWrap(&ssoled, true);

  Serial.print("startup\n");
  
  EEPROM.begin(512);
  EEPROM.get(0,currentsettings);
  if (currentsettings.settings_version != SETTINGS_VERSION)
  {
    factory_reset();
  }
  else
  {
    
    Serial.println("Loaded settings");
    Serial.print("snurdle score: ");
    Serial.println(currentsettings.snurdle_score, DEC);
    Serial.print("Version: ");
    Serial.println(currentsettings.settings_version, DEC);
    Serial.print("Brightness: ");
    Serial.println(currentsettings.brightness, DEC);
    Serial.print("LED Mode: ");
    Serial.println(currentsettings.led_mode, DEC);
    Serial.print("Last Snurdle: ");
    Serial.println(currentsettings.last_snurdle_word, DEC);
    Serial.print("Battle top highscore: ");
    Serial.println(currentsettings.battle_top_highscore, DEC);
    Serial.print("Battle bottom highscore: ");
    Serial.println(currentsettings.battle_bottom_highscore, DEC);
    
  }
  
  configure_leds();   

  //load snurdle bitmap of played games
  load_bitmap();
  //make sure game state is reset
  change_snurdle();
  
  //battle mode word tracker
  tracker_init(&tracker);

} /* setup() */


void up_handler()
{
  char theword[MAX_LENGTH];
  switch (current_mode) {
    case MODE_DEFAULT:
      default_mode_screen++;
      default_dirty=true;
      break;
    case MODE_MENU:
      if (main_menu_selected > 0)
        main_menu_selected--;
    case MODE_MENU_SETTINGS:
      if (settings_menu_selected > 0)
        settings_menu_selected--;
      break;
    case MODE_MENU_SETTINGS_BRIGHTNESS:
      if (currentsettings.brightness > 8)
        currentsettings.brightness -= 8;
      else
        currentsettings.brightness = 0;
      configure_leds();
      break;

    case MODE_MENU_SETTINGS_PARTYSPEED:
      if (fade_interval > 5)
      {
        fade_interval-=5;
      }
      break;
    
    case MODE_MENU_SETTINGS_LEDMODE:
      if (ledmode_menu_selected > 0)
        ledmode_menu_selected--;
      currentsettings.led_mode=(Ledmode)ledmode_menu_selected;
      configure_leds();
      break;
      
    case MODE_SNURDLE_MENU:
      if (currentsettings.last_snurdle_word < MAX_SNURDLE)
      {
        currentsettings.last_snurdle_word++;
        change_snurdle();
      }
      break;

    case MODE_SNURDLE:
      break;
    
    case MODE_MYSTERY:
      break;

    case MODE_BATTLE_MENU:
      break;

    case MODE_BATTLE:
      battle_dirty=true;
      battle_sure=false;
      theword[0]='\0';
      pad_line(theword, NORMAL_LINE);
      oledWriteString(&ssoled, 0, 0, 2, theword, FONT_NORMAL, 0, 1);
      oledWriteString(&ssoled, 0, 0, 4, theword, FONT_NORMAL, 0, 1);
      
      readletters();  //populates global "topword"
      remove_asterisks(topword);
      strcpy(theword, topword);
      pad_line(theword, NORMAL_LINE);
      if (!check_dictionary(topword))
      {
        shake_word(1, theword);
        battle_topscore = battle_topscore - 10;
        if (battle_topscore < 0)
          battle_topscore=0;
        oledWriteString(&ssoled, 0, 0, 3, (char*)"Top Lost 10 Pts  ", FONT_NORMAL, 0, 1);
      }
      else
      {
        if (!has_been_played(&tracker, topword, 1))
        {
          short newscore;
          newscore=score_word(topword);
          battle_topscore+=newscore;
          oledWriteString(&ssoled, 0, 0, 3, (char*)"Top scored:      ", FONT_NORMAL, 0, 1);
          oledWriteString(&ssoled, 0, 11*8, 3, shorttostring(newscore), FONT_NORMAL, 0, 1);
          insert_played_word(&tracker, topword, 1);
        }
        else
        {
          oledWriteString(&ssoled, 0, 0, 3, (char*)"Already played  ", FONT_NORMAL, 0, 1);
        }
      }
      break;
    
    case MODE_CREDITS:
    {
      char ** credits_copy = copy_string_array(credits, credits_size);
      scramble_strings(credits_copy, credits_size, 32);
      print_credits((const char **)credits_copy);
      free(credits_copy);
    }
      break;
  }
}

void down_handler()
{
  char theword[MAX_LENGTH];
  switch (current_mode) {
    case MODE_DEFAULT:
        default_mode_screen--;
        default_dirty=true;
      break;
    case MODE_MENU:
    {
      short num_options=sizeof(main_menu)/sizeof(main_menu[0]);
      if (main_menu_selected < (num_options - 1))
      {
        main_menu_selected++;
      }
      break;
    }
      break;
    case MODE_MENU_SETTINGS:
      {
        short num_options=sizeof(settings_menu)/sizeof(settings_menu[0]);
        if (settings_menu_selected < (num_options - 1))
        {
          settings_menu_selected++;
        } 
      }
      break;
    case MODE_MENU_SETTINGS_BRIGHTNESS:
      if (currentsettings.brightness <= 239)
        currentsettings.brightness += 8;
      else
        currentsettings.brightness=255;
      configure_leds();
      break;

    case MODE_MENU_SETTINGS_PARTYSPEED:
      if (fade_interval < 100)
      {
        fade_interval+=5;
      }
      break;
      
    case MODE_MENU_SETTINGS_LEDMODE:
      {
        short num_options=sizeof(ledmode_menu)/sizeof(ledmode_menu[0]);
        if (ledmode_menu_selected < (num_options - 1))
        {
          ledmode_menu_selected++;
        } 
      }
      currentsettings.led_mode=(Ledmode)ledmode_menu_selected;
      configure_leds();
      break;

    case MODE_SNURDLE_MENU:
      if (currentsettings.last_snurdle_word > 1)
      {
        currentsettings.last_snurdle_word--;
        change_snurdle();
      }
      break;

    case MODE_SNURDLE:
      char printline[MAX_LENGTH];
      if (is_used(currentsettings.last_snurdle_word))
      { //we are on the game over screen
        currentsettings.last_snurdle_word++;
        sprintf(printline, "NextGame# %d", currentsettings.last_snurdle_word);
        oledWriteString(&ssoled, 0, 0, 0, printline, FONT_NORMAL, 1, 1);
        change_snurdle();
        snurdle_animate=true;
        delay(1000);
        clear_screen();
        break;
      }
      readletters();  //populates global "topword"
      strcpy(printline, topword);
      pad_line(printline, NORMAL_LINE);
      if (!check_dictionary(topword))
      {
        shake_word(current_snurdle_guess, printline);
      }
      else
      {
        strcpy(snurdle_guesses[current_snurdle_guess], topword);
        current_snurdle_guess++;
        snurdle_animate=true;
      }
      break;
    
    case MODE_MYSTERY:
      break;
      
    case MODE_BATTLE_MENU:
      break;

    case MODE_BATTLE:
      battle_dirty=true;
      battle_sure=false;
      //clear old text from last play
      theword[0]='\0';
      pad_line(theword, NORMAL_LINE);
      oledWriteString(&ssoled, 0, 0, 3, theword, FONT_NORMAL, 0, 1);
      oledWriteString(&ssoled, 0, 0, 2, theword, FONT_NORMAL, 0, 1);
      
      readletters();  //populates globals topword bottomword
      remove_asterisks(bottomword);
      strcpy(theword, bottomword);
      pad_line(theword, NORMAL_LINE);
      if (!check_dictionary(bottomword))
      {
        shake_word(6, theword);
        battle_bottomscore = battle_bottomscore - 10;
        if (battle_bottomscore < 0)
          battle_bottomscore=0;
        oledWriteString(&ssoled, 0, 0, 4, (char*)"Bot Lost 10 Pts  ", FONT_NORMAL, 0, 1);
      }
      else
      {

        if (!has_been_played(&tracker, bottomword, 2))
        {
          short newscore;
          newscore=score_word(bottomword);
          battle_bottomscore+=newscore;
          oledWriteString(&ssoled, 0, 0, 4, (char*)"Bot scored:     ", FONT_NORMAL, 0, 1);
          oledWriteString(&ssoled, 0, 11*8, 4, shorttostring(newscore), FONT_NORMAL, 0, 1);
          insert_played_word(&tracker, bottomword, 2);
        }
        else
        {
          oledWriteString(&ssoled, 0, 0, 4, (char*)"Already played  ", FONT_NORMAL, 0, 1);
        }
        
        
      }
      break;
      
    case MODE_CREDITS:
    {
      char ** credits_copy = copy_string_array(credits, credits_size);
      scramble_strings(credits_copy, credits_size, 128);
      print_credits((const char **)credits_copy);
      free(credits_copy);
    }
      break;
  }
    
}

void left_handler()
{
  switch (current_mode) {
    case MODE_DEFAULT:
      clear_screen();
      oledWriteString(&ssoled, 0, 0, 0, (char*)"Screen turning off", FONT_SMALL, 0, 1);
      oledWriteString(&ssoled, 0, 0, 1, (char*)"Hit right to turn on", FONT_SMALL, 0, 1);
      delay(2000);
      clear_screen();
      current_mode=MODE_SCREENOFF;
      break;
    case MODE_MENU:
      clear_screen();
      current_mode=MODE_DEFAULT;
      break;
    case MODE_MENU_SETTINGS:
      sure=false;
      clear_screen();
      current_mode=MODE_MENU;
      break;
    case MODE_MENU_SETTINGS_BRIGHTNESS:
      clear_screen();
      write_settings();
      current_mode=MODE_MENU_SETTINGS;
      break;

    case MODE_MENU_SETTINGS_PARTYSPEED:
      clear_screen();
      current_mode=MODE_MENU_SETTINGS;
      break;
    case MODE_MENU_SETTINGS_LEDMODE:
      clear_screen();
      write_settings();
      current_mode=MODE_MENU_SETTINGS;
      break;

    case MODE_SNURDLE_MENU:
      clear_screen();
      write_settings();
      current_mode=MODE_MENU;
      configure_leds();
      break;
    
    case MODE_SNURDLE:
      save_bitmap();
      clear_screen();
      current_mode=MODE_SNURDLE_MENU;
      break;
    
    case MODE_MYSTERY:
      clear_screen();
      current_mode=MODE_MENU;
      break;

    case MODE_SNURDLE_ALPHABET:
      clear_screen();
      snurdle_animate=true;
      current_mode=MODE_SNURDLE;
      break;

    case MODE_BATTLE_MENU:
      clear_screen();
      write_settings();
      current_mode=MODE_MENU;
      break;

    case MODE_BATTLE:
      clear_screen();
      battle_sure=false;
      current_mode=MODE_BATTLE_MENU;
      break;
    
    case MODE_CREDITS:
      clear_screen();
      current_mode=MODE_MENU;
      break;
  }

}

void right_handler()
{
  char printline[MAX_LENGTH] = {'\0'};
  switch (current_mode) {
    case MODE_DEFAULT:
      clear_screen();
      default_dirty=true;
      current_mode=MODE_MENU;
      break;
    case MODE_MENU:
      clear_screen();
      switch(main_menu_selected) {
        case 0:
          current_mode=MODE_MENU_SETTINGS;
          break;
        case 1:
          current_mode=MODE_SNURDLE_MENU;
          break;
        case 2:
          current_mode=MODE_BATTLE_MENU;
          break;
        case 3:
          mystery_dirty=true;
          current_mode=MODE_MYSTERY;
          break;
        case 4:
          credits_dirty=true;
          current_mode=MODE_CREDITS;
          break;
      }
      break;
    case MODE_MENU_SETTINGS:
      clear_screen();
      switch(settings_menu_selected) {
        case 0:
          current_mode=MODE_MENU_SETTINGS_BRIGHTNESS;
          sure=false;
          break;
        case 1:
          current_mode=MODE_MENU_SETTINGS_LEDMODE;
          sure=false;
          break;
        case 2:
          sure=false;
          led_party=!led_party;
          if (!led_party)
            configure_leds();
          break;
        case 3:
          led_party=true;
          clear_screen();
          current_mode=MODE_MENU_SETTINGS_PARTYSPEED;
          break;
        case 4:
          if (!sure)
          {
            oledWriteString(&ssoled, 0, 0, 6, (char*)"Deletes all progress!", FONT_SMALL, 0, 1);
            oledWriteString(&ssoled, 0, 0, 7, (char*)"Hit right to confirm", FONT_SMALL, 0, 1);
            sure=true;
          }
          else
          {
            factory_reset();
            oledWriteString(&ssoled, 0, 0, 6, (char*)"                   ", FONT_SMALL, 0, 1);
            oledWriteString(&ssoled, 0, 0, 7, (char*)"Factory reset done", FONT_SMALL, 0, 1);
            sure=false;
          }
          break;
      }
      break;
    case MODE_MENU_SETTINGS_BRIGHTNESS:
      break;
    case MODE_MENU_SETTINGS_PARTYSPEED:
      brightness_1=currentsettings.brightness;
      break;
    case MODE_MENU_SETTINGS_LEDMODE:
      break;

    case MODE_SNURDLE_MENU:
      clear_screen();
      write_settings();

      //temporarily only turn on top LED
      led_party=false; //no more party for now
      switch (currentsettings.led_mode) {
        case BOTH_ON:
          analogWrite(BOTTOM_LED_PIN, 255);
          analogWrite(TOP_LED_PIN, currentsettings.brightness);
          break;
        case BOTTOM_ON:
          analogWrite(BOTTOM_LED_PIN, 255);
          analogWrite(TOP_LED_PIN, currentsettings.brightness);
          break;
      } //end switch led_mode
      current_mode=MODE_SNURDLE;
      snurdle_animate=true;
      break;
    
    case MODE_SNURDLE:
      clear_screen();
      current_mode=MODE_SNURDLE_ALPHABET;
      break;
    
    case MODE_MYSTERY:
      break;

    case MODE_SCREENOFF:
      default_dirty=true;
      current_mode=MODE_DEFAULT;
      break;

    case MODE_SNURDLE_ALPHABET:
      clear_screen();
      snurdle_animate=true;
      current_mode=MODE_SNURDLE;
    break;

    case MODE_BATTLE_MENU:
      clear_screen();
      battle_dirty=true;
      current_mode=MODE_BATTLE;
      break;

    case MODE_BATTLE:
      //blank old scores
      if (battle_sure)
      {
        clear_screen();

        tracker_init(&tracker);
        
        //print new ones
        oledWriteString(&ssoled, 0, 0, 0, (char*)"GAME OVER", FONT_NORMAL, 0, 1);
        
        oledWriteString(&ssoled, 0, 0, 2, (char*)"Final Top:", FONT_NORMAL, 0, 1);
        oledWriteString(&ssoled, 0, -1, -1, longtostring(battle_topscore), FONT_NORMAL, 0, 1);      
        
        oledWriteString(&ssoled, 0, 0, 3, (char*)"Final Bot:", FONT_NORMAL, 0, 1);      
        oledWriteString(&ssoled, 0, -1, -1, longtostring(battle_bottomscore), FONT_NORMAL, 0, 1);      
  
        
        if (battle_topscore > currentsettings.battle_top_highscore)
          currentsettings.battle_top_highscore=battle_topscore;
        if (battle_bottomscore > currentsettings.battle_bottom_highscore)
          currentsettings.battle_bottom_highscore=battle_bottomscore;
          
        oledWriteString(&ssoled, 0, 0, 5, (char*)"Best Top:", FONT_NORMAL, 0, 1);
        oledWriteString(&ssoled, 0, -1, -1, longtostring(currentsettings.battle_top_highscore), FONT_NORMAL, 0, 1);      
        
        oledWriteString(&ssoled, 0, 0, 6, (char*)"Best Bot:", FONT_NORMAL, 0, 1);
        oledWriteString(&ssoled, 0, -1, -1, longtostring(currentsettings.battle_bottom_highscore), FONT_NORMAL, 0, 1);      
  
        battle_topscore=0;
        battle_bottomscore=0;
        write_settings();
      }
      else
      {
        oledWriteString(&ssoled, 0, 0, 2, (char*)"Right again to end", FONT_SMALL, 0, 1);
        battle_sure=true;
      }
      break;
      
    case MODE_CREDITS:
      break;
  }
  
}


short loopcounter=0;
void loop() {
  unsigned long current_millis = millis();
  char printline[MAX_LENGTH];
  loopcounter++;

  readbuttons();


    
  if (led_party) //party time, excellent
  {
    if ((currentsettings.led_mode == TOP_ON)||(currentsettings.led_mode == BOTH_ON))
    {
      if (current_millis - previous_millis_1 >= fade_interval) 
      {
        previous_millis_1 = current_millis;
        fade_led(TOP_LED_PIN, brightness_1, fading_in_1);
      }
    }
    if ((currentsettings.led_mode == BOTTOM_ON)||(currentsettings.led_mode == BOTH_ON))
    {
      if (current_millis - previous_millis_2 >= fade_interval) 
      {
        previous_millis_2 = current_millis;
        fade_led(BOTTOM_LED_PIN, brightness_2, fading_in_2);
      }
    }
  }


  switch (current_mode) {
    case MODE_DEFAULT:
      display_default_mode();
    break; //End MODE_DEFAULT
    case MODE_MENU:
      draw_menu(main_menu, sizeof(main_menu)/sizeof(main_menu[0]), main_menu_selected);
    break; //End MODE_MENU
    case MODE_MENU_SETTINGS:
      draw_menu(settings_menu, sizeof(settings_menu)/sizeof(settings_menu[0]), settings_menu_selected);
    break; //End MODE_MENU_SETTINGS
    case MODE_MENU_SETTINGS_BRIGHTNESS:
      {
        oledWriteString(&ssoled, 0, 0, 0, (char*)"Up for brighter", FONT_SMALL, 0, 1);
        oledWriteString(&ssoled, 0, 0, 1, (char*)"Down for dimmer", FONT_SMALL, 0, 1);
        oledWriteString(&ssoled, 0, 0, 2, (char*)"Left for save/exit", FONT_SMALL, 0, 1);
        oledWriteString(&ssoled, 0, 0, 4, (char*)"Bright LEDs decrease", FONT_SMALL, 0, 1);
        oledWriteString(&ssoled, 0, 0, 5, (char*)"battery life!", FONT_SMALL, 0, 1);

        int len = ((255 - currentsettings.brightness) * 18)/255;
        char progress[MAX_LENGTH];
        snprintf(progress, MAX_LENGTH, "|%-*.*s%*s|", len, len, "********************", 18 - len, "");
        oledWriteString(&ssoled, 0, 0, 6, progress, FONT_SMALL, 0, 1);
        
      } //End Brightness menu
      break;
    case MODE_MENU_SETTINGS_PARTYSPEED:
      {
        oledWriteString(&ssoled, 0, 0, 0, (char*)"Up for faster", FONT_SMALL, 0, 1);
        oledWriteString(&ssoled, 0, 0, 1, (char*)"Down for slower", FONT_SMALL, 0, 1);
        oledWriteString(&ssoled, 0, 0, 2, (char*)"Hold Right to desync", FONT_SMALL, 0, 1);
        oledWriteString(&ssoled, 0, 0, 3, (char*)"Left for exit", FONT_SMALL, 0, 1);
        int len = (((100 - fade_interval) * 18)/95);
        char progress[MAX_LENGTH];
        snprintf(progress, MAX_LENGTH, "|%-*.*s%*s|", len, len, "********************", 18 - len, "");
        oledWriteString(&ssoled, 0, 0, 6, progress, FONT_SMALL, 0, 1);
        
      } //End Party speed
      break;
    
    case MODE_MENU_SETTINGS_LEDMODE:
      draw_menu(ledmode_menu, sizeof(ledmode_menu)/sizeof(ledmode_menu[0]), ledmode_menu_selected);
      break;
    case MODE_SNURDLE_MENU:
      oledWriteString(&ssoled, 0, 0, 0, (char*)"Guess 7 tiles top row", FONT_SMALL, 0, 1);
      oledWriteString(&ssoled, 0, 0, 1, (char*)"Down locks in guess", FONT_SMALL, 0, 1);
      oledWriteString(&ssoled, 0, 0, 2, (char*)"Right for alphabet", FONT_SMALL, 0, 1);
      oledWriteString(&ssoled, 0, 0, 3, (char*)"lowcase: not in word", FONT_SMALL, 0, 1);
      oledWriteString(&ssoled, 0, 0, 4, (char*)"CAPS: wrong spot", FONT_SMALL, 0, 1);
      oledWriteString(&ssoled, 0, 0, 5, (char*)"Reverse:", FONT_SMALL, 1, 1);
      oledWriteString(&ssoled, 0, -1, 5, (char*)" correct", FONT_SMALL, 0, 1);



      oledWriteString(&ssoled, 0, 0, 6, (char*)"Puzzle: ", FONT_SMALL, 0, 1);
      if (is_used(currentsettings.last_snurdle_word))
        oledWriteString(&ssoled, 0, -1, 6, longtostring(currentsettings.last_snurdle_word), FONT_SMALL, 1, 1);
      else
        oledWriteString(&ssoled, 0, -1, 6, longtostring(currentsettings.last_snurdle_word), FONT_SMALL, 0, 1);
      oledWriteString(&ssoled, 0, -1, 6, (char*)" (up/down)", FONT_SMALL, 0, 1);
      
      oledWriteString(&ssoled, 0, 0, 7, (char*)"Hit Right to begin", FONT_SMALL, 0, 1);
      break;
    case MODE_SNURDLE:
      readletters();
      if ((strcmp(last_topword, topword) != 0)||(snurdle_animate))
      {
        strcpy(last_topword, topword);
        display_snurdle();
      }
      break;

    case MODE_SNURDLE_ALPHABET:
    {
      short y=1;
      short x=0;
      char tempchar[2]={0};
      for (int i = 0; i < 26; i++)
      {
        tempchar[0]='A'+i;
        if (!(i%9))
        {
          y++;
          x=0;
        }
        switch (snurdle_alpha[i]) 
        {
        case UPPER:
          oledWriteString(&ssoled, 0, x*8/*x*/, y/*y*/, tempchar, FONT_NORMAL, 0/*inv color*/, 1);
          break;
        case LOWER:
          tempchar[0]=tolower(tempchar[0]);
          oledWriteString(&ssoled, 0, x*8/*x*/, y/*y*/, tempchar, FONT_NORMAL, 0/*inv color*/, 1);
          break;
        case REVERSE:
          oledWriteString(&ssoled, 0, x*8/*x*/, y/*y*/, tempchar, FONT_NORMAL, 1/*inv color*/, 1);
          break;
        case PLAYED:
          oledWriteString(&ssoled, 0, x*8/*x*/, y/*y*/, (char*)" ", FONT_NORMAL, 0/*inv color*/, 1);
          break;
        }
        x++;
      }
    }
    break;
    
    case MODE_MYSTERY:
      if (mystery_dirty)
      {
        oledWriteString(&ssoled, 0, 0, 3, (char*)"gigsatdc.com/ + ", FONT_SMALL, 0, 1);
        strcpy(printline, "SetecAstronomy");
        scramble_string(printline, 255);
        oledWriteString(&ssoled, 0, 0, 4, printline, FONT_SMALL, 0, 1);
        mystery_dirty=false;
      }
      break;
    
    case MODE_BATTLE_MENU:
      oledWriteString(&ssoled, 0, 0/*x*/, 0/*y*/, (char*)"Battle Mode", FONT_NORMAL, 0/*inv color*/, 1);
      oledWriteString(&ssoled, 0, 0/*x*/, 1/*y*/, (char*)"Up scores top", FONT_SMALL, 0/*inv color*/, 1);
      oledWriteString(&ssoled, 0, 0/*x*/, 2/*y*/, (char*)"Down scores bottom", FONT_SMALL, 0/*inv color*/, 1);
      oledWriteString(&ssoled, 0, 0/*x*/, 3/*y*/, (char*)"Right to begin", FONT_SMALL, 0/*inv color*/, 1);
      oledWriteString(&ssoled, 0, 0/*x*/, 4/*y*/, (char*)"Right to end game", FONT_SMALL, 0/*inv color*/, 1);
      oledWriteString(&ssoled, 0, 0/*x*/, 6/*y*/, (char*)"See instructions", FONT_SMALL, 0/*inv color*/, 1);
      oledWriteString(&ssoled, 0, 0/*x*/, 7/*y*/, (char*)"for suggested rules", FONT_SMALL, 0/*inv color*/, 1);
      break;

    case MODE_BATTLE:
      readletters();
      if(battle_dirty)
      {
        //blank old scores
        printline[0]='\0';
        pad_line(printline,NORMAL_LINE);
        oledWriteString(&ssoled, 0, 0, 0, printline, FONT_NORMAL, 0, 1);
        oledWriteString(&ssoled, 0, 0, 7, printline, FONT_NORMAL, 0, 1);

        //print new ones
        oledWriteString(&ssoled, 0, 0, 0, (char*)"Top:", FONT_NORMAL, 0, 1);
        oledWriteString(&ssoled, 0, 5*8, 0, longtostring(battle_topscore), FONT_NORMAL, 0, 1);      
        oledWriteString(&ssoled, 0, 0, 7, (char*)"Bot:", FONT_NORMAL, 0, 1);      
        oledWriteString(&ssoled, 0, 5*8, 7, longtostring(battle_bottomscore), FONT_NORMAL, 0, 1);      
      }
      if ((strcmp(last_topword, topword) != 0)||(battle_dirty))
      {
        strcpy(printline, topword);
        remove_asterisks(printline);
        pad_line(printline, NORMAL_LINE);
        oledWriteString(&ssoled, 0, 0, 1, printline, FONT_NORMAL, 0, 1);      
      }

      if ((strcmp(last_bottomword, bottomword) != 0)||(battle_dirty))
      {
        strcpy(printline, bottomword);
        remove_asterisks(printline);
        pad_line(printline, NORMAL_LINE);
        oledWriteString(&ssoled, 0, 0, 6, printline, FONT_NORMAL, 0, 1);
      }
      strcpy(last_topword, topword);
      strcpy(last_bottomword, bottomword);
      battle_dirty=false;
      break;

    case MODE_CREDITS:
      if (credits_dirty)
      {
        print_credits(credits);
        credits_dirty=false;
      }
      break;
    
  } //end main mode switch

} //end loop()


void print_credits(const char** thecredits)
{
  oledWriteString(&ssoled, 0, 0/*x*/, 0/*y*/, (char*)thecredits[0], FONT_NORMAL, 0/*inv color*/, 1);
  for (int y=1; y < credits_size; y++)
  {
    oledWriteString(&ssoled, 0, 0/*x*/, y/*y*/, (char*)thecredits[y], FONT_SMALL, 0/*inv color*/, 1);
  }
}

void too_many_secrets()
{
  char printline[MAX_LENGTH];
  static int secrets_counter=255;
  static int finish_counter=255;
  led_party=true;
  if (secrets_counter > 0)
  {
    secrets_counter--;
  }
  else
  {
    if (finish_counter > 0)
    {
      oledWriteString(&ssoled, 0, 0, 0, topword, FONT_LARGE, 0, 1);
      oledWriteString(&ssoled, 0, 0, 4, bottomword, FONT_LARGE, 0, 1);
      finish_counter--;
      return;
    }
    else
    {
      finish_counter=255;
      secrets_counter=255;
    }
      
    
  }

  if (secrets_counter % 10)
  {
    strcpy(printline, topword);
    scramble_string(printline, secrets_counter);
    oledWriteString(&ssoled, 0, 0, 0, printline, FONT_LARGE, 0, 1);
    strcpy(printline, bottomword);
    scramble_string(printline, secrets_counter);
    oledWriteString(&ssoled, 0, 0, 4, printline, FONT_LARGE, 0, 1);
    for (int i = 0; i < 1024; i++) 
    {
      int x = random(128);  // Generate a random x-coordinate (0 to 127)
      int y = random(64);   // Generate a random y-coordinate (0 to 63)
      oledSetPixel(&ssoled, x, y, 0, 1);  // Set the pixel at the given coordinates
    }
  }
}


void psycho()
{
  static int count=0;
  led_party=true;
  count++;
  switch (count)
  {
    case 1:
      show_image("/psycho.bin");
    break;
    case 32:
      invert_screen();
    break;
    case 64:
      count=0;
    break;
  }
}

void display_default_mode()
{
  char printline[MAX_LENGTH];
  short modesel=abs(default_mode_screen % 6);
  unsigned long current_millis = millis();
  static unsigned long last_millis = millis();
  static unsigned long anim_last_millis = millis();
  const int anim_delay=2500;
  const int anim_frame_delay=300;
  static bool anim_playing=false;
  
  readletters();

    //easter eggs
  char trimmed_top[MAX_LENGTH];
  char trimmed_bot[MAX_LENGTH];
  strcpy(trimmed_top, topword);
  strcpy(trimmed_bot, bottomword);
  remove_asterisks(trimmed_top);
  remove_asterisks(trimmed_bot);
  if ((strcmp(trimmed_top, "TOOMANY") == 0)&&(strcmp(trimmed_bot, "SECRETS") == 0))
  {
    too_many_secrets();
    strcpy(last_topword, topword);
    strcpy(last_bottomword, bottomword);
    return;
  }

  if ((strcmp(trimmed_top, "PSYCHO") == 0)&&(strcmp(trimmed_bot, "HOLICS") == 0))
  {
    psycho();
    strcpy(last_topword, topword);
    strcpy(last_bottomword, bottomword);
    return;
  }

  if ((strcmp(trimmed_top, "SNEAKY") == 0)&&(strcmp(trimmed_bot, "BADGE") == 0))
  {
    if ((current_millis - last_millis) > ATTRACT_DELAY)
    {
      default_mode_screen++;
      modesel=abs(default_mode_screen % 6);
      //Serial.println(default_mode_screen);
      last_millis=current_millis;
      default_dirty=true;
    }
    strcpy(last_topword, topword);
    strcpy(last_bottomword, bottomword);
  }


  //amimate cat
  if ((modesel==3)&&(!default_dirty))
  {
    if ((current_millis - anim_last_millis) > anim_delay)
    {
      anim_playing=true;
      show_image("/cat1bit2.bin");
      anim_last_millis=current_millis;
    }
    if (anim_playing && ((current_millis - anim_last_millis) > anim_frame_delay))
    {
      anim_playing=false;
      show_image("/cat1bit.bin");
      anim_last_millis=current_millis;
    }
  }
  
  
  if ((strcmp(last_bottomword, bottomword) != 0)||(strcmp(last_topword, topword) != 0))
  {
    strcpy(last_topword, topword);
    strcpy(last_bottomword, bottomword);
    default_dirty=true;
  }

  if (just_booted)
  {
    clear_screen();
    oledWriteString(&ssoled, 0, 0, 1, (char*)"Up/Dn: images", FONT_NORMAL, 0, 1);
    oledWriteString(&ssoled, 0, 0, 2, (char*)"Right: menu", FONT_NORMAL, 0, 1);
    oledWriteString(&ssoled, 0, 0, 3, (char*)"Left: ScreenOff", FONT_NORMAL, 0, 1);
    strcpy(printline, "Version: ");
    strcat(printline, shorttostring(SETTINGS_VERSION));
    oledWriteString(&ssoled, 0, 0, 7, printline, FONT_NORMAL, 0, 1);
    just_booted=false;
    default_dirty=false;
    return;
  }

  if (default_dirty)
  {
    switch (modesel)
    {
      case 0:   
        clear_screen_fancy();
        
        strcpy(printline, topword);
        replace_asterisks(printline);
        pad_line(printline, LARGE_LINE);
        oledWriteString(&ssoled, 0, 0, 0, printline, FONT_LARGE, 0, 1);      
  
  
        strcpy(printline, bottomword);
        replace_asterisks(printline);
        pad_line(printline, LARGE_LINE);
        oledWriteString(&ssoled, 0, 0, 4, printline, FONT_LARGE, 0, 1);
  
        if ((strcmp(topword, "*******") == 0)&&(strcmp(bottomword, "*******") == 0))
        {
          if (!fs_fail)
          {
            oledWriteString(&ssoled, 0, 0, 0, (char*)"INSERT  ", FONT_LARGE, 0, 1);
            oledWriteString(&ssoled, 0, 0, 4, (char*)"LETTERS ", FONT_LARGE, 0, 1);
          }
          else
          {
            oledWriteString(&ssoled, 0, 0, 0, (char*)"FILESYST", FONT_LARGE, 0, 1);
            oledWriteString(&ssoled, 0, 0, 4, (char*)"FAILURE ", FONT_LARGE, 0, 1);
          }
          
        }
        break;
      case 1:
        clear_screen_fancy();
        show_image("/splash.bin");
        break;
      case 2:
        clear_screen_fancy();
        show_image("/splash.bin");
        invert_screen();
        break;
      case 3:
        clear_screen_fancy();
        anim_last_millis=current_millis;
        show_image("/cat1bit.bin");
        break;
      case 4:
        clear_screen_fancy();
        show_image("/cat1bit.bin");
        invert_screen();
        break;
      case 5:
        clear_screen_fancy();
        show_image("/btlogo.bin");
        invert_screen();
        break;
    } //end switch modesel
    default_dirty=false;
  }
}


void display_snurdle()
{
  //global current_snurdle_guess
  //global snurdle_guesses[6][MAX_LENGTH]
  //currentsettings.last_snurdle_word

  bool animate=false;

  char theword[MAX_LENGTH] = {'\0'};
  char printline[MAX_LENGTH] = {'\0'};

  get_random_word(currentsettings.last_snurdle_word, theword, 7, true);

  if (is_used(currentsettings.last_snurdle_word))
  { //already done this one
    //just print it in the middle of the screen
    clear_screen();
    write_snurdle_line(theword, theword, 3, true);
    oledWriteString(&ssoled, 0, 0/*x*/, 1/*y*/, (char*)"Down for next", FONT_NORMAL, 0/*inv color*/, 1);
    printline[0]='\0';
    int points=score_word(theword);
    sprintf(printline, "Done. Pts:%d Tot:%d", points, currentsettings.snurdle_score);
    pad_line(printline, SMALL_LINE);
    oledWriteString(&ssoled, 0, 0/*x*/, 7/*y*/, printline, FONT_SMALL, 1/*inv color*/, 1);
    snurdle_animate=false;
    return;
  }
  readletters();  //populates global "topword"
  
  sprintf(printline, "Score: %d", currentsettings.snurdle_score);
  oledWriteString(&ssoled, 0, 0/*x*/, 7/*y*/, printline, FONT_NORMAL, 1/*inv color*/, 1);
  strcpy(printline, "");


  for (short line=0; line < 6; line++)
  {
    if (snurdle_guesses[line][0] == '\0') //unpopulated
    {
      strcpy(printline, topword);
      pad_line(printline, NORMAL_LINE);
      oledWriteString(&ssoled, 0, 0, current_snurdle_guess, printline, FONT_NORMAL, 0, 1);
      break;    
    }
    else
    {
      if ((line == (current_snurdle_guess - 1))&&(snurdle_animate))
      {
        animate=true;
        snurdle_animate=false;
      }
      pad_line(printline, NORMAL_LINE);
      oledWriteString(&ssoled, 0, 0, line, printline, FONT_NORMAL, 0, 1);
      
      if (write_snurdle_line(snurdle_guesses[line], theword, line, animate))
      {
          //win
          printline[0]='\0';
          int points=score_word(theword);
          currentsettings.snurdle_score+=points;
          mark_used(currentsettings.last_snurdle_word);
          save_bitmap();
          write_settings();
          sprintf(printline, "Win! Pts:%d Tot:%d", points, currentsettings.snurdle_score);
          pad_line(printline, SMALL_LINE);
          oledWriteString(&ssoled, 0, 0/*x*/, 7/*y*/, printline, FONT_SMALL, 1/*inv color*/, 1);
          
      }
      else
      {
        if ((current_snurdle_guess >= 6)&&(line==5)) //loss condition
        {
          sprintf(printline, "Word was:%s", theword);
          pad_line(printline, NORMAL_LINE);
          oledWriteString(&ssoled, 0, 0/*x*/, 7/*y*/, printline, FONT_NORMAL, 1/*inv color*/, 1);
          mark_used(currentsettings.last_snurdle_word);
          write_settings();
        }
        
      }
    }
  }
} //end display snurdle

bool write_snurdle_line(const char* guess, const char* the_word, short y, bool animate)
{ 
  short word_length = strlen(the_word);
  bool revealed[word_length] = {false}; // Array to track revealed letters

  short match=0;
  Display displaymap[word_length] = {};
  
  char tempchar[2]={'\0'};
  // Check for correctly positioned letters
  for (int i = 0; i < word_length; i++)
  {
    if (guess[i] == the_word[i])
    {
      displaymap[i]=REVERSE;
      snurdle_alpha[guess[i] - 'A']=REVERSE;
      revealed[i] = true; // Mark the letter as revealed
      match++;
    }
  }
  
  for (int i = 0; i < word_length; i++)
  {
    bool foundflag=false;
    if (displaymap[i] == REVERSE)
      continue;
    for (int j = 0; j < word_length; j++)
    {
      if ((guess[i] == the_word[j]) && (!revealed[j]))
      {
        revealed[j]=true;
        foundflag=true;
        displaymap[i]=UPPER;
        if (snurdle_alpha[guess[i] - 'A']==LOWER)
          snurdle_alpha[guess[i] - 'A']=UPPER;
        break;
      }
    }
    if (!foundflag)
    {
      if (snurdle_alpha[guess[i] - 'A']==LOWER)
        snurdle_alpha[guess[i] - 'A']=PLAYED;
      displaymap[i]=LOWER;
      //not found
    }
  }

  //display them now
  for (int i = 0; i < word_length; i++)
  {
    tempchar[0]=guess[i];
    if (animate)
    {
      oledWriteString(&ssoled, 0, i*8/*x*/, y/*y*/, tempchar, FONT_NORMAL, 1/*inv color*/, 1);
      delay(50);
      oledWriteString(&ssoled, 0, i*8/*x*/, y/*y*/, tempchar, FONT_NORMAL, 0/*inv color*/, 1);
      delay(50);
      oledWriteString(&ssoled, 0, i*8/*x*/, y/*y*/, tempchar, FONT_NORMAL, 1/*inv color*/, 1);
      delay(50);
      oledWriteString(&ssoled, 0, i*8/*x*/, y/*y*/, tempchar, FONT_NORMAL, 0/*inv color*/, 1);
    }
    switch (displaymap[i]) 
    {
    case UPPER:
      oledWriteString(&ssoled, 0, i*8/*x*/, y/*y*/, tempchar, FONT_NORMAL, 0/*inv color*/, 1);
      break;
    case LOWER:
      tempchar[0]=tolower(guess[i]);
      oledWriteString(&ssoled, 0, i*8/*x*/, y/*y*/, tempchar, FONT_NORMAL, 0/*inv color*/, 1);
      break;
    case REVERSE:
      oledWriteString(&ssoled, 0, i*8/*x*/, y/*y*/, tempchar, FONT_NORMAL, 1/*inv color*/, 1);
      break;
    }
    
  }
  
  return (match==7);
}


void change_snurdle()
{
    for (int i = 0; i < 6; i++)
    {
        strcpy(snurdle_guesses[i], "");
    }
    current_snurdle_guess=0;
    for (int i = 0; i < 26; i++)
    {
      snurdle_alpha[i]=LOWER;
    }
}

void shake_word(short line, char* theword)
{
  for (int x=0; x<=3; x++)
  {
    oledWriteString(&ssoled, 0, 0, line, theword, FONT_NORMAL, 0, 1);
    delay(50);
    oledWriteString(&ssoled, 0, 0, line, (char *)" ", FONT_NORMAL, 0, 1);
    oledWriteString(&ssoled, 0, 5, line, theword, FONT_NORMAL, 0, 1);
    delay(50);
  }
  oledWriteString(&ssoled, 0, 0, line, theword, FONT_NORMAL, 0, 1);
}


int score_word(const char* theword)
{
    int scores[] = {
        1, 3, 3, 2, 1, 4, 2, 4, 1, 8, 5, 2, 4, 2, 1, 4, 10, 1, 1, 1, 2, 4, 4, 8, 3, 10
    };

    int score = 0;
    for (int i = 0; theword[i] != '\0'; ++i)
    {
        char ch = theword[i];
        if (ch == '*')
        {
            score += 0;
        }
        else if (ch >= 'A' && ch <= 'Z')
        {
            int index = ch - 'A';
            score += scores[index];
        }
    }

    return score;
}


//WARNING: This assumes random_word can hold at least 20 chars.
void get_random_word(int seed, char *random_word, int word_size, bool sndict) {

  File dict;
  if (sndict)
    dict=sndictionary;
  else
    dict=dictionary;
 
    
  // Set the random seed
  randomSeed(seed);
  // Get the size of the dictionary file
  long file_size = dict.size();
  int cur_word_size=0;
  bool safe=false;
  
  while ((cur_word_size!=word_size) || (safe==false))
  {
    // Generate a random position within the file
    long random_pos = random(file_size);
  
    // Seek to the random position
    dict.seek(random_pos);
  
    // Skip to the beginning of the current line
    while (true) {
      if (dict.position() == 0) {
        break;  // At the beginning of the file
      }
      char currentChar = dict.read();
      if (currentChar == '\n') {
        break;  // At the beginning of the line
      }
    }
    cur_word_size=dict.readBytesUntil('\n', random_word, 20);
    random_word[cur_word_size] = '\0';  // Ensure null termination
    safe=is_word_safe(random_word);  //only tiles that come with the badge
      
  }
}

//returns true if word is in dictionary
bool check_dictionary(const char* searchWord) {
  long fileSize = dictionary.size();
  long left = 0;
  long right = fileSize - 1;

  while (left <= right) {
    long middle = (left + right) / 2;
    dictionary.seek(middle);

    // Skip to the beginning of the current line
    while (dictionary.peek() != '\n' && dictionary.position() > 0) {
      dictionary.seek(dictionary.position() - 1);
    }
    
    // Skip to the beginning of the current line
    while (true) {
      if (dictionary.position() == 0) {
        break;  // At the beginning of the file
      }
      char currentChar = dictionary.read();
      if (currentChar == '\n') {
        break;  // At the beginning of the line
      }
    }

    // Read the current line
    char line[MAX_LENGTH];
    line[0]='\0';
    int wordsize;
    wordsize=dictionary.readBytesUntil('\n', line, sizeof(line));
    line[wordsize] = '\0';  // Ensure null termination


//char printline[MAX_LENGTH]={'\0'};
//    printline[0]='\0';
//    strcpy(printline, line);
//    pad_small_line(printline);
//    oledWriteString(&ssoled, 0, 0, 2, printline, FONT_SMALL, 0, 1);
//    delay(500);

    // Compare the search word with the current line
    int comparison = strcmp(searchWord, line);

    if (comparison == 0) {
      // Word found
      return true;
    } else if (comparison < 0) {
      // Search in the left half
      right = middle - 1;
    } else {
      // Search in the right half
      left = middle + 1;
    }
  }

  // Word not found
  return false;
}

//the letters are on two separate busses
//B0 (bottom row) tile select is pin 4,5,6 tile data comes out 10,11,12,13,14
//B1 (top row) tile select is pin 7,8,9 tile data comes out 15,16,17,18,19



//populates globals topword and bottomword, uses * for empty spaces
void readletters()
{
  int letter=0;
  topword[7]='\0';
  bottomword[7]='\0';
  
  if (DEBUG) Serial.println("Top");
  for (int x = 0; x<7; x++)
  {
    digitalWrite(7, x & 1);
    digitalWrite(8, (x>>1) & 1);
    digitalWrite(9, (x>>2) & 1);

    delay(1);
    letter=0;
    letter=letter | digitalRead(19);
    letter=letter | (digitalRead(18) << 1);
    letter=letter | (digitalRead(17) << 2);
    letter=letter | (digitalRead(16) << 3);
    letter=letter | (digitalRead(15) << 4);
    if (DEBUG)
    {
      Serial.print(letter, BIN);
      Serial.print(" ");
      Serial.println(lettertable[letter]);
    }
    topword[x]=lettertable[letter];
  }

  if (DEBUG) Serial.println("Bottom");
  for (int x = 0; x<7; x++)
  {
    digitalWrite(4, x & 1);
    digitalWrite(5, (x>>1) & 1);
    digitalWrite(6, (x>>2) & 1);

    delay(1);
    letter=0;
    letter=letter | digitalRead(14);
    letter=letter | (digitalRead(13) << 1);
    letter=letter | (digitalRead(12) << 2);
    letter=letter | (digitalRead(11) << 3);
    letter=letter | (digitalRead(10) << 4);
    
    if (DEBUG)
    {
      Serial.print(letter, BIN);
      Serial.print(" ");
      Serial.println(lettertable[letter]);
    }
    bottomword[x]=lettertable[letter];
  }
  if (DEBUG) Serial.println(" ");
  
}


//function for reading buttons
void readbuttons()
{
  //Button states
  static bool left_pressed=false;
  static bool right_pressed=false;
  static bool up_pressed=false;
  static bool down_pressed=false;
  static long left_last=millis();
  static long right_last=millis();
  static long up_last=millis();
  static long down_last=millis();
  

  if (!digitalRead(BUTT_LEFT))
  {
    if (!left_pressed)
    {
      left_pressed=true;
      left_last=millis();
    }
    else //being held down
    {
      if (((int)millis() - left_last) > DEBOUNCE_DELAY)
      {
        left_last=millis() + REPEAT_DELAY;
        left_handler();
      }
    }
  }
  else
  {
    left_pressed=false;
  }
  

  if (!digitalRead(BUTT_RIGHT))
  {
    if (!right_pressed)
    {
      right_pressed=true;
      right_last=millis();
    }
    else //being held down
    {
      if (((int)millis() - right_last) > DEBOUNCE_DELAY)
      {
        right_last=millis() + REPEAT_DELAY;
        right_handler();
      }
    }
  }
  else
  {
    right_pressed=false;
  }


  if (!digitalRead(BUTT_UP))
  {
    if (!up_pressed)
    {
      up_pressed=true;
      up_last=millis();
    }
    else //being held down
    {
      if (((int)millis() - up_last) > DEBOUNCE_DELAY)
      {
        up_last=millis() + REPEAT_DELAY;
        up_handler();
      }
    }
  }
  else
  {
    up_pressed=false;
  }


  if (!digitalRead(BUTT_DOWN))
  {
    if (!down_pressed)
    {
      down_pressed=true;
      down_last=millis();
    }
    else //being held down
    {
      if (((int)millis() - down_last) > DEBOUNCE_DELAY)
      {
        down_last=millis() + REPEAT_DELAY;
        down_handler();
      }
    }
  }
  else
  {
    down_pressed=false;
  }


}

//DO NOT call this in a tight loop, you will wear out the flash.
void write_settings()
{
    EEPROM.put(0,currentsettings);
    EEPROM.commit();
}

void configure_leds()
{
  byte brightness = currentsettings.brightness;
  Ledmode led_mode = currentsettings.led_mode;
  switch (led_mode) {
    case BOTH_ON:
      analogWrite(BOTTOM_LED_PIN, brightness);
      analogWrite(TOP_LED_PIN, brightness);
      break;
    case TOP_ON:
      analogWrite(BOTTOM_LED_PIN, 255);
      analogWrite(TOP_LED_PIN, brightness);
      break;
    case BOTTOM_ON:
      analogWrite(BOTTOM_LED_PIN, brightness);
      analogWrite(TOP_LED_PIN, 255);
      break;
    case OFF:
      analogWrite(BOTTOM_LED_PIN, 255);
      analogWrite(TOP_LED_PIN, 255);
      break;
  } //end switch led_mode
} //end configure_leds

void fade_led(int pin, int& brightness, bool& fading_in)
{
  int fade_speed_scaled;

  fade_speed_scaled = static_cast<float>(fade_speed) * (255.0 - static_cast<float>(currentsettings.brightness)) / 255.0;
  if (fade_speed_scaled == 0)
    fade_speed_scaled=1;
  //Serial.println(fade_speed_scaled);

  
  if (fading_in)
  {
    brightness -= fade_speed_scaled;
    if (brightness <= currentsettings.brightness)
    {
      brightness = currentsettings.brightness;
      fading_in = false;
    }
  }
  else
  {
    brightness += fade_speed_scaled;
    if (brightness >= 255)
    {
      brightness = 255;
      fading_in = true;
    } 
  }

  analogWrite(pin, brightness);
}

void setupOledScreens(){ //setup OLED screens, and detect any issues
  int rcleft = false;
  int rcright = false;
  //Serial.print("OLED startup\n");
  byte oledinitattempts = 0; //oleds aren't init'd propertly on first attempt, ignore the first init attempt. Plus the first time we check left, right can't have been init'd //TODO test with one screen physically connected
  do{
    Serial.print("OLED init ");
    Serial.println(oledinitattempts);
    rcleft = oledInit(&ssoled, OLEDRESOLUTION, OLED_ADDR_LEFT, FLIP180, INVERT, 1, SDA_PIN, SCL_PIN, RESET_PIN, 1600000L); // use I2C bus at 1600Khz
  
    if(rcleft == OLED_SH1106_3C){ //oled init'd correctly
      oledSetBackBuffer(&ssoled, zerobuffer); //set buffer
      oledSetContrast(&ssoled, 255); //max brightness
      oledFill(&ssoled, 0, 1);//cls      
      Serial.println("OLED success");
    }else if(oledinitattempts > 0){
      flashLEDs();
    }
    if(oledinitattempts < 100){
      oledinitattempts++; //increment counter, don't let it roll over
    }
  }while(rcleft != OLED_SH1106_3C);
  Serial.println("OLED Exit");
}//setupOledScreens()

void flashLEDs(){ //don't flash for too long, this is hit once after reflash of the code. But a constant error will execute this multiple times and cause continuous flashing which we want.
  for(int i = 0; i < 3; i++){
    delay(50);
    digitalWrite(LED_BUILTIN, LOW);// led on 
    delay(50);
    digitalWrite(LED_BUILTIN, HIGH); // led off

  }
}

void clear_screen()
{
  oledFill(&ssoled, 0, 1);
}

void clear_screen_fancy()
{
  for(byte r=0;r<96;r+=5)
  {
      oledEllipse(&ssoled, 63, 31, r, r, 0, 1);
      oledDumpBuffer(&ssoled, NULL);     
  }
  //oledDumpBuffer(&ssoled, zerobuffer);
}

void invert_screen()
{
  for(byte x=0;x<8;x++)
  {//invert all pixels
    for(byte y=0;y<128;y++)
    {
      zerobuffer[(x*128)+y] = ~zerobuffer[(x*128)+y];
    }
  }
  oledDumpBuffer(&ssoled, zerobuffer);//show inverted logo 
}


bool is_word_safe(const char* word) {
  int tile_counts[26] = {
    4, 1, 1, 2, 5, 1, 1, 3, 3, 1, 1, 2, 2, 3, 4, 1, 1, 2, 3, 4, 2, 1, 1, 1, 1, 1
  };
  
  int word_length = strlen(word);
  for (int i = 0; i < word_length; i++) {
    char letter = toupper(word[i]) - 'A';
    if (letter < 0 || letter >= 26 || tile_counts[letter] == 0) {
      return false;
    }
    tile_counts[letter]--;
  }
  
  return true;
}


char shortstring[5]; //used to convert values for oled display
char* shorttostring(short input)
{ //used to convert shorts to char* for oled display
  snprintf(shortstring, 5, "%d", input);
  return shortstring;
}

char longstring[20];
char* longtostring(long input)
{
  snprintf(longstring, 20, "%d", input);
  return longstring;
}


void pad_line(char* input, short newLen)
{
  short len = strlen(input);
  
  if (len >= newLen) {
    // The string is already longer or equal to the desired length
    return;
  }
  short paddingLen = newLen - len;
  snprintf(input + len, paddingLen + 1, "%*s", (int)paddingLen, "");
}


void draw_menu(const char ** menu, short len, short selected)
{
  for (short y = 0; y < len; y++)
  {
    if (y==selected)
    {
      oledWriteString(&ssoled, 0, 0, y, (char*)">", FONT_SMALL, 0, 1);
    }
    else
    {
      oledWriteString(&ssoled, 0, 0, y, (char*)" ", FONT_SMALL, 0, 1);
    }
    
    oledWriteString(&ssoled, 0, 6, y, (char *)(menu[y]), FONT_SMALL, 0, 1);
  }
}

void show_image(const char * filename)
{
  uint8_t imagebuffer[1024];
  File image;
  image=LittleFS.open(filename,"r");
  if (!image)
  {
    Serial.println("Image File open fail");
    fs_fail=true;
    Serial.println(filename);
    oledWriteString(&ssoled, 0, 0, 4, (char*)"Image File open fail", FONT_SMALL, 0, 1);
    flashLEDs();
    return;
  } 
  image.readBytes((char*)imagebuffer, 1024);
  //display startup screen logo on screen
  oledDumpBuffer(&ssoled, imagebuffer);
  image.close();
}


void remove_asterisks(char* str) {
    if (str == NULL)
        return;

    char* dest = str;
    while (*str != '\0') {
        if (*str != '*') {
            *dest = *str;
            dest++;
        }
        str++;
    }
    *dest = '\0';
}

void replace_asterisks(char* str) {
  if (str == NULL) {
    return;  // Check for NULL string
  }
  
  for (int i = 0; str[i] != '\0'; i++) {
    if (str[i] == '*') {
      str[i] = ' ';  // Replace asterisk with space
    }
  }
}


// Mark a number as used
void mark_used(uint16_t number)
{
  int index = number / 8;
  int bit = number % 8;
  bitmap[index] |= (1 << bit);
}

// Check if a number is used
bool is_used(uint16_t number)
{
  int index = number / 8;
  int bit = number % 8;
  return (bitmap[index] & (1 << bit)) != 0;
}

// Clear the bitmap and mark all numbers as unused
void clear_bitmap()
{
  memset(bitmap, 0, sizeof(bitmap));
}

// Save the bitmap to the file
void save_bitmap()
{
  File file = LittleFS.open(bitmap_file_name, "w");
  if (file)
  {
    file.write(bitmap, bitmap_size);
    file.close();
  }
}

// Load the bitmap from the file
void load_bitmap()
{
  File file = LittleFS.open(bitmap_file_name, "r");
  if (file)
  {
    file.read(bitmap, bitmap_size);
    file.close();
  }
  else
  {
    // File doesn't exist, initialize the bitmap to zeros
    clear_bitmap();
  }
}

void scramble_string(char* str, uint8_t parameter)
{
   int length = strlen(str);
   if (parameter == 0 || length <= 1) {
        return;
    }

    for (int i = 0; i < length - 1; i++) {
        int j = random(i + 1, length);

        if (random(256) < parameter) {
            char temp = str[i];
            str[i] = str[j];
            str[j] = temp;
        }
    }
}

void scramble_strings(char** strings, int num_strings, uint8_t parameter)
{
    for (int i = 0; i < num_strings; i++) {
        char* str = strings[i];
        int length = strlen(str);

        if (parameter == 0 || length <= 1) {
            continue;
        }

        for (int j = 0; j < length - 1; j++) {
            int k = rand() % (length - j - 1) + j + 1;

            if (rand() % 256 < parameter) {
                char temp = str[j];
                str[j] = str[k];
                str[k] = temp;
            }
        }
    }
}


char** copy_string_array(const char** array, int size)
{
    char** copy = (char**)malloc(size * sizeof(char*));

    if (copy == NULL) {
        fprintf(stderr, "Memory allocation failed\n");
        exit(1);
    }

    for (int i = 0; i < size; i++) {
        const char* str = array[i];
        int length = strlen(str);
        copy[i] = (char*)malloc((length + 1) * sizeof(char));

        if (copy[i] == NULL) {
            fprintf(stderr, "Memory allocation failed\n");
            exit(1);
        }

        strcpy(copy[i], str);
    }

    return copy;
}

void factory_reset()
{
    Serial.println("Setting up EEPROM first boot");
    currentsettings.init=true;
    currentsettings.brightness=239;
    currentsettings.snurdle_score=0;
    currentsettings.settings_version = SETTINGS_VERSION;
    currentsettings.led_mode = BOTH_ON;
    currentsettings.last_snurdle_word=1;
    currentsettings.battle_top_highscore=0;
    currentsettings.battle_bottom_highscore=0;
    EEPROM.put(0,currentsettings);
    EEPROM.commit();
    clear_bitmap();
    save_bitmap();
}


void tracker_init(WordTracker* tracker) {
  memset(tracker->player1_words, 0, sizeof(tracker->player1_words));
  memset(tracker->player2_words, 0, sizeof(tracker->player2_words));
  tracker->count = 0;
}


bool has_been_played(const WordTracker* tracker, const char* theword, int player) 
{
  const char (*words)[MAX_LENGTH];

  if (player == 1) 
  {
    words = tracker->player1_words;
  }
  else
  {
    if (player == 2) 
    {
      words = tracker->player2_words;
    } 
    else 
    {
      return false;  // Invalid player number
    }
  }
  
  for (int i = 0; i < tracker->count; i++) 
  {
    if (strcmp(words[i], theword) == 0) 
    {
      return true;
    }
  }

  return false;
}


void insert_played_word(WordTracker* tracker, const char* theword, int player) 
{
  if (tracker->count == MAX_PLAYED_WORDS) {
    // Remove the oldest word from the list
    memmove(tracker->player1_words, tracker->player1_words + 1, sizeof(tracker->player1_words) - sizeof(tracker->player1_words[0]));
    memmove(tracker->player2_words, tracker->player2_words + 1, sizeof(tracker->player2_words) - sizeof(tracker->player2_words[0]));
    tracker->count--;
  }

  if (player == 1) 
  {
    strcpy(tracker->player1_words[tracker->count], theword);
  } 
  else 
  {
    if (player == 2) 
    {
      strcpy(tracker->player2_words[tracker->count], theword);
    }
  }
  
  tracker->count++;
}
