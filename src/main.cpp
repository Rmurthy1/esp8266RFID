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
int _scale = 100;

// indicator led
const int ledPin = D8;

int period = 20;
unsigned long time_now = 0;
long scaleValue = 0;

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

unsigned extract_tag();
long hexstr_to_value(char *str, unsigned int length);
long getValue();
void prepareScale();
float readWeight();
void useExtractedTag();
void readRFIDSerialValue();
 
void setup()
{
  pinMode(ledPin, OUTPUT);
  // scale setup
  network.setup();
  prepareScale();

  // serial setup
  Serial.begin(9600);
  ssrfid.begin(9600);
  ssrfid.listen(); 
  
  while (lcd.begin(COLUMS, ROWS, LCD_5x8DOTS, 4, 5, 400000, 250) != 1) //colums, rows, characters size, SDA, SCL, I2C speed in Hz, I2C stretch time in usec 
  {
    Serial.println(F("PCF8574 is not connected or lcd pins declaration is wrong. Only pins numbers: 4,5,6,16,11,12,13,14 are legal."));
    delay(5000);
  }

  lcd.print(F("PCF8574 is OK...")); //(F()) saves string to flash & keeps dynamic memory free

  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print("Ready to count cards");
  lcd.setCursor(0, 1);
  lcd.print("1. take some cards");
  lcd.setCursor(0, 2);
  lcd.print("2. place box here");
  delay(2000);
}

unsigned long timeSinceLastSerialReadFromRM6300 = 0;
bool hasReadStarted = false;
bool hasReadEnded = false;
unsigned lastTag = 0;
// rfid input debounce
unsigned long lastDebounceTime = 0;  // the last time the output pin was toggled
unsigned long debounceDelay = 1000;    // the debounce time; increase if the output flickers

bool isTagOk = false; // used to determine if the tag is valid (in useExtractedTag()), resets after each tag read. mutates in extract_tag()
bool firstReadOfSeries = false; // used to determine if the first read of a series has been made, resets after 2 seconds of no serial reads
unsigned long timeOfFirstRead = 0;
bool doneReadingDueToTimeExpiring = false;
bool initialRead = false; // used to determine if the first read has been made, doesn't reset

unsigned uploadTag = 0;
int uploadWeight = 0;
//bool readyToUpload = false;

// led blink stuff
bool readyToRead = true;

bool startUpload = false;
bool uploadOnce = false; // do i really need this guy? or can i get away with startUpload?

void loop() {

  if (network.isConnected() && initialRead == false) {
    lcd.setCursor(9, 3);
    lcd.print("wifi on");
  }

  if (hasReadStarted == true) {
    readyToRead = false;
    if (millis() - timeSinceLastSerialReadFromRM6300 > debounceDelay) { // the big reset happens here
      Serial.println("inside of debounce delay for rfid, this should be called once data is done being sent from the rfid chip"); // called once
      hasReadStarted = false;
      hasReadEnded = true;
    }
    if (firstReadOfSeries == false) {
      firstReadOfSeries = true;
      timeOfFirstRead = millis();
      initialRead = true;
      startUpload = false;
      lcd.clear();
    }
    if (doneReadingDueToTimeExpiring == true) {
      /*Serial.println("read ended due to the two second passing, data will continue to come from the chip"); // called many times
      lcd.setCursor(0, 0);
      lcd.print("                    ");*/ 
      // this is where we should prepare for upload, we have the tag and the weight.
      // just need to set a variable once and then we can upload
      if (startUpload == false) {
        startUpload = true;
        uploadOnce = true;
      }
    }
  }

  if (startUpload == true) {
    //Serial.println("upload test ");
    if (uploadOnce == true) {
      Serial.println("upload once");
      uploadOnce = false;
      lcd.setCursor(0, 2);
      lcd.print("uploading ");
      lcd.print(uploadTag);
    }
  }

  if (hasReadEnded == true) {
    //useExtractedTag(); // not sure why i had this here, it happens too late i think
    hasReadEnded = false;
    lastTag = 0; // unused?
    Serial.println("Read ended, ready for the next one");
    firstReadOfSeries = false;
    lcd.setCursor(0, 0);
    lcd.print("Ready to read tag");
    lcd.setCursor(0, 1);
    lcd.print("                  ");
    lcd.setCursor(0, 2);
    lcd.print("                  ");
    readyToRead = true;
  }
  digitalWrite(ledPin, ((readyToRead == true) /*&& millis() % 1000 < 500*/ ));
  readRFIDSerialValue();
  // always update the scale value
  
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

/*these are copilot comments, i just wanted to see what all the hype is about*/
// this function is called when a tag has been read
// it extracts the tag from the buffer and checks if the tag is valid
// if the tag is valid, it will print the tag on the lcd
// if the tag is not valid, it will not print the tag on the lcd
// if the tag is valid, it will stop reading tags after 2 seconds
// if the tag is not valid, it will stop reading tags immediately
// if the tag is valid, it will store the tag and weight and prepare for upload
// if the tag is not valid, it will not store the tag and weight and will not prepare for upload
void useExtractedTag() { // called from readRFIDSerialValue()
  unsigned tag = extract_tag();
  if (isTagOk == false) {
    return;
  }

  // after 2 seconds, we stop storing tags. 
  if ((millis() - timeOfFirstRead > 2000) /*&& (tag != lastTag)*/) {
    Serial.println("2 seconds have passed, stopping reading tags");
    lcd.setCursor(0, 1);
    lcd.print("Please remove box.");
    doneReadingDueToTimeExpiring = true;
    return;
  } 
  /*lcd.setCursor(0, 2);
  lcd.print("                 ");  
  lcd.setCursor(0, 2);
  lcd.print(tag);*/
  if (tag == 10622595) {
    _offset = getValue();
   
  } else { // if its any other card
    lcd.setCursor(0, 3);
    lcd.print("                  ");
    lcd.setCursor(8, 3);
    lcd.print(scaleValue);
    lcd.print(" g");
  // store weight and tag and prepare for upload
  uploadTag = tag;
  uploadWeight = scaleValue;
  //readyToUpload = true;

  }
}


// called in loop, reads the serial value from the RFID reader
// this mutates the buffer and buffer_index, timeSinceLastSerialReadFromRM6300, hasReadStarted
void readRFIDSerialValue() {
  
  if (ssrfid.available() > 0){
    hasReadStarted = true;
    timeSinceLastSerialReadFromRM6300 = millis();

    bool call_extract_tag = false;
    //Serial.println("Data available");
    int ssvalue = ssrfid.read(); // read
    //Serial.println(ssvalue); 
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
        useExtractedTag();
      } else { // something is wrong... start again looking for preamble (value: 2)
        buffer_index = 0;
        return;
      }
    }
  }
}

// extracts the tag from the buffer, called in useExtractedTag()
// this mutates the buffer, buffer_index, isTagOk, and returns the tag
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
      isTagOk = true;
    } else {
      Serial.print(" (NOT OK)"); // checksums do not match
      isTagOk = false;
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