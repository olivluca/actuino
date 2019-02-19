#include "motor.h"

Motor::Motor(int pinwest, int pineast, int active)
{
  m_pinwest=pinwest;
  m_pineast=pineast;
  m_active=active;
  digitalWrite(m_pinwest,!m_active);
  pinMode(m_pinwest, OUTPUT);
  digitalWrite(m_pineast,!m_active);
  pinMode(m_pineast, OUTPUT);
  lastdir=MD_STOP;
  stopped=true;
  m_lastwest=false;
  Stop();
}

void Motor::Stop()
{
  newdir=MD_STOP;
  /* the relay module I'm using is active with output low, high turns off the relays */
  digitalWrite(m_pinwest,!m_active);
  digitalWrite(m_pineast,!m_active);
  moving=false;
  if (!stopped) {
    //Serial.println("wait");
    stopped=true;
    wait=millis();
  }
}

void Motor::internalGo()
{
    /* the relay module I'm using is active with output low, high turns off the relays */
    digitalWrite(m_pinwest, lastdir==MD_WEST ? m_active : !m_active);
    digitalWrite(m_pineast, lastdir==MD_EAST ? m_active : !m_active);
    stopped=false;
    moving=lastdir!=MD_STOP;
    if (lastdir==MD_WEST)
      m_lastwest=true;
    if (lastdir==MD_EAST)
      m_lastwest=false;  
}

void Motor::Go(motor_direction dir)
{
  //Serial.print("motor go lastdir ");
  //Serial.print(lastdir);
  //Serial.print(" newdir ");
  //Serial.println(dir);
  if (dir!=lastdir && lastdir!=MD_STOP) {
    //Serial.println("Stop in go");
    Stop();
  }
  newdir=dir;
  if (newdir == lastdir || lastdir == MD_STOP) {
    //Serial.println("internalgo in go");
    lastdir=newdir;
    internalGo();
  } 
}

void Motor::Loop()
{
  if (lastdir!=MD_STOP && stopped) {
    if (millis()-wait>=1000) {
      lastdir=MD_STOP;
      //Serial.println("stopped");
    }  
  } 
  if (newdir!=MD_STOP && lastdir==MD_STOP) {
    lastdir=newdir;
    //Serial.println("internalgo in loop");
    internalGo();
  }
}
