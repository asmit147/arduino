/* vi:set tabstop=2: shiftwidth=2: softtabstop=2: expandtab: */
/* :e ++ff=dos then remove ^M */
/*
 * Pump control system
 *
 * Update v1.1: 
 * 1. Added IN_RESET_ALARMS to reset alarms
 * 2. Modified the operation of the IN_STOP
 *    to shutdown pump turn on alarm
 * 2. Added outputs to the logging
 * 3. All test result issues resolved
 * 4. Test for oil pressure on startup
 * 
 * Update v1.0: 
 * 1. Added oil pressure input
 * 2. Added inputs to the logging
 * 3. Added fuel alert output & oil pressure alert output
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
 *
 */
//Inputs pullup ACTIVE LOW
#define IN_BATT                         A0
#define IN_RESET_ALARMS                 1
#define IN_START                        2
#define IN_STOP                         3
#define IN_TANK_1_FLOAT                 4
#define IN_TANK_1_LIM                   5
#define IN_TANK_2_FLOAT                 6
#define IN_TANK_2_LIM                   7
#define IN_TIME_CLOCK                   8
#define IN_FUEL                         9
#define IN_WATER_PRESSURE               10
#define IN_OIL_PRESSURE_SWITCH          11

//Outputs ACTIVE HIGH
#define OUT_OIL_PRESSURE_ALERT          12
#define OUT_LOW_FUEL_ALERT              13
#define OUT_IGN                         0
#define OUT_START                       A1
#define OUT_PRESSURE_OVERRIDE           A2
#define OUT_TANK_1_VALVE                A3
#define OUT_TANK_2_VALVE                A4
#define OUT_WATER_PRESSURE_ALERT        A5

//max time to wait for pressure switch to open after ignition off (seconds)
#define IGN_OFF_WAIT_MAX                15
#define CRANKING_DELAY                  5
#define CRANKING_TIME                   7
#define WATER_PRESSURE_WAIT_MAX         15
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
  pinMode(OUT_PRESSURE_OVERRIDE, OUTPUT);
  pinMode(OUT_TANK_1_VALVE, OUTPUT);
  pinMode(OUT_TANK_2_VALVE, OUTPUT);
  pinMode(OUT_WATER_PRESSURE_ALERT, OUTPUT);
  pinMode(OUT_OIL_PRESSURE_ALERT, OUTPUT);
  pinMode(OUT_LOW_FUEL_ALERT, OUTPUT);

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

#define VREF        4.96      // measured from 5v on arduino with fluke
#define VDIVFACTOR  3.09884   // measured with fluke 12.29/3.966 out
#define R1          10000.0    // nominal
#define R2          4700.0    // nominal
#define R1M          9830.0    // measured
#define R2M          4690.0    // measured

float readbatt()
{
  float a, v, vmeasured;
  a = analogRead(IN_BATT);
  vmeasured = a*VREF/1024.0;
  char buf[64];
  char voltstr[10];
  // measured input voltage
  sprintf(buf, "measured %sV", dtostrf(vmeasured, 2, 2, voltstr));
  log(buf);
  // method 1: work out v at VIN based on resistors 4.7k and 10k
  // Vin = Vout * (R1 + R2) / R2
  // measured resistor values
  sprintf(buf, "battery mes %sV", dtostrf((vmeasured*(R1M + R2M))/R2M, 2, 2, voltstr));
  log(buf);
  // nominal resistor values
  sprintf(buf, "battery nom %sV", dtostrf((vmeasured*(R1 + R2))/R2, 2, 2, voltstr));
  log(buf);
  // method 2: measure the real circuit and apply a constant value
  sprintf(buf, "battery con %sV", dtostrf(vmeasured*VDIVFACTOR, 2, 2, voltstr));
  log(buf);

  v = vmeasured*VDIVFACTOR;
  return v;
}

//defining the alerts

void oil_pressure_alert(const char *msg) {
  /* alert operator and do nothing forever until start button pressed */
  char buf[64];
  sprintf(buf, "Alert: %s", msg);
  log(buf);

    log("low oil pressure, shutting down pump and valves");
    digitalWrite(OUT_OIL_PRESSURE_ALERT, HIGH);
    do_shutdown();
  
  while (1) {
    delay(10);
    if (!digitalRead(IN_RESET_ALARMS)) {
      log("reset button pressed, low oil pressure alert cleared");
      digitalWrite(OUT_OIL_PRESSURE_ALERT, LOW);
      /* wait for button release so this button press does not
       * also cause a manual startup. */
      while (!digitalRead(IN_RESET_ALARMS))
        delay(10);
      break;
    }
  }
}

void low_fuel_alert(const char *msg) {
  /* alert operator and do nothing forever until start button pressed */
  char buf[64];
  sprintf(buf, "Alert: %s", msg);
  log(buf);

  log("low fuel, shutting down pump and valves");
  digitalWrite(OUT_LOW_FUEL_ALERT, HIGH);
  do_shutdown();
  
  while (1) {
    delay(10);
    if (!digitalRead(IN_RESET_ALARMS)) {
      log("reset button pressed, alert finished");
      digitalWrite(OUT_LOW_FUEL_ALERT, LOW);
      /* wait for button release so this button press does not
       * also cause a manual startup. */
      while (!digitalRead(IN_RESET_ALARMS))
        delay(10);
      break;
    }
  }
}

void low_water_pressure_alert(const char *msg) {
  /* alert operator and do nothing forever until start button pressed */
  char buf[64];
  sprintf(buf, "Alert: %s", msg);
  log(buf);
  log("low water pressure, shutting down pump and valves");
  digitalWrite(OUT_WATER_PRESSURE_ALERT, HIGH);
  do_shutdown();
  
  while (1) {
    delay(10);
    if (!digitalRead(IN_RESET_ALARMS)) {
      log("reset button pressed, alert finished");
      digitalWrite(OUT_WATER_PRESSURE_ALERT, LOW);
      /* wait for button release so this button press does not
       * also cause a manual startup. */
      while (!digitalRead(IN_RESET_ALARMS))
        delay(10);
      break;
    }
  }
}

void stop_pump_alert() {
  log("shutdown pump and turn on alerts");
  digitalWrite(OUT_WATER_PRESSURE_ALERT, HIGH);
  digitalWrite(OUT_OIL_PRESSURE_ALERT, HIGH);
  
  do_shutdown();
  while (1) {
    delay(10);
    if (!digitalRead(IN_RESET_ALARMS)) {
      log("reset button pressed, alert finished");
      digitalWrite(OUT_WATER_PRESSURE_ALERT, LOW);
      digitalWrite(OUT_OIL_PRESSURE_ALERT, LOW);
      /* wait for button release so this button press does not
       * also cause a manual startup. */
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
    log("waiting for water pressure and oil pressure to drop.....");
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
  *   Pump will try 2 attempts to start (when in 'auto mode'), checking the status of the  
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
   
  
   if (startup_attempt >= MAX_STARTUP_ATTEMPTS) {
     stop_pump_alert();
     startup_attempt = 0;
     return;
   }
    startup_attempt++;
    sprintf(buf, "start attempt %d", startup_attempt);
    log(buf);
    
  log("ignition on...");
  digitalWrite(OUT_IGN, HIGH);
  //ignition_on = digitalRead(OUT_IGN);
  ignition_on = 1;
  delay(CRANKING_DELAY * 1000);

  log("cranking...");
  digitalWrite(OUT_START, HIGH);
  delay(CRANKING_TIME * 1000);
  digitalWrite(OUT_START, LOW);

  for (cnt = 0; cnt < WATER_PRESSURE_WAIT_MAX; cnt++) {
    /* check pressure valve every 1 second */
    delay(1000);
     
      if (digitalRead(IN_OIL_PRESSURE_SWITCH) == LOW){
         log("oil pressure is good");
      }
        else { 
          log("waiting for oil pressure......");
        }

      if (digitalRead(IN_WATER_PRESSURE) == LOW){
          log("water pressure is good");
      }
        else { 
          log("waiting for water pressure......");
        }
     
    if (digitalRead(IN_OIL_PRESSURE_SWITCH) == LOW && digitalRead(IN_WATER_PRESSURE) == LOW){
      log("pump_is_now_running = 1");
      pump_is_now_running = 1;
      startup_attempt = 0;
      break;  
    } 
  }
    if (startup_attempt == MAX_STARTUP_ATTEMPTS){
        
      log("pump did not start after 2nd attempt");
      
      if (digitalRead(IN_OIL_PRESSURE_SWITCH) == HIGH) {
        log("because oil pressure is low");
      }   
      if (digitalRead(IN_WATER_PRESSURE) == HIGH) {
         log("because water pressure is low");
      }
      stop_pump_alert();  
    }

   
  /* if we never achieve water pressure, next loop will shutdown */
}

//main loop

void loop() {
  delay(100);

  char buf[128] = "";
  int start_button = digitalRead(IN_START);
  int stop_button = digitalRead(IN_STOP);
  int reset_button = digitalRead(IN_RESET_ALARMS);
  float volts = readbatt();

  low_fuel = digitalRead(IN_FUEL);
  /* time clock input high = contacts open = do not run */
  quiet_time = digitalRead(IN_TIME_CLOCK);
  /* no water pressure when contact is open, input is high */
  no_water_pressure = digitalRead(IN_WATER_PRESSURE);
  
  /*Brad's additions
    reads the status of the ignition switch
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
  sprintf(buf, "pump is running %s", pump_is_now_running ? "yes" : "no");
  log(buf);
  sprintf(buf, "ignition output is %s", ignition_status ? "on" : "off");
  log(buf);
  sprintf(buf, "water pressure %s", no_water_pressure ? "no" : "yes");
  log(buf);
  sprintf(buf, "low oil pressure %s", low_oil_pressure ? "no" : "yes");
  log(buf);
  sprintf(buf, "low fuel %s", low_fuel ? "no" : "yes");
  log(buf);
  sprintf(buf, "tank 1: lim %d float %d valve %s",
    digitalRead(IN_TANK_1_LIM),
    digitalRead(IN_TANK_1_FLOAT),
    tank_1_valve_open ? "open" : "closed");
  log(buf);
  sprintf(buf, "tank 2: lim %d float %d valve %s",
    digitalRead(IN_TANK_2_LIM),
    digitalRead(IN_TANK_2_FLOAT),
    tank_2_valve_open ? "open" : "closed");
  log(buf);
  char voltstr[10];
  sprintf(buf, "battery %sV", dtostrf(volts, 2, 2, voltstr));
  log(buf);

// check if fuel is low, shutdown (if pump is running) and turn on low fuel alert
  if (!low_fuel) {
   log("fuel low");
   low_fuel_alert("alert");   
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



/*  stop button causes alert which means we do nothing forever until the
*   reset button is pressed. change back to auto mode.
*/
 
    if (pump_is_now_running && !stop_button) {
      log("stop button pressed");
      log("turn auto mode on");
      mode_auto = 1;
      stop_pump_alert();
    }

  //  if the pump is NOT running & time clock is ON and in Auto Mode
  //  auto pump mode only reads the tank floats
    
  quiet_time = digitalRead(IN_TIME_CLOCK);

  if (!pump_is_now_running && !quiet_time && mode_auto) {
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
          log("pump is running, water and oil pressure is good");
    }  
      if (no_water_pressure) {
          log("pump is running, BUT water pressure is not good");
          low_water_pressure_alert("alert");
    }
      if (low_oil_pressure) {
           log("pump is running, BUT oil pressure is not good");
           oil_pressure_alert("alert");  
     }
  }
/*  should not be needed now
*   while pump is running, shutdown if all float & limit switches are off
*     if (!pump_is_now_running && tanks_full() )
*      do_shutdown();
*/

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
