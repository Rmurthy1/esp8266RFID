#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <ESP8266WiFi.h>
#include <SoftwareSerial.h>
#include "networking.h"


#define COLUMS           20   //LCD columns
#define ROWS             4    //LCD rows
#define LCD_SPACE_SYMBOL 0x20 //space symbol from LCD ROM, see p.9 of GDM2004D datasheet

// lcd plugged into D1,D2 (slc,sda)

// rdm630/rdm6300 RFID reader plugged into D5,D6 (tx,rx)

LiquidCrystal_I2C lcd(PCF8574_ADDR_A21_A11_A01, 4, 5, 6, 16, 11, 12, 13, 14, POSITIVE);
// tx = D5,gpio14
// rx = D6,gpio12

// scale setup
const int _DoutPin = D7;
const int _SckPin = D3;       
long _offset = 0; // tare value
int _scale = 500;

// sound setup
const int _buzzerPin = D8;

int period = 20;
unsigned long time_now = 0;

// networking class
Networking network;

const int BUFFER_SIZE = 14; // RFID DATA FRAME FORMAT: 1byte head (value: 2), 10byte data (2byte version + 8byte tag), 2byte checksum, 1byte tail (value: 3)
const int DATA_SIZE = 10; // 10byte data (2byte version + 8byte tag)
const int DATA_VERSION_SIZE = 2; // 2byte version (actual meaning of these two bytes may vary)
const int DATA_TAG_SIZE = 8; // 8byte tag
const int CHECKSUM_SIZE = 2; // 2byte checksum

SoftwareSerial ssrfid = SoftwareSerial(D5,D6); // RX, TX

char buffer[BUFFER_SIZE]; // used to store an incoming data frame 
int buffer_index = 0;

// rfid input debounce
unsigned long lastDebounceTime = 0;  // the last time the output pin was toggled
unsigned long debounceDelay = 50;    // the debounce time; increase if the output flickers


unsigned extract_tag();
long hexstr_to_value(char *str, unsigned int length);
long getValue();
void prepareScale();
float readWeight();
 


void setup()
{
  pinMode(_buzzerPin, OUTPUT);
  /*
  WiFi.persistent(false);           //disable saving wifi config into SDK flash area
  WiFi.forceSleepBegin();           //disable AP & station by calling "WiFi.mode(WIFI_OFF)" & put modem to sleep

  Serial.begin(115200);
  */

  // scale setup
  network.setup();
  prepareScale();



  // serial setup
  Serial.begin(9600);
  ssrfid.begin(9600);
  ssrfid.listen(); 
 
  Serial.println("INIT DONE");
 
  while (lcd.begin(COLUMS, ROWS, LCD_5x8DOTS, 4, 5, 400000, 250) != 1) //colums, rows, characters size, SDA, SCL, I2C speed in Hz, I2C stretch time in usec 
  {
    Serial.println(F("PCF8574 is not connected or lcd pins declaration is wrong. Only pins numbers: 4,5,6,16,11,12,13,14 are legal."));
    delay(5000);
  }

  lcd.print(F("PCF8574 is OK...")); //(F()) saves string to flash & keeps dynamic memory free
  delay(2000);

  lcd.clear();

  /* prints static text */
}
/*
void loop()
{
  lcd.setCursor(14, 2);             //set 15-th colum & 3-rd  row, 1-st colum & row started at zero
  lcd.print(random(10, 1000));
  lcd.write(LCD_SPACE_SYMBOL);

  delay(1000);
}
*/

int counter = 0;
void loop() {
  if (ssrfid.available() > 0){
    bool call_extract_tag = false;
    Serial.println("Data available");
    int ssvalue = ssrfid.read(); // read
    Serial.println(ssvalue); 
    if (ssvalue == -1) { // no data was read
      Serial.println("Error: No data was read!");
      return;
    }

    if (ssvalue == 2) { // RDM630/RDM6300 found a tag => tag incoming 
      buffer_index = 0;
    } else if (ssvalue == 3) { // tag has been fully transmitted       
      call_extract_tag = true; // extract tag at the end of the function call
    }

    if (buffer_index >= BUFFER_SIZE) { // checking for a buffer overflow (It's very unlikely that an buffer overflow comes up!)
      Serial.println("Error: Buffer overflow detected!");
      return;
    }
    
    buffer[buffer_index++] = ssvalue; // everything is alright => copy current value to buffer

    if (call_extract_tag == true) {
      if (buffer_index == BUFFER_SIZE) {
        unsigned tag = extract_tag();
        lcd.setCursor(0, 2);  
        lcd.print(tag);
        if (tag == 10622595) {
          _offset = getValue();
          counter = counter + 1;
          //tone(_buzzerPin, 1000, 2000);
          lcd.setCursor(0, 3);
          lcd.print("        counter: ");
          lcd.print(counter);
        } else { // if its any other card
          noTone(_buzzerPin);
        }
      } else { // something is wrong... start again looking for preamble (value: 2)
        buffer_index = 0;
        return;
      }
    }    
  }
  long scaleValue;
  if (millis() > time_now + period) {
    time_now = millis();
    scaleValue = -1 * readWeight();
    lcd.setCursor(0, 3);
    lcd.print("        ");
    lcd.setCursor(0, 3);
    lcd.print(scaleValue);
    lcd.print(" g");
  }    
}

unsigned extract_tag() {
    char msg_head = buffer[0];
    char *msg_data = buffer + 1; // 10 byte => data contains 2byte version + 8byte tag
    char *msg_data_version = msg_data;
    char *msg_data_tag = msg_data + 2;
    char *msg_checksum = buffer + 11; // 2 byte
    char msg_tail = buffer[13];

    // print message that was sent from RDM630/RDM6300
    Serial.println("--------");

    Serial.print("Message-Head: ");
    Serial.println(msg_head);

    Serial.println("Message-Data (HEX): ");
    for (int i = 0; i < DATA_VERSION_SIZE; ++i) {
      Serial.print(char(msg_data_version[i]));
    }
    Serial.println(" (version)");
    for (int i = 0; i < DATA_TAG_SIZE; ++i) {
      Serial.print(char(msg_data_tag[i]));
    }
    Serial.println(" (tag)");

    Serial.print("Message-Checksum (HEX): ");
    for (int i = 0; i < CHECKSUM_SIZE; ++i) {
      Serial.print(char(msg_checksum[i]));
    }
    Serial.println("");

    Serial.print("Message-Tail: ");
    Serial.println(msg_tail);

    Serial.println("--");

    long tag = hexstr_to_value(msg_data_tag, DATA_TAG_SIZE);
    Serial.print("Extracted Tag: ");
    Serial.println(tag);

    long checksum = 0;
    for (int i = 0; i < DATA_SIZE; i+= CHECKSUM_SIZE) {
      long val = hexstr_to_value(msg_data + i, CHECKSUM_SIZE);
      checksum ^= val;
    }
    Serial.print("Extracted Checksum (HEX): ");
    Serial.print(checksum, HEX);
    if (checksum == hexstr_to_value(msg_checksum, CHECKSUM_SIZE)) { // compare calculated checksum to retrieved checksum
      Serial.print(" (OK)"); // calculated checksum corresponds to transmitted checksum!
    } else {
      Serial.print(" (NOT OK)"); // checksums do not match
    }

    Serial.println("");
    Serial.println("--------");

    return tag;
}

long hexstr_to_value(char *str, unsigned int length) { // converts a hexadecimal value (encoded as ASCII string) to a numeric value
  char* copy = (char*)malloc((sizeof(char) * length) + 1); 
  memcpy(copy, str, sizeof(char) * length);
  copy[length] = '\0'; 
  // the variable "copy" is a copy of the parameter "str". "copy" has an additional '\0' element to make sure that "str" is null-terminated.
  long value = strtol(copy, NULL, 16);  // strtol converts a null-terminated string to a long value
  free(copy); // clean up 
  return value;
}

float readWeight() {
  long val = (getValue() - _offset);
  return (float) val / _scale;
}

long getValue() {
  uint8_t data[3];
    long ret;
    while (digitalRead(_DoutPin)); // wait until _dout is low
    for (uint8_t j = 0; j < 3; j++)
    {
        for (uint8_t i = 0; i < 8; i++)
        {
            digitalWrite(_SckPin, HIGH);
            bitWrite(data[2 - j], 7 - i, digitalRead(_DoutPin));
            digitalWrite(_SckPin, LOW);
        }
    }

    digitalWrite(_SckPin, HIGH);
    digitalWrite(_SckPin, LOW);
    ret = (((long) data[2] << 16) | ((long) data[1] << 8) | (long) data[0])^0x800000;
    return ret;
}

void prepareScale() {
    pinMode(_SckPin, OUTPUT);
    pinMode(_DoutPin, INPUT);

    digitalWrite(_SckPin, HIGH);
    delayMicroseconds(100);
    digitalWrite(_SckPin, LOW);
    _offset = getValue();
}