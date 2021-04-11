/* vi:set tabstop=2: shiftwidth=2: softtabstop=2: expandtab:
 *
 * Pump control system
 * slave node with LCD screen output
 * testing version connected to master node over rs485
 * Andrew is using using arduino nano for this, your pin definitions may vary
 *
 */

#include <ArduinoRS485.h>
#include <SoftwareSerial.h>
#include <LiquidCrystal_I2C.h>
#include <Wire.h>
#include <pump-control-lib.h>

#define SERIAL_RX                       10 // RO
#define SERIAL_TX                       11 // DI
#define OUT_DE                          3 // DE & RE

//Inputs pullup ACTIVE LOW
#define IN_TANK_1_FLOAT                 4
#define IN_TANK_1_LIM                   5
#define IN_RESET_ALARMS                 6
#define IN_START                        12
#define IN_STOP                         13

//Outputs ACTIVE HIGH
#define OUT_ALERT                       A0

// uncomment to output to USB serial port
#define debug_serial_USB
// uncomment to output to TX RX header serial port
//#define debug_serial_header

static struct slavemsgbuf sendbuf;
static struct mastermsgbuf recvbuf;

char buf[32];

//the first parameter is the I2C address - for the 20x4 it is 0x27
LiquidCrystal_I2C lcd(0x27, 20, 4);

SoftwareSerial serialcom(SERIAL_RX, SERIAL_TX);

void setup() {

	memset(&sendbuf, 0, sizeof(sendbuf));
	sendbuf.node_address_to |= MASTER_ID;
	sendbuf.node_address_from |= DISPLAY_NODE_ID;

	serialcom.begin(RS485_BPS);

  /* initalise all inputs and outputs */
  pinMode(IN_RESET_ALARMS, INPUT_PULLUP);
  pinMode(IN_START, INPUT_PULLUP);
  pinMode(IN_STOP, INPUT_PULLUP);

	/* it's good practice to set the output level before defining the pin mode OUTPUT */
	digitalWrite(OUT_DE, 0);
	digitalWrite(OUT_ALERT, 0);

	pinMode(OUT_DE, OUTPUT);
	pinMode(OUT_ALERT, OUTPUT);

  lcd.init();
  lcd.backlight();
  
  #ifdef debug_serial_USB
  /* USB port ttyACM */
  Serial.begin(SERIAL_MON_BPS);
  #elif defined debug_serial_header
  /* TX RX pin header */
  Serial1.begin(SERIAL_MON_BPS);
  #endif
}

/* log function makes it easy to control which serial port is being used */
void log(const char *buf) {
  #ifdef debug_serial_USB
  Serial.println(buf);
  #elif defined debug_serial_header
  Serial1.println(buf);
  #else
  /* no serial output */
  #endif
}

void lcd_writebuf(uint8_t row, const char *buf) {
  // always write from beginning of row
  lcd.setCursor(0, row);
  // 21 chars so we can add null terminator
  char lcdbuf[21];
  memcpy(lcdbuf, buf, 20);
  lcdbuf[21] = '\0';
	//log(lcdbuf);
  lcd.print(lcdbuf);
}

void loop() {

	digitalWrite(OUT_DE, 0);
  delay(1000);

  int start_button = digitalRead(IN_START);
  int stop_button = digitalRead(IN_STOP);
  int reset_button = digitalRead(IN_RESET_ALARMS);

  if (!start_button) {
    log("start button pressed");
    // change reply struct with start button setting
  }
  //TODO
  // stop button
  // reset button

  // recieve from master and send response
	serialcom.listen();
	while (serialcom.available() > 0) {
		int i, j;
		char in = serialcom.read();
		char buf[20];
		//Serial.print(in);
		if (in = 0x2) {
			log("addressed to me!");
			recvbuf.node_address_to = in;
			// fill out recv messge there's probably a better way
			recvbuf.node_address_from = serialcom.read();
			recvbuf.inputs = serialcom.read();
			recvbuf.outputs = serialcom.read();
			for (i = 0; i < 4; i++) {
				for (j = 0; j < 20; j++) {
					recvbuf.line[i][j] = serialcom.read();
					sprintf(buf, "recv: %c\n", recvbuf.line[i][j]);
					Serial.println(buf);
				}
			}
			for (i = 0; i < 4; i++) {
				lcd_writebuf(i, recvbuf.line[i]);
			}
			break;
		}
	}

	//sprintf(buf, "hello world");
  //lcd_writebuf(0, buf);
}
