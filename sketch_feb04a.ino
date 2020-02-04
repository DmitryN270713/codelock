#include <SPI.h>
#include <MFRC522.h>
#include <require_cpp11.h>
#include <Key.h>
#include <Keypad.h>
#include <LiquidCrystal.h>

#include <pt-sem.h>
#include <pt.h>

// Display
const int rs = 53, en = 51, d4 = 49, d5 = 47, d6 = 45, d7 = 43;
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
const int sda = 10, sck = 13, mosi = 11, miso = 12, rst = 9;
// States
static enum states{BEGIN = 0, RUN, STOP, READ_KEY, WRITE_KEY, LISTEN_KEYPAD, RESET, INVALID};
// Threads
static struct pt btn_pt, rfid_write_pt, rfid_read_pt, display_pt, keypad_pt;
// Mutexes
static struct pt_sem btn_mutex, rfid_write_mutex, rfid_read_mutex, display_mutex, keypad_mutex;

static PT_THREAD(btn_thread(struct pt *pt))
{
  PT_BEGIN(pt);
  // TO DO: wait for button pressed
  PT_END(pt);
}

static PT_THREAD(rfid_write_thread(struct pt *pt))
{
  PT_BEGIN(pt);
  // TO DO: write data
  PT_END(pt);
}

static PT_THREAD(rfid_read_thread(struct pt *pt))
{
  PT_BEGIN(pt);
  // TO DO: read data  
  PT_END(pt);
}

static PT_THREAD(display_thread(struct pt *pt))
{
  PT_BEGIN(pt);
  // TO DO: display implementation
  PT_END(pt);
}

static PT_THREAD(membrane_keypad_thread(struct pt *pt))
{
  PT_BEGIN(pt);
  // TO DO: membrane keypad reading
  PT_END(pt);
}

void setup() {
  // put your setup code here, to run once:
  // Threads
  PT_INIT(&btn_pt);
  PT_INIT(&rfid_write_pt);
  PT_INIT(&rfid_read_pt);
  PT_INIT(&display_pt);
  PT_INIT(&keypad_pt);
  // Mutexes
  PT_SEM_INIT(&btn_mutex, 1);
  PT_SEM_INIT(&rfid_write_mutex, 1);
  PT_SEM_INIT(&rfid_read_mutex, 1);
  PT_SEM_INIT(&display_mutex, 1);
  PT_SEM_INIT(&keypad_mutex, 1);
}

void loop() {
  // put your main code here, to run repeatedly:

}
