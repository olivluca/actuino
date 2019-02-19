#ifndef Motor_h
#define Motor_h
#include "Arduino.h"

enum motor_direction {
     MD_STOP=0,
     MD_WEST,
     MD_EAST
     };
     
class Motor
{
public:
  Motor(int pinwest, int pineast, int active);
  void Stop();
  void Go(motor_direction dir);
  void Loop();
  motor_direction LastDirection() { return lastdir;}
  bool Moving() { return moving;}
  bool Lastwest() { return m_lastwest;}
private:
  int m_pinwest;
  int m_pineast;
  int m_active;
  bool m_lastwest;
  motor_direction newdir;
  motor_direction lastdir;
  bool stopped;
  bool moving;
  unsigned long wait;
  void internalGo(void);  
};


#endif
