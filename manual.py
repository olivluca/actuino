#!/usr/bin/python

# Simple curses based program to control the actuator
# It connects to a tcp socket provided by the companion sermux.py script
# which in turn communicates with the arduino controlling the actuator
# through a serial port

import curses
import curses.textpad

import socket
import sys
import time

HOST, PORT = sys.argv[1], 12345

statuses=('Stopped','Moving west','Moving east')
errors=('No error','Position lost','East limit','West limit','Not moving (no pulses)')

def statusstr(st):
  try:
    return statuses[int(st)]
  except:
    return '?'+st  
    
def errorstr(st):
  try:
    return errors[int(st)]
  except:
    return '?'+st  

class cursclass():
 
  def __init__(self):
    self.setstatus('')
    # Create a socket (SOCK_STREAM means a TCP socket)
    self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)

  def setstatus(self,status):
    self._status=status
    self._statustime=time.time()
    
  def showstatus(self):
    if self._status!='':
      if time.time()-self._statustime>5:
        self._status=''
    self.stdscr.addstr(15,1,self._status); self.stdscr.clrtoeol()
    
  def sendcommand(self,command,status=True):
    self.sock.sendall(command+'\n')
    reply=self.sock.recv(1024).strip()
    if status:
      self.setstatus(reply)
    return reply  
    
  def readint(self,prompt,command,status=True):
    self.stdscr.addstr(15,1,prompt); self.stdscr.clrtoeol()
    self.stdscr.addstr(16,1,""); self.stdscr.clrtoeol()
    self.stdscr.refresh()
    w=curses.newwin(1,20,16,1)
    s=curses.textpad.Textbox(w).edit()
    del(w)
    try:
      i=int(s)
    except:
      self.setstatus("Invalid integer")
      return
    finally:  
      self.stdscr.addstr(16,1,""); self.stdscr.clrtoeol()
    self.sendcommand(command+s,status)  
        
  def cursfunc(self,stdscr):    
    curses.halfdelay(1)
    self.window=curses.newwin(3,20,20,1)
    self.stdscr=stdscr
    stdscr.addstr(1,1,"q=quit, (alt)left/right arrow=move, space=halt, +-=set west/east limits")
    stdscr.addstr(2,1,"g=goto, p=set position, e=enable d=disable limits, l=lock, u=unlock")
    stdscr.addstr(3,1,"------------------------------------------------------------------------")
    # Connect to server and send data
    self.sock.settimeout(0.5)
    self.sock.connect((HOST, PORT))
    self.sock.settimeout(None)
    while True:
        c=stdscr.getch()
        if c==ord('q'):
          break
        elif c == ord(' '):
          self.sendcommand("H")
        elif c == curses.KEY_RIGHT:
          self.sendcommand(">0")    
        elif c == curses.KEY_LEFT:
          self.sendcommand("<0")    
        elif c == 558: #alt+right
          self.readint("Steps",">")    
        elif c == 543:
          self.readint("Steps","<")    
        elif c == ord('g'):
          self.readint("Target","G",False)    
        elif c == ord('p'):
          self.readint("Position","P")
        elif c == ord('+') or c == ord('-') or c == ord('e') or c == ord('d'):
          self.sendcommand(chr(c).upper())
        elif c == ord('l'):
          self.sendcommand('LOCK')
        elif c == ord('u'):
          self.sendcommand('UNLOCK')
        received = self.sendcommand("?",False)
        if received!='ERR':
          (s,e,target,position,eastlimit,westlimit,limitsenabled,freeram)=received.split(',')
          status=statusstr(s)
          error=errorstr(e)
        else:
          status="ERR"
          error=''
          target=''
          position=''
          eastlimit=''
          westlimit=''
          limitsenabled=''
          freeram=''    
        stdscr.addstr(4,1,"Status:  "+status); stdscr.clrtoeol()
        stdscr.addstr(5,1,"Error:   "+error); stdscr.clrtoeol()
        stdscr.addstr(6,1,"Target:  "+target); stdscr.clrtoeol()
        stdscr.addstr(7,1,"Position:"+position); stdscr.clrtoeol()
        stdscr.addstr(8,1,"E limit: "+eastlimit); stdscr.clrtoeol()
        stdscr.addstr(9,1,"W limit: "+westlimit); stdscr.clrtoeol()
        stdscr.addstr(10,1,"Free ram:"+freeram); stdscr.clrtoeol()
        stdscr.addstr(11,1,limitsenabled == '1' and "Limits enabled" or "Limits disabled"); stdscr.clrtoeol()
        self.showstatus()
           
    self.sock.close()

cl=cursclass()
curses.wrapper(cl.cursfunc)
