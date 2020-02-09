#include <SPI.h>
#include <MFRC522.h>
#include <require_cpp11.h>
#include <Key.h>
#include <Keypad.h>
#include <LiquidCrystal.h>

#include <pt.h>
#include <string.h>

// Display
const int rs = 40, en = 41, d4 = 49, d5 = 47, d6 = 45, d7 = 43;
LiquidCrystal lcd(rs, en, d4, d5, d6, d7);
// Button
const int btn_pin = 2;
// Membrane switch module
const int ROWS = 4;
const int COLUMNS = 4;
byte row_pins[] = {22, 23, 25, 27};
byte column_pins[] = {29, 31, 33, 35};
char keys[ROWS][COLUMNS] = {
  {'1','2','3','A'},
  {'4','5','6','B'},
  {'7','8','9','C'},
  {'*','0','#','D'}
};

Keypad keypad = Keypad( makeKeymap(keys), row_pins, column_pins, ROWS, COLUMNS );
// RC522 RFID
const int sda = 53, rst = 30;
MFRC522 mfrc522(sda, rst);
MFRC522::MIFARE_Key rfid_key;
static const byte rfid_sector = 1;
static const byte rfid_block_addr = 4;
static const byte rfid_trailer_block = 7;
MFRC522::StatusCode status;
// States
enum general_states{JUST_STARTED = 0, SETTINGS, WORK, INVALID};
enum keypad_states{READY = 0, ENTER, ENTERING_NEW_PASS, AUTH, CHECK_PASS, INVALID_ST};
enum rfid_states{READ = 0, WRITE, RF_INVALID};
// Threads. Even though everything can be done in one rfid thread,
// for training purposes let's use one for writing and one for reading
static struct pt btn_pt, rfid_write_pt, rfid_read_pt, keypad_pt;
// Globals
enum general_states general_state = JUST_STARTED;
enum keypad_states keypad_state = READY;
enum rfid_states rfid_state = READ;
static const byte MAX_PASS_LENGTH = 18;
static char password[MAX_PASS_LENGTH];
static char user_password[MAX_PASS_LENGTH];
static byte counter = 0;
static byte comparison_result = 0;
byte rfid_buffer[MAX_PASS_LENGTH] = {};
// Flags
static bool btn_pressed = false;
static bool key_pressed = false;
static bool can_read_rfid = true;


static void print_on_display(const char * const string_to_show, bool need_clear)
{
  if (need_clear){
    lcd.clear();
    lcd.setCursor(0, 0);
  }
  
  lcd.print(string_to_show);
}

static PT_THREAD(btn_thread(struct pt *pt))
{
  static unsigned long timestamp = 0;
  static const byte interval = 100;
  static bool was_btn_high = false;
  static byte btn_state = 0;
  
  PT_BEGIN(pt);
  
  while(true) {
    // Only on button up
    if (was_btn_high && btn_state != HIGH) {
      if (general_state == JUST_STARTED || general_state == WORK) {
        general_state = SETTINGS;
        print_on_display("Settings", true);
        lcd.setCursor(0, 1);
        print_on_display("Enter new pass", false);
        btn_pressed = true;
        Serial.println("Settings");
        PT_WAIT_UNTIL(pt, !btn_pressed);
      } else {
        Serial.println("Changing back to work");
        general_state = WORK;
        keypad_state = READY;
        print_on_display("Door is closed", true);
        lcd.setCursor(0, 1);
        print_on_display("Use pass or key", false);
      }
      was_btn_high = false;
    }
    
    PT_WAIT_UNTIL(pt, millis() - timestamp > interval);
    timestamp = millis();
    btn_state = digitalRead(btn_pin);

    if (btn_state == HIGH)
    {
      was_btn_high = true;
    }
  }
  
  PT_END(pt);
}

static PT_THREAD(rfid_write_thread(struct pt *pt))
{
  static unsigned long timestamp = 0;
  static const int interval = 100;
  
  PT_BEGIN(pt);
  while(true) {
    PT_WAIT_UNTIL(pt, !can_read_rfid);

    if ( mfrc522.PICC_IsNewCardPresent() && rfid_state == WRITE && general_state == SETTINGS) {
      print_on_display("Key presented.", true);
      // Debug info
      mfrc522.PICC_ReadCardSerial();
      Serial.print(F("Card UID:"));
      dump_byte_array(mfrc522.uid.uidByte, mfrc522.uid.size);
      Serial.println();
      Serial.print(F("PICC type: "));
      MFRC522::PICC_Type piccType = mfrc522.PICC_GetType(mfrc522.uid.sak);
      Serial.println(mfrc522.PICC_GetTypeName(piccType));      
      // Debug info ends

      Serial.println(F("Authenticating using key B..."));
      status = (MFRC522::StatusCode) mfrc522.PCD_Authenticate(MFRC522::PICC_CMD_MF_AUTH_KEY_B, rfid_trailer_block, &rfid_key, &(mfrc522.uid));
      if (status != MFRC522::STATUS_OK) {
        Serial.print(F("PCD_Authenticate() failed: "));
        Serial.println(mfrc522.GetStatusCodeName(status));
      }
      
      // Write block
      status = mfrc522.MIFARE_Write(rfid_block_addr, (byte *)password, 16);
      if (status != MFRC522::STATUS_OK) {
        Serial.print(F("MIFARE_Write() failed: "));
        Serial.println(mfrc522.GetStatusCodeName(status));
      } else {
        Serial.println("Success write operation");
        print_on_display("Data ready", true);
        rfid_state = READ;

        mfrc522.PICC_DumpMifareClassicSectorToSerial(&(mfrc522.uid), &rfid_key, rfid_sector);
        
        mfrc522.PICC_HaltA(); // Halt PICC
        mfrc522.PCD_StopCrypto1();  // Stop encryption on PCD
        // May be give a chance to restore. TODO: Fix me
        btn_pressed = false;
        can_read_rfid = true;
      }
    }
    
    PT_WAIT_UNTIL(pt, millis() - timestamp > interval);
    timestamp = millis();
  }
  PT_END(pt);
}

static PT_THREAD(rfid_read_thread(struct pt *pt))
{
  static unsigned long timestamp = 0;
  static const int interval = 100;
  static byte rfid_buff_size = sizeof(rfid_buffer);
  
  PT_BEGIN(pt);
  while(true) {
    PT_WAIT_UNTIL(pt, can_read_rfid);

    // Contains some debug information too. It is assumed for this play-ground demo that we always
    // are using the standard cards: MFRC522::PICC_TYPE_MIFARE_MINI, MFRC522::PICC_TYPE_MIFARE_1K or
    // MFRC522::PICC_TYPE_MIFARE_4K
    if (mfrc522.PICC_IsNewCardPresent() && rfid_state == READ && general_state == WORK) {
      // Debug info
      mfrc522.PICC_ReadCardSerial();
      Serial.print(F("Card UID:"));
      dump_byte_array(mfrc522.uid.uidByte, mfrc522.uid.size);
      Serial.println();
      Serial.print(F("PICC type: "));
      MFRC522::PICC_Type piccType = mfrc522.PICC_GetType(mfrc522.uid.sak);
      Serial.println(mfrc522.PICC_GetTypeName(piccType));      
      // Debug info ends

      Serial.println(F("Authenticating using key A..."));
      status = (MFRC522::StatusCode) mfrc522.PCD_Authenticate(MFRC522::PICC_CMD_MF_AUTH_KEY_A, rfid_trailer_block, &rfid_key, &(mfrc522.uid));
      if (status != MFRC522::STATUS_OK) {
        Serial.print(F("PCD_Authenticate() failed: "));
        Serial.println(mfrc522.GetStatusCodeName(status));
      }
      
      status = mfrc522.MIFARE_Read(rfid_block_addr, rfid_buffer, &rfid_buff_size);
      if (status != MFRC522::STATUS_OK) {
        Serial.print(F("MIFARE_Read() failed: "));
        Serial.println(mfrc522.GetStatusCodeName(status));
      }
      Serial.print(F("Data in block ")); Serial.print(rfid_block_addr); Serial.println(F(":"));
      dump_byte_array(rfid_buffer, rfid_buff_size); Serial.println();
      Serial.println();

      mfrc522.PICC_HaltA();
      mfrc522.PCD_StopCrypto1();
      if (compare_readings(rfid_buffer)) {
        Serial.println("Come in!");
        print_on_display("Come in!", true); 
      } else {        
        Serial.println("Wrong pass!");
        print_on_display("Wrong pass!", true);
      }
    }
        
    PT_WAIT_UNTIL(pt, millis() - timestamp > interval);
    timestamp = millis();
  }
  PT_END(pt);
}

static PT_THREAD(membrane_keypad_thread(struct pt *pt))
{
  static unsigned long timestamp = 0;
  static const byte interval = 100;
    
  PT_BEGIN(pt);
  while(true) {
    if (general_state == SETTINGS && keypad_state == READY) {
      // We need to wait before updating instructions
      
      PT_WAIT_UNTIL(pt, btn_pressed);
      Serial.println("Keypad is ready for new password");

      keypad_state = ENTER;
    } else {
      keypad.getKey();
      timestamp = millis();
      PT_WAIT_UNTIL(pt, millis() - timestamp > interval);
      
      if (keypad_state == CHECK_PASS) {
        if (!comparison_result) {
          print_on_display("Come in!", true);
        } else {
          print_on_display("Wrong pass!", true);  
        }
        keypad_state = READY;
      }
    }
  }
  PT_END(pt);
}

void keypadEvent(KeypadEvent key){

  switch (keypad.getState()){
  case PRESSED:
    if (keypad_state == ENTER || keypad_state == ENTERING_NEW_PASS) {
      if (key == 'A') {

        print_on_display("Password:", true);
        lcd.setCursor(0, 1);
      
        keypad_state = ENTERING_NEW_PASS;
        counter = 0;
        Serial.println("Entering new password");
      } else if (key == 'D') {
        password[counter] = '\0';        
        counter = 0;
        Serial.println("Password entered. Ready.");
        Serial.println(password);
        btn_pressed = false;
      }
    }
      break;

  case RELEASED:
    if (keypad_state == ENTERING_NEW_PASS) {
      if (key >= '0' && key <= '9') {
        lcd.setCursor(counter, 1);
        password[counter++] = key;
        print_on_display("*", false);
      }
    } else if ((keypad_state == READY || keypad_state == AUTH) && general_state == WORK) {
      if (keypad_state != AUTH) {  
        print_on_display("Enter password:", true);
        keypad_state = AUTH;
      }
      
      if (key >= '0' && key <= '9') {
        lcd.setCursor(counter, 1);
        user_password[counter++] = key;
        print_on_display("*", false);
      }
      
      if (key == '#') {
          user_password[counter++] = '\0';
          Serial.println(user_password);
          Serial.println(password);
          comparison_result = memcmp(user_password, password, sizeof(user_password));
          Serial.println(comparison_result);
          keypad_state = CHECK_PASS;
          counter = 0;
      }
      key_pressed = true;
    }

  case HOLD:
    if (general_state == SETTINGS && rfid_state == READ) {
      if (key == 'C' && keypad_state == ENTER) {
        print_on_display("Key changing", true);
        lcd.setCursor(0, 1);
        print_on_display("Wait a bit", false);
        rfid_state = WRITE;
        can_read_rfid = false;
      }
    }
    break;
  }
}

void setup() {
  // put your setup code here, to run once:
  // Threads
  PT_INIT(&btn_pt);
  PT_INIT(&rfid_write_pt);
  PT_INIT(&rfid_read_pt);
  PT_INIT(&keypad_pt);
  
  // Devices  
  Serial.begin(9600);
  Serial.println("Ready");
  
  lcd.begin(16, 2);
  print_on_display("Initializing...", true);  
  
  keypad.addEventListener(keypadEvent);
  
  lcd.setCursor(0, 1);
  print_on_display("Press button...", false);

  SPI.begin();
  mfrc522.PCD_Init();
  for (byte i = 0; i < 6; i++) rfid_key.keyByte[i] = 0xFF;
  dump_byte_array(rfid_key.keyByte, MFRC522::MF_KEY_SIZE);
  Serial.println();
}

void loop() {
  // put your main code here, to run repeatedly:
  btn_thread(&btn_pt);
  membrane_keypad_thread(&keypad_pt);
  rfid_read_thread(&rfid_read_pt);
  rfid_write_thread(&rfid_write_pt);
}

static bool compare_readings(byte *read_one) {
  byte rfid_buff_size = sizeof(password);
  bool result = true;
  
  for (byte i = 0; password[i] != '\0'; i++) {
    
    if (password[i] != read_one[i]) {
      result = false;
      break; 
    }
  }

  return result;
}

static void dump_byte_array(byte *buffer, byte bufferSize) {
  for (byte i = 0; i < bufferSize; i++) {
    Serial.print(buffer[i] < 0x10 ? " 0" : " ");
    Serial.print(buffer[i], HEX);
  }
}
