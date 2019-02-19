#include <I2C.h>
#include <avr/eeprom.h>
#include <avr/pgmspace.h>
#include <stdlib.h>
#include "motor.h"

#define GO_WEST 10
#define GO_EAST 11
#define REED 12 
#define LED 13

#define CMD_HALT             'H'
#define CMD_DISABLE_LIMITS   'D'
#define CMD_ENABLE_LIMITS    'E'
#define CMD_SET_EAST_LIMIT   '-'
#define CMD_SET_WEST_LIMIT   '+'
#define CMD_GO_EAST          '<'
#define CMD_GO_WEST          '>'
#define CMD_GO_POSITION      'G'
#define CMD_SHOW_STATUS      '?'
#define CMD_FORMAT           'X'
#define CMD_SET_POSITION     'P'


enum errors {
  ERR_NOERROR=0,
  ERR_LOST_POS,
  ERR_LIMIT_EAST,
  ERR_LIMIT_WEST,
  ERR_NOT_MOVING
};  

enum statuses {
  STS_STOPPED,
  STS_MOVING_WEST,
  STS_MOVING_EAST
};


enum params {
  limit_east,
  limit_west,
};

Motor motor(GO_WEST,GO_EAST,LOW);
int16_t target;
int16_t position;
int16_t param[limit_west+1];

byte write_sequence;

bool limits_enabled;
bool positioning;
bool reached;
errors error;

unsigned int timeout;
unsigned int halt_timer;

char recvbuffer[32];
int recvcount;
unsigned long recvtime;
unsigned long blink;

#define FRAM_ADDRESS (0x50)
#define SLAVE_ID (0xF8)

bool recv()
{
    char c;
    while (Serial.available()) {
        c=Serial.read();
        if (c=='\r' || c=='\n') {
            if (recvcount>0) {
                recvbuffer[recvcount]=0;
                recvcount=0;
                return true;
            }
        } else {
            if (recvcount<30) { //leave one byte for the termination
                recvbuffer[recvcount++]=c;
                recvtime=millis();
            }
        }
    }
    if (recvcount>0) 
        if (millis()-recvtime>1000)
            recvcount=0;
    return false;    
}

void check_fram(void)
{
    uint8_t a[3];
    uint16_t manufacturerID = 0;
    uint16_t productID = 0;
    
    if (I2c.write(SLAVE_ID >> 1, FRAM_ADDRESS << 1, true)!=0) {
        Serial.println("check_fram - error I2c.write");
    } else   
    if (I2c.read(SLAVE_ID >> 1, 3, a)!=0) {
        Serial.println("check_fram - error I2c.read ");
    } else { 
      manufacturerID = (a[0] << 4) + (a[1]  >> 4);
      productID = ((a[1] & 0x0F) << 8) + a[2];
    }
    if (manufacturerID!=0x0a || productID!=0x10) {
        if (manufacturerID!=0x0a) {
            Serial.print(F("Wrong manufacturerID 0x"));
            Serial.println(manufacturerID, HEX);
        }
        if (productID!=0x10) {
            Serial.print(F("Wrong productID 0x"));
            Serial.println(productID, HEX);
        }
        while(true) {
         digitalWrite(LED, HIGH);
         delay(100);
         digitalWrite(LED, LOW);
         delay(100);
        }
    }
}

uint8_t write_fram(uint16_t address, uint8_t *data, uint8_t len)
{
  uint8_t addr=FRAM_ADDRESS;
  if (address>0xff)
    addr|=1;
  return I2c.write(addr, address & 0xff, data, len);  
}

uint8_t read_fram(uint16_t address, uint8_t *data, uint8_t len)
{
  uint8_t addr=FRAM_ADDRESS;
  if (address>0xff)
    addr|=1;
  return I2c.read(addr, address & 0xff, len, data);  
}

/* write sequence is written before and after the position to
 * detect power loss during writing
 */
void save_position(void)
{
  byte buffer[4];
  write_sequence++;
  if (write_sequence==0)
    write_sequence=1;
  buffer[0]=write_sequence;
  buffer[1]=position >> 8;
  buffer[2]=position & 0xff;
  buffer[3]=write_sequence;
  write_fram(0, buffer, sizeof(buffer));
}

void clear_position(void)
{
  byte buffer=0;
  write_fram(0,&buffer,sizeof(buffer));
  error=ERR_LOST_POS;
}

void read_position(void)
{
  byte buffer[4];
  error=ERR_LOST_POS;
  position=0;
  /* if write_sequence is the same the position is valid */
  if (read_fram(0, buffer, sizeof(buffer))==0 && buffer[0]!=0 && buffer[0]==buffer[3]) {
    write_sequence=buffer[0];
    position=buffer[1] << 8 | buffer[2];
    error=ERR_NOERROR;
  }
}

#define OFFSET_PARAMS 4

void check_and_format_eeprom(void)
{
  union {
    char chars[4];
    uint32_t num;
  }  magic = { .chars = {'A','C','T','U'}};
  
  uint32_t readmagic;
  uint32_t readmagic2;
  uint32_t zero=0;
  unsigned int maxeprom=(OFFSET_PARAMS+sizeof(param));
  
  readmagic=eeprom_read_dword((uint32_t *)0);
  readmagic2=eeprom_read_dword((uint32_t *)maxeprom);
  if (magic.num==readmagic && magic.num==readmagic2)
    return;
  for (unsigned int i=4; i<maxeprom; i+=4) 
    eeprom_write_dword((uint32_t*) i, zero);
  eeprom_write_dword((uint32_t*)0, magic.num);  
  eeprom_write_dword((uint32_t*)maxeprom, magic.num);  
}

void save_param(int param_to_save) {
  eeprom_write_word((uint16_t*) (param_to_save*2+OFFSET_PARAMS), param[param_to_save]);
}

void read_params(void) {
  eeprom_read_block(&param, (void *) OFFSET_PARAMS, sizeof(param));
}

/* timeout between pulses */
void set_timeout(void)
{
  timeout=5000;
}

/* if no pulses are received while the motor is moving, stop the
 * motor and register the error
 */
void check_moving(void)
{
  if (!motor.Moving()) {
    set_timeout();
    return;
  }  
  if (--timeout)
    return;
  motor.Stop();
  positioning=false;
  //avoid clearing ERR_LOST_POS here
  if (error != ERR_LOST_POS)
    error=ERR_NOT_MOVING; 
}  

/* each ms */
SIGNAL(TIMER0_COMPA_vect)
{
  
  static int lastreed=HIGH;
  static unsigned int debounce=10;
  static int lastdebounced=HIGH;
  
  /* timer go east or go west */
  if (halt_timer!=0) {
    if (--halt_timer==0)
      motor.Stop();
  }
  /* read and debounce pulses */
  int reed=digitalRead(REED);
  if (reed==lastreed) {
     if (debounce) debounce--;
     else {  //debounced
        if (reed==LOW && reed!=lastdebounced) { //new pulse
            /* here we count pulses even with the position invalid to allow for 
             * manual movement, but don't save the position to fram
             */
            if (motor.LastDirection() == MD_WEST) { 
              position++;
              if (error != ERR_LOST_POS) {
                save_position();
                if (limits_enabled && position>=param[limit_west]) {
                  motor.Stop();
                  error=ERR_LIMIT_WEST;
                  positioning=false;
                }
              }  
            } else if (motor.LastDirection() == MD_EAST) {
              position--;
              if (error != ERR_LOST_POS) {
                save_position();
                if (limits_enabled && position<=param[limit_east])  {
                  motor.Stop();
                  error=ERR_LIMIT_EAST;
                  positioning=false;
                } 
              }  
            }  
            if (positioning && position == target) {
                motor.Stop();
                positioning=false;
                reached=true;
            }
            set_timeout(); 
        }
        lastdebounced=reed;  
     }
  } else {
    debounce=10;  
    lastreed=reed;
  }
  check_moving();
}

int freeRam () {
  extern int __heap_start, *__brkval;
  int v;
  return (int) &v - (__brkval == 0 ? (int) &__heap_start : (int) __brkval);
}

void setup()
{
  //Init i2c master library
  I2c.begin();

  I2c.setSpeed(true);
  // initialize the serial communication:
  Serial.begin(115200);
  Serial.setTimeout(100);
  // initialize the ledPin as an output:
  pinMode(REED, INPUT_PULLUP);
  pinMode(LED, OUTPUT);
  check_fram();
  check_and_format_eeprom();
  read_position(); //it will either set the error to ERR_NOERROR or ERR_LOST_POS
  read_params();
  limits_enabled=true;
  positioning=false;
  reached=false;
  halt_timer=0;
  blink = millis();
  
  //enable 1ms interrupt
  OCR0A = 0xAF;
  TIMSK0 |= _BV(OCIE0A);
}

byte getdisplaystatus(void) {
  if (!motor.Moving()) 
      return STS_STOPPED;
  if (motor.LastDirection()==MD_WEST)
   return STS_MOVING_WEST;
  return STS_MOVING_EAST;
}

void separator()
{
    Serial.write(',');
}
void show_status() 
{  
  Serial.print(getdisplaystatus()); separator();
  Serial.print(error); separator();
  Serial.print(target); separator();
  Serial.print(position); separator();
  Serial.print(param[limit_east]); separator();
  Serial.print(param[limit_west]); separator();
  Serial.print(limits_enabled); separator();
  Serial.println(freeRam());
}

void loop() {
  char command;
  int parameter;
      
  motor.Loop();
  
  command=0;
  
  //commands over serial
  if (recv()) 
  {
    command=recvbuffer[0];
    parameter=atoi(recvbuffer+1);
    switch (command) {
      case CMD_FORMAT:
        Serial.println(F("deleting magic and parameters"));
        eeprom_write_dword((uint32_t*)0, 0);  
        break;   

      case CMD_HALT:
        Serial.println(F("halt"));
        halt_timer=0;  
        motor.Stop();
        positioning=false;
        reached=false;
        if (error != ERR_LOST_POS)
          error=ERR_NOERROR;
        break;

      case CMD_DISABLE_LIMITS:
        limits_enabled=false;
        Serial.println(F("Limits disabled"));
        break;

      case CMD_ENABLE_LIMITS:
        limits_enabled=true;
        Serial.println(F("Limits enabled"));
        break;
        
      case CMD_SET_EAST_LIMIT: 
        if (error != ERR_LOST_POS) {
          param[limit_east]=position;
          save_param(limit_east);
          Serial.print(F("Set east limit "));
          Serial.println(param[limit_east]);
        } else {
          Serial.println(F("Limit not set (position lost)"));
        }
        break;

      case CMD_SET_WEST_LIMIT:
        if (error != ERR_LOST_POS) {
          param[limit_west]=position;
          save_param(limit_west);
          Serial.print(F("Set west limit "));
          Serial.println(param[limit_west]);
        } else {
          Serial.println(F("Limit not set (position lost)"));
        }
        break;

      case CMD_GO_EAST:
      case CMD_GO_WEST:
        bool go_west;
        go_west=command==CMD_GO_WEST;
        if (parameter>0) {
          target=position+parameter*( go_west ? 1 : -1);
          positioning=true;
          reached=false;
          halt_timer=0;
        } else {
          halt_timer=-parameter;
          positioning=false;
        }  
        if (positioning) {
         Serial.print(parameter);
         Serial.print(F(" steps "));  
        } else {
         Serial.print(-parameter);
         Serial.print(F(" ms ")); 
        }
        Serial.println(go_west ? F("west") : F("east"));
        motor.Go(go_west ? MD_WEST : MD_EAST);
        error=ERR_NOERROR;
        break;

      case CMD_GO_POSITION:
        if (error != ERR_LOST_POS) {
          reached=false;
          error=ERR_NOERROR;
          if (limits_enabled && parameter<param[limit_east])
            error=ERR_LIMIT_EAST;
          else if (limits_enabled && parameter>param[limit_west])
            error=ERR_LIMIT_WEST;
          if (error==ERR_NOERROR)
          {
            positioning=true;
            halt_timer=0;
            target=parameter;
            if (target>position) motor.Go(MD_WEST);
            else if (target<position) motor.Go(MD_EAST);
            else {
              motor.Stop();
              positioning=false;
              reached=true;
             }
          }
        }
        show_status();
        break;

      case CMD_SET_POSITION:
        position=parameter;
        save_position();
        error=ERR_NOERROR;
        Serial.print(F("Position set to "));
        Serial.println(position);
        break;
        
      case CMD_SHOW_STATUS:
        show_status();
        break;
    }
  } //if recv()  
  
  if (error==ERR_NOERROR)
    digitalWrite(LED, HIGH);
  else {
      digitalWrite(LED, millis()-blink<250);
      if (millis()-blink>500)
        blink=millis();
  }
        
}
