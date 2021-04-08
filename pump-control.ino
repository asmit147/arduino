
/* vi:set tabstop=2: shiftwidth=2: softtabstop=2: expandtab: */
/* :e ++ff=dos then remove ^M */
/*
 * Pump control system
 *
 * Each pin can provide or receive a maximum of 40 mA and has an internal
 * pull-up resistor (disconnected by default) of 20-50 kOhms.
 *
 * water pressure switch (pressurised by pump)
 * normally open = no pressure = pullup to HIGH
 * closed = pressure = LOW
 *
 * Tank float switches are normally open (pullup HIGH) when there is nothing to
 * do. Float switch will close (LOW) when it begins to fill and during filling.
 * When tank is full the float switch will open, nothing to do.
 *
 * Tank limit switches are open (pullup HIGH) when the tank is full at the
 * limit. Tank limit switch rests in the closed LOW state when the tank is not
 * at or above its limit. This way, if no limit switches are fitted, we always
 * read them as if the tank is full, so only the float switches will drive
 * actions.
 */

// For half-duplex rs485 communication between Arduino's,
//over my Cat6 cable from the Dam Pump to the Tank(s)
//#include <ArduinoRS485.h>

#include <LiquidCrystal_I2C.h>
#include <Wire.h>
// Connect pins 2 to SDA & 3 toSCL on the Leonardo board.
// Connect pins SDA to A4, SCL to A5 on an UNO
// Install the "LiquidCrystalI2C" library

LiquidCrystal_I2C lcd(0x27, 20, 4);
//initialize the liquid crystal library and create an "lcd" instance
//the first parameter is the I2C address - for the 20x4 it is 0x27
//the second parameter is how many columns are on your screen
//the third parameter is how many rows are on your screen
//If display is blank, run I2C scanner to find correct address
//To run multiple displays, short the A0,A1 or A2 jumpers on the I2C adapter
//to get different addresses on different displays. Then create
//new instances with LiquidCrystal_I2C

// For 2.4g - 2.5g wireless comunications between Adruinos using the RF24.h library.
//#include <nRF24L01.h>
//#include <RF24.h>
//#include <RF24_config.h>
//#include <printf.h>
 
//Inputs pullup ACTIVE LOW
#define IN_BATT                         A0
#define IN_RESET_ALARMS                 1
#define LCD_RX                          2
#define LCD_TX                          3
#define IN_TANK_1_FLOAT                 4
#define IN_TANK_1_LIM                   5
#define IN_TANK_2_FLOAT                 6
#define IN_TANK_2_LIM                   7
#define IN_TIME_CLOCK                   8
#define IN_FUEL                         9
#define IN_WATER_PRESSURE               10
#define IN_OIL_PRESSURE_SWITCH          11
#define IN_START                        12
#define IN_STOP                         13

//Outputs ACTIVE HIGH
#define OUT_IGN                         A1
#define OUT_START                       A2
#define OUT_TANK_1_VALVE                A3
#define OUT_TANK_2_VALVE                A4
#define OUT_ALERT                       A5

//max time to wait for pressure switch to open after ignition off (seconds)
#define IGN_OFF_WAIT_MAX                15
#define CRANKING_DELAY                  5
#define CRANKING_TIME                   7
#define PRESSURE_WAIT_MAX               15
#define MAX_STARTUP_ATTEMPTS            2

/* uncomment to output to USB serial port */
#define debug_serial_USB
/* uncomment to output to TX RX header serial port */
//#define debug_serial_header
#define BAUD                            115200

/* Global variables */
int mode_auto = 1;
int quiet_time = 0;
int ignition_on = 0;
int low_fuel = 1;
int no_water_pressure = 1;
int startup_attempt = 0;
int tank_1_valve_open = 0;
int tank_2_valve_open = 0;
int pump_is_now_running = 0;
int ignition_status = 1;
int low_oil_pressure = 1;

char buf[64] = "";

void setup() {
  /* initalise all inputs and outputs */
  pinMode(IN_RESET_ALARMS, INPUT_PULLUP);
  pinMode(IN_START, INPUT_PULLUP);
  pinMode(IN_STOP, INPUT_PULLUP);
  pinMode(IN_TANK_1_FLOAT, INPUT_PULLUP);
  pinMode(IN_TANK_1_LIM, INPUT_PULLUP);
  pinMode(IN_TANK_2_FLOAT, INPUT_PULLUP);
  pinMode(IN_TANK_2_LIM, INPUT_PULLUP);
  pinMode(IN_TIME_CLOCK, INPUT_PULLUP);
  pinMode(IN_FUEL, INPUT_PULLUP);
  pinMode(IN_WATER_PRESSURE, INPUT_PULLUP);
  pinMode(IN_OIL_PRESSURE_SWITCH, INPUT_PULLUP);
  pinMode(IN_BATT, INPUT);
  
  pinMode(OUT_IGN, OUTPUT);
  pinMode(OUT_START, OUTPUT);
  pinMode(OUT_TANK_1_VALVE, OUTPUT);
  pinMode(OUT_TANK_2_VALVE, OUTPUT);
  pinMode(OUT_ALERT, OUTPUT);
 
  lcd.init();
  lcd.backlight();
  
  #ifdef debug_serial_USB
  /* USB port ttyACM */
  Serial.begin(BAUD);
  #elif defined debug_serial_header
  /* TX RX pin header */
  Serial1.begin(BAUD);
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

#define VREF        5.12      // measured from 5v on arduino with fluke
#define VDIVFACTOR  3.09884   // measured with fluke 12.29/3.966 out

float readbatt() {
  float a, v, vmeasured;
  a = analogRead(IN_BATT);
  vmeasured = a*VREF/1024.0;
  v = vmeasured*VDIVFACTOR;
  return v;
}

void lcd_writeln(uint8_t col, uint8_t row, const char *buf) {
  lcd.setCursor(col, row);
  // we want to trim to 20 chars and pad with spaces so we get a clean line
  // we don't want to mess with the real buf so copy what we need into a new buf here
  // 21 chars so we can add a null terminator at the end in-case we print as a
  // string too.
  char lcdbuf[21];
  int len = strlen(buf);
  if (len > 20)
    len = 20;
  memcpy(lcdbuf, buf, len);
  // pad with spaces to overwrite previous characters on display
  while (len < 20) {
    lcdbuf[len] = ' ';
    len++;
  }
  // add null terminator to signal end of string
  // not required for lcd print, but required for normal string printing
  lcdbuf[len] = '\0';
  //log("lcdbuf:");
  //log(lcdbuf);
  lcd.print(lcdbuf);
}

//defining alert due low fuel, low water or oil pressure
void alert(const char *msg) {
  do_shutdown();
  lcd_writeln(0, 0, "Alert!");
  lcd_writeln(0, 1, msg);
  lcd_writeln(0, 2, "");
  lcd_writeln(0, 3, "Press Alarm Reset");
  // this buf is the global buf
  sprintf(buf, "Alert: %s", msg);
  log(buf);
  digitalWrite(OUT_ALERT, HIGH);
  
  while (1) {
    delay(10);
    if (!digitalRead(IN_RESET_ALARMS)) {
      log("alarm reset!");
      digitalWrite(OUT_ALERT, LOW);
      while (!digitalRead(IN_RESET_ALARMS))
        delay(10);
      break;
    }
  }
}

void do_tank_1_valve_open() {
  log(__func__);
  digitalWrite(OUT_TANK_1_VALVE, HIGH);
  tank_1_valve_open = digitalRead(OUT_TANK_1_VALVE);
  
}
void do_tank_2_valve_open() {
  log(__func__);
  digitalWrite(OUT_TANK_2_VALVE, HIGH);
  tank_2_valve_open = digitalRead(OUT_TANK_2_VALVE);
 
}
void do_tank_1_valve_close() {
  log(__func__);
  digitalWrite(OUT_TANK_1_VALVE, LOW);
  tank_1_valve_open = digitalRead(OUT_TANK_1_VALVE);
 
}
void do_tank_2_valve_close() {
  log(__func__);
  digitalWrite(OUT_TANK_2_VALVE, LOW);
  tank_2_valve_open = digitalRead(OUT_TANK_2_VALVE);
  
}
void close_valves() {
  log(__func__);
  do_tank_1_valve_close();
  do_tank_2_valve_close();
}

  /*  During shutdown, the program will only be looking at the status of
  *   the IN_OIL_PRESSURE_SWITCH and IN_WATER_PRESSURE inputs.
  *   All other inputs will be ignored until shutdwn is complete
  */

void do_shutdown() {
  log(__func__);
  int cnt;
  digitalWrite(OUT_IGN, LOW);
  ignition_on = digitalRead(OUT_IGN);
  
  for (cnt = 0; cnt < IGN_OFF_WAIT_MAX; cnt++) {
    delay(1000);
    lcd_writeln(0, 0, "Shutting Down Pump");
   
    if (digitalRead(IN_OIL_PRESSURE_SWITCH) == HIGH && digitalRead(IN_WATER_PRESSURE) == HIGH){    
      close_valves();
      mode_auto = 1;
      startup_attempt = 0;
      pump_is_now_running = 0;
      break;
    }   
  }
  close_valves();
  mode_auto = 1;
  startup_attempt = 0;
  pump_is_now_running = 0;
}
  /*  
  *   Pump will start if; pump is not already started || no valves are open
  *   Pump will try 2 attempts to start, checking the status of the  
  *   IN_OIL_PRESSURE_SWITCH  && IN_WATER_PRESSURE   
  */

void do_startup() {
  log(__func__);
  int cnt;
  char buf[24];

  if (pump_is_now_running) {
    log("pump is already running!");
    return;
  }
  if (!tank_1_valve_open && !tank_2_valve_open) {
    log("no valves open, not starting pump!");
    return;
  }

  /* manual mode we assume the operator knows best and will not limit
   * startup attempts */
          
  startup_attempt++;
  sprintf(buf, "Start attempt: %d", startup_attempt);
  log(buf);
  lcd_writeln(0, 0, buf);

  // clear following lines that do not get used for a few seconds
  lcd_writeln(0, 1, "");
  lcd_writeln(0, 2, "");
  lcd_writeln(0, 3, "");
  
  // a message stating why want to wait for the cranking delay time period
  if(startup_attempt >= 2) {
    //log("Ignition OFF");
    digitalWrite(OUT_IGN, LOW);
    delay(CRANKING_DELAY * 1000);
  }
      
  //log("ignition on...");
  digitalWrite(OUT_IGN, HIGH);
  ignition_on = 1;

  // CRANKING_DELAY is number of seconds
  // delay function expects microseconds
  delay(CRANKING_DELAY * 1000);

  lcd_writeln(0, 1, "Cranking...");

  digitalWrite(OUT_START, HIGH);
  delay(CRANKING_TIME * 1000);
  digitalWrite(OUT_START, LOW);

  for (cnt = 0; cnt < PRESSURE_WAIT_MAX; cnt++) {
    /* check pressure inputs every 1 second */
    delay(1000);
    lcd_writeln(0, 1, "Waiting for pressure");

    char stat[3];
    if (digitalRead(IN_OIL_PRESSURE_SWITCH) == LOW){
      sprintf(stat, "OK");
    }
    else {
      sprintf(stat, "NO");
    }
    sprintf(buf, "oil pressure: %s", stat);
    lcd_writeln(0, 2, buf);
      
    if (digitalRead(IN_WATER_PRESSURE) == LOW){
      sprintf(stat, "OK");
    }
    else {
      sprintf(stat, "NO");
    }
    sprintf(buf, "water pressure: %s", stat);
    lcd_writeln(0, 3, buf);
    
    if (digitalRead(IN_OIL_PRESSURE_SWITCH) == LOW && digitalRead(IN_WATER_PRESSURE) == LOW){
      //log("pump is now running"); this is indicated in main loop
      pump_is_now_running = 1;
      startup_attempt = 0;
      break;  
    } 
  }
  if (startup_attempt >= MAX_STARTUP_ATTEMPTS){
    log("pump did not start after 2nd attempt");
    if (digitalRead(IN_OIL_PRESSURE_SWITCH) == HIGH) {
      alert("oil pressure NO");
    }
    if (digitalRead(IN_WATER_PRESSURE) == HIGH) {
      alert("water pressure NO");
    }
  }
}

//main loop

void loop() {
  delay(100);
  
  //Initialise (clear) LC Display
  //lcd.init();
  // LCD is initialised in setup function, we don't need to clear it here
  // because we simply overwrite the lines with new content.

  int start_button = digitalRead(IN_START);
  int stop_button = digitalRead(IN_STOP);
  int reset_button = digitalRead(IN_RESET_ALARMS);
  float volts = readbatt();

  low_fuel = digitalRead(IN_FUEL);
  /* time clock input high = contacts open = do not run */
  quiet_time = digitalRead(IN_TIME_CLOCK);
  /* no water pressure when contact is open, input is high */
  no_water_pressure = digitalRead(IN_WATER_PRESSURE);
  
  /* reads the status of the ignition switch
    and the tank valves on each void loop pass */
  ignition_status = digitalRead(OUT_IGN);
  tank_1_valve_open = digitalRead(OUT_TANK_1_VALVE);
  tank_2_valve_open = digitalRead(OUT_TANK_2_VALVE);
  
  // low oil pressure when contact is open, input is high 
  low_oil_pressure = digitalRead(IN_OIL_PRESSURE_SWITCH);

  //provides realtime status to serial output
  sprintf(buf, "time clock is %s", quiet_time ? "off" : "on");
  log(buf);
  sprintf(buf, "auto mode is %s", mode_auto ? "on" : "off");
  log(buf);
  sprintf(buf, "ignition is %s", ignition_on ? "on" : "off");
  log(buf);
  sprintf(buf, "Pump%srunning", pump_is_now_running ? " " : " NOT ");
  log(buf);
  lcd_writeln(0, 0, buf);
  //sprintf(buf, "ignition output is %s", ignition_status ? "on" : "off");
  //log(buf);
  sprintf(buf, "water pressure %s", no_water_pressure ? "no" : "yes");
  log(buf);
  sprintf(buf, "low oil pressure %s", low_oil_pressure ? "no" : "yes");
  log(buf);
  sprintf(buf, "low fuel %s", low_fuel ? "no" : "yes");
  log(buf);
  sprintf(buf, "T1:L%d F%d V %s",
    digitalRead(IN_TANK_1_LIM),
    digitalRead(IN_TANK_1_FLOAT),
    tank_1_valve_open ? "open" : "closed");
  log(buf);
  lcd_writeln(0, 1, buf);
  
  sprintf(buf, "T2:L%d F%d V %s",
    digitalRead(IN_TANK_2_LIM),
    digitalRead(IN_TANK_2_FLOAT),
    tank_2_valve_open ? "open" : "closed");
  log(buf);
  
  lcd_writeln(0, 2, buf);
  
  char voltstr[10];
  sprintf(buf, "Battery %sV", dtostrf(volts, 2, 2, voltstr));
  log(buf);
  lcd_writeln(0, 3, buf);
     
// check if fuel is low, shutdown (if pump is running) and turn on low fuel alert
  if (!low_fuel) {
    alert("low fuel");
}

/*    the start button will place the program into "manual mode", start the pump and 
 *    fill the respective tanks until each limit switch turns off
 *    Note: When in 'manual mode', the program will only look at the limit switches
*/

  if (!pump_is_now_running && !start_button) {
    log("start button pressed");
    log("turn auto mode off");
    mode_auto = 0;
    
    if (digitalRead(IN_TANK_1_LIM) == LOW){
      do_tank_1_valve_open();
      do_startup();
    }
    if (digitalRead(IN_TANK_2_LIM) == LOW){
      do_tank_2_valve_open();
      do_startup();
    }
  }

    // to trigger the second pass of manual start attempt
    if ( startup_attempt >= 1 && !pump_is_now_running && !mode_auto) {
      if (digitalRead(IN_TANK_1_LIM) == HIGH || digitalRead(IN_TANK_2_LIM) == HIGH) {
        do_startup();
      }
    }

//  stop button causes alert which means we do nothing forever until the
//  reset button is pressed. change back to auto mode.
 
    if (pump_is_now_running && !stop_button) {
      log("turn auto mode on");
      mode_auto = 1;
      alert("stop button pressed");
    }

  //  if the pump is NOT running & time clock is ON and in Auto Mode
  //  auto pump mode only reads the tank floats
    
  quiet_time = digitalRead(IN_TIME_CLOCK);

  if (!pump_is_now_running && !quiet_time && mode_auto) {

    //lcd_writeln(0, 0, "Pump NOT running ");

    if (digitalRead(IN_TANK_1_FLOAT) == LOW) {
      do_tank_1_valve_open();
      do_startup();
    }
    if (digitalRead(IN_TANK_2_FLOAT) == LOW) {
      do_tank_2_valve_open();
      do_startup();
    }   
  }
  
   //  I have added the 'do_shutdown' statements to
  //  resolve a float switch bounce on and off situation
  
    if ( startup_attempt >= 1 && !pump_is_now_running && !quiet_time && mode_auto) {
      if (digitalRead(IN_TANK_1_FLOAT) == HIGH && digitalRead(IN_TANK_2_FLOAT) == HIGH) {
        do_shutdown();
      }
    }

   //  I have added the 'close' statements to
  //  resolve a limit switch bounce on and off situation
  
    if ( startup_attempt >= 1 && !pump_is_now_running && !mode_auto) {
      if (digitalRead(IN_TANK_1_LIM) == HIGH && digitalRead(IN_TANK_2_LIM) == HIGH) {
        do_shutdown();
      }
    }
    
//while pump is running, check water and oil pressure is good, else shutdown

    low_oil_pressure = digitalRead(IN_OIL_PRESSURE_SWITCH);
    no_water_pressure = digitalRead(IN_WATER_PRESSURE);
    
    if (pump_is_now_running) {
      if (!no_water_pressure && !low_oil_pressure){
        //log("pump is running, water and oil pressure is good");

    }  
      if (no_water_pressure) {
        //log("pump is running, BUT water pressure is NOT good");
        alert("Water Pressure low");
    }
      if (low_oil_pressure) {
        //log("pump is running, BUT oil pressure is not good");
        alert("Oil pressure low");
     }
  }

//deals with the floats switch changes when pump is running & in auto mode
  
  if (pump_is_now_running && mode_auto) {
    if (digitalRead(IN_TANK_1_FLOAT) == HIGH && digitalRead(IN_TANK_2_FLOAT) == HIGH){
      log("pump is running, both float switches are off");
      do_shutdown();
    }
     if (digitalRead(IN_TANK_1_FLOAT) == HIGH && digitalRead(IN_TANK_2_FLOAT) == LOW){
       do_tank_1_valve_close();
       do_tank_2_valve_open();
               
    }        
     if (digitalRead(IN_TANK_1_FLOAT) == LOW && digitalRead(IN_TANK_2_FLOAT) == HIGH){
       do_tank_1_valve_open();
       do_tank_2_valve_close();

    }          
      if (digitalRead(IN_TANK_1_FLOAT) == LOW && digitalRead(IN_TANK_2_FLOAT) == LOW){
       do_tank_1_valve_open();
       do_tank_2_valve_open();

    }
  }
  
//deals with the limit switch changes when pump is running & in manual mode

 if (pump_is_now_running && !mode_auto) {
    if (digitalRead(IN_TANK_1_LIM) == HIGH && digitalRead(IN_TANK_2_LIM) == HIGH){
      log("pump is running, both limit switches are off, pump will be shutdown");
      do_shutdown();
    }            
      if (digitalRead(IN_TANK_1_LIM) == LOW && digitalRead(IN_TANK_2_LIM) == HIGH){
       do_tank_1_valve_open();
       do_tank_2_valve_close();
    }      
      if (digitalRead(IN_TANK_1_LIM) == HIGH && digitalRead(IN_TANK_2_LIM) == LOW){
        do_tank_1_valve_close();
        do_tank_2_valve_open();
    }        
      if (digitalRead(IN_TANK_1_LIM) == LOW && digitalRead(IN_TANK_2_LIM) == LOW){
        do_tank_1_valve_open();
        do_tank_2_valve_open();
    }                     
  }
}
