/*
    Name:       TeensyOTADemo.ino
    Created:	10/9/2021 11:18:19 AM
    Author:     FRANKNEWXPS15\Frank

    This program demonstrates the use of 'board.txt' post-build instructions in
    conjunction with a small C# command-line program for seamless 'over-the-air' (OTA)
    firmware update for Teensy modules.

    Implementation of OTA update requires that Joe Pasquariello's 'FlasherX'
    code be added to the Teensy sketch as it is below, and some code to allow
    an external serial communications program to trigger the update process.
    
    In the demo sketch below, the 'FlasherX' additions are the '#include FlasherTxx.h'
    line and the addition of the FlasherX specific code at the bottom. The trigger
    mechanism is provided by the 'CheckForUserInput();' call in loop.  If the ASCII 
    code for 'U' or 'u' is present on Serial1, the update process will be launched.
    
*/

#include "FlashTxx.h"		// TLC/T3x/T4x flash primitives
#include <elapsedMillis.h>

elapsedMillis MsecSinceLastLEDToggle; //used for LED blink timer

Stream* FTDIport = &Serial1;	// Serial (USB) or Serial1, Serial2, etc. (UART)

//******************************************************************************
// hex_info_t	struct for hex record and hex file info
//******************************************************************************
typedef struct {	// 
  char* data;		// pointer to array allocated elsewhere
  unsigned int addr;	// address in intel hex record
  unsigned int code;	// intel hex record type (0=data, etc.)
  unsigned int num;	// number of data bytes in intel hex record

  uint32_t base;	// base address to be added to intel hex 16-bit addr
  uint32_t min;		// min address in hex file
  uint32_t max;		// max address in hex file

  int eof;		// set true on intel hex EOF (code = 1)
  int lines;		// number of hex records received  
} hex_info_t;

void read_ascii_line(Stream* serial, char* line, int maxbytes);
int  parse_hex_line(const char* theline, char* bytes,
  unsigned int* addr, unsigned int* num, unsigned int* code);
int  process_hex_record(hex_info_t* hex);
void update_firmware(Stream* serial, uint32_t buffer_addr, uint32_t buffer_size);

uint32_t buffer_addr, buffer_size; //09/20/21 copied from FlasherX - loop()

void setup()
{
  Serial.begin(115200);
  //Serial.begin(230400);
  //while (!Serial) {} //10/06/21 infinite wait hangs pgm when USB cable removed - oops!
  delay(2000); //10/06/21 - just use fixed delay instead

//  Serial1.begin(115200);
  Serial1.begin(230400);
  while (!Serial1) {}

  pinMode(LED_BUILTIN, OUTPUT);

  MsecSinceLastLEDToggle = 0;
}

void loop()
{
  if (Serial1.available())
  {
    CheckForUserInput();
  }

  if (MsecSinceLastLEDToggle >= 200)
  {
    MsecSinceLastLEDToggle -= 200;
    digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
  }
}
void CheckForUserInput()
{
  const int bufflen = 80;
  char buff[bufflen];
  memset(buff, 0, bufflen);
  //const char s[2] = ",";

  if (Serial1.available() > 0)
  {
    // read the incoming byte:
    int incomingByte = Serial1.read();

    // say what you got:
    //Serial1.print("I received: ");
    //Serial1.println(incomingByte, HEX); //chg to HEX 02/18/20

    //02/18/20 experiment with multiple commands
    switch (incomingByte)
    {
    case 0x55: //ASCII 'U'
    case 0x75: //ASCII 'u'
      Serial1.println(F("Start Program Update - Send new HEX file!"));


      //09/20/21 copied from FlasherX - loop()
      if (firmware_buffer_init(&buffer_addr, &buffer_size) == 0)
      {
        Serial1.printf("unable to create buffer\n");
        Serial1.flush();
        for (;;) {}
      }

      Serial1.printf("buffer = %1luK %s (%08lX - %08lX)\n",
        buffer_size / 1024, IN_FLASH(buffer_addr) ? "FLASH" : "RAM",
        buffer_addr, buffer_addr + buffer_size);

      //09/20/21 clear the serial buffer
      while (Serial1.available())
      {
        Serial1.read();
      }

      // receive hex file via serial, write new firmware to flash, clean up, reboot
      update_firmware(&Serial1, buffer_addr, buffer_size); // no return if success

      // return from update_firmware() means error or user abort, so clean up and
      // reboot to ensure that static vars get boot-up initialized before retry
      Serial1.printf("erase FLASH buffer / free RAM buffer...\n");
      firmware_buffer_free(buffer_addr, buffer_size);
      Serial1.flush();
      REBOOT;
      break;
    }
  }
}

//09/20/21 copied from FlasherX
//******************************************************************************
// update_firmware()	read hex file and write new firmware to program flash
//******************************************************************************
void update_firmware(Stream* serial, uint32_t buffer_addr, uint32_t buffer_size)
{
  static char line[96];					// buffer for hex lines
  static char data[32] __attribute__((aligned(8)));	// buffer for hex data
  hex_info_t hex = {					// intel hex info struct
    data, 0, 0, 0,					//   data,addr,num,code
    0, 0xFFFFFFFF, 0, 					//   base,min,max,
    0, 0						//   eof,lines
  };

  serial->printf("waiting for hex lines...\n");

  // read and process intel hex lines until EOF or error
//DEBUG!!
  //int line_num = 0;
//DEBUG!!
  long int start_msec = 0;
  bool bFirstTime = true;
  while (!hex.eof) {

    read_ascii_line(serial, line, sizeof(line));

    if (bFirstTime)
    {
      start_msec = millis();
      bFirstTime = false;
    }

//DEBUG!!
    //serial->printf("%d: %s\n", line_num, line);
    //line_num++;
//DEBUG!!

    //10/02/21 gfp added for visual xfer confirmation
    if (hex.lines % 10 == 0)
    {
      digitalWriteFast(LED_BUILTIN, !digitalReadFast(LED_BUILTIN));
    }

    // reliability of transfer via USB is improved by this printf/flush
    if (serial == (Stream*)&Serial)
    {
      serial->printf("%s\n", line);
      serial->flush();
    }

    if (parse_hex_line((const char*)line, hex.data, &hex.addr, &hex.num, &hex.code) == 0) {
      serial->printf("abort - bad hex line %s\n", line);
      return;
    }
    else if (process_hex_record(&hex) != 0) { // error on bad hex code
      serial->printf("abort - invalid hex code %d\n", hex.code);
      return;
    }
    else if (hex.code == 0) { // if data record
      uint32_t addr = buffer_addr + hex.base + hex.addr - FLASH_BASE_ADDR;
      if (hex.max > (FLASH_BASE_ADDR + buffer_size)) {
        serial->printf("abort - max address %08lX too large\n", hex.max);
        return;
      }
      else if (!IN_FLASH(buffer_addr)) {
        memcpy((void*)addr, (void*)hex.data, hex.num);
      }
      else if (IN_FLASH(buffer_addr)) {
        int error = flash_write_block(addr, hex.data, hex.num);
        if (error) {
          serial->printf("abort - error %02X in flash_write_block()\n", error);
          return;
        }
      }
    }
    hex.lines++;
  }

  //serial->printf("\nhex file: %1d lines %1lu bytes (%08lX - %08lX)\n",
  //  hex.lines, hex.max - hex.min, hex.min, hex.max);
  serial->printf("\nhex file: %1d lines %1lu bytes (%08lX - %08lX), %lu Msec\n",
    hex.lines, hex.max - hex.min, hex.min, hex.max, millis()-start_msec);

  // check FSEC value in new code -- abort if incorrect
#if defined(KINETISK) || defined(KINETISL)
  uint32_t value = *(uint32_t*)(0x40C + buffer_addr);
  if (value == 0xfffff9de) {
    serial->printf("new code contains correct FSEC value %08lX\n", value);
  }
  else {
    serial->printf("abort - FSEC value %08lX should be FFFFF9DE\n", value);
    return;
  }
#endif

  // check FLASH_ID in new code - abort if not found
  if (check_flash_id(buffer_addr, hex.max - hex.min)) {
    serial->printf("new code contains correct target ID %s\n", FLASH_ID);
  }
  else {
    serial->printf("abort - new code missing string %s\n", FLASH_ID);
    return;
  }

  // get user input to write to flash or abort
  int user_lines = -1;
  while (user_lines != hex.lines && user_lines != 0) {
    serial->printf("enter %d to flash or 0 to abort\n", hex.lines);
    read_ascii_line(serial, line, sizeof(line));
    sscanf(line, "%d", &user_lines);
  }

  if (user_lines == 0) {
    serial->printf("abort - user entered 0 lines\n");
    return;
  }

  // move new program from buffer to flash, free buffer, and reboot
  //serial->printf("user entered %d lines\n", user_lines);
  flash_move(FLASH_BASE_ADDR, buffer_addr, hex.max - hex.min);

  // should not return from flash_move(), but put REBOOT here as reminder
  REBOOT;
}

//******************************************************************************
// read_ascii_line()	read ascii characters until '\n', '\r', or max bytes
//******************************************************************************
void read_ascii_line(Stream* serial, char* line, int maxbytes)
{
  int c = 0, nchar = 0;
  while (nchar < maxbytes && !(c == '\n' || c == '\r')) {
    if (serial->available()) {
      c = serial->read();
      line[nchar++] = c;
    }
  }
  line[nchar - 1] = 0;	// null-terminate
}

//******************************************************************************
// process_hex_record()		process record and return okay (0) or error (1)
//******************************************************************************
int process_hex_record(hex_info_t* hex)
{
  if (hex->code == 0) { // data -- update min/max address so far
    if (hex->base + hex->addr + hex->num > hex->max)
      hex->max = hex->base + hex->addr + hex->num;
    if (hex->base + hex->addr < hex->min)
      hex->min = hex->base + hex->addr;
  }
  else if (hex->code == 1) { // EOF (:flash command not received yet)
    hex->eof = 1;
  }
  else if (hex->code == 2) { // extended segment address (top 16 of 24-bit addr)
    hex->base = ((hex->data[0] << 8) | hex->data[1]) << 4;
  }
  else if (hex->code == 3) { // start segment address (80x86 real mode only)
    return 1;
  }
  else if (hex->code == 4) { // extended linear address (top 16 of 32-bit addr)
    hex->base = ((hex->data[0] << 8) | hex->data[1]) << 16;
  }
  else if (hex->code == 5) { // start linear address (32-bit big endian addr)
    hex->base = (hex->data[0] << 24) | (hex->data[1] << 16)
      | (hex->data[2] << 8) | (hex->data[3] << 0);
  }
  else {
    return 1;
  }

  return 0;
}

//******************************************************************************
// Intel Hex record foramt:
//
// Start code:  one character, ASCII colon ':'.
// Byte count:  two hex digits, number of bytes (hex digit pairs) in data field.
// Address:     four hex digits
// Record type: two hex digits, 00 to 05, defining the meaning of the data field.
// Data:        n bytes of data represented by 2n hex digits.
// Checksum:    two hex digits, computed value used to verify record has no errors.
//
// Examples:
//  :10 9D30 00 711F0000AD38000005390000F5460000 35
//  :04 9D40 00 01480000 D6
//  :00 0000 01 FF
//******************************************************************************

/* Intel HEX read/write functions, Paul Stoffregen, paul@ece.orst.edu */
/* This code is in the public domain.  Please retain my name and */
/* email address in distributed copies, and let me know about any bugs */

/* I, Paul Stoffregen, give no warranty, expressed or implied for */
/* this software and/or documentation provided, including, without */
/* limitation, warranty of merchantability and fitness for a */
/* particular purpose. */

// type modifications by Jon Zeeff

/* parses a line of intel hex code, stores the data in bytes[] */
/* and the beginning address in addr, and returns a 1 if the */
/* line was valid, or a 0 if an error occured.  The variable */
/* num gets the number of bytes that were stored into bytes[] */

#include <stdio.h>		// sscanf(), etc.
#include <string.h>		// strlen(), etc.

int parse_hex_line(const char* theline, char* bytes,
  unsigned int* addr, unsigned int* num, unsigned int* code)
{
  unsigned sum, len, cksum;
  const char* ptr;
  int temp;

  *num = 0;
  if (theline[0] != ':')
    return 0;
  if (strlen(theline) < 11)
    return 0;
  ptr = theline + 1;
  if (!sscanf(ptr, "%02x", &len))
    return 0;
  ptr += 2;
  if (strlen(theline) < (11 + (len * 2)))
    return 0;
  if (!sscanf(ptr, "%04x", (unsigned int*)addr))
    return 0;
  ptr += 4;
  /* Serial.printf("Line: length=%d Addr=%d\n", len, *addr); */
  if (!sscanf(ptr, "%02x", code))
    return 0;
  ptr += 2;
  sum = (len & 255) + ((*addr >> 8) & 255) + (*addr & 255) + (*code & 255);
  while (*num != len)
  {
    if (!sscanf(ptr, "%02x", &temp))
      return 0;
    bytes[*num] = temp;
    ptr += 2;
    sum += bytes[*num] & 255;
    (*num)++;
    if (*num >= 256)
      return 0;
  }
  if (!sscanf(ptr, "%02x", &cksum))
    return 0;

  if (((sum & 255) + (cksum & 255)) & 255)
    return 0;     /* checksum error */
  return 1;
}
