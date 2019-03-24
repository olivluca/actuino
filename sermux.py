#!/usr/bin/python

# An arduino, connected to a serial port, controls a satellite actuator.
# This script listens on a tcp socket (12345 by default), sends to the serial
# port what has received and sends back the reply from the serial port.
# This script is needed to allow more than one program to control the
# actuator at the same tome.
# The serial port to open is specified as the parameter of the script.
# Other optional parameters are 
#   --port, -p   to specify the tcp port
#   --test, -t   test mode, doesn't use the serial port

import time
import threading
import SocketServer
import signal
import sys
import traceback
import serial
import argparse
from kodi.xbmcclient import *

statuses=('Parado','Moviendo a oeste','Moviendo a este')
errors=('','Posicion perdida','Limite este','Limite oeste','No se mueve (no hay pulsos)')

class actuator:
   def __init__(self,port,test=False):
     self.test=test
     self.lock=threading.RLock()
     self.opened=False;
     self.port=port
     self.exclusive=0
     self.sock=socket(AF_INET,SOCK_DGRAM)
     self.olds=''
     self.olde=''
     self.oldtarget=''
     self.oldposition=''
   
   def open(self):
     if not self.test:
       self.lock.acquire()
       try:
         self.serial=serial.Serial(self.port, 115200, timeout=1)
         self.opened=True
       except:  
         traceback.print_exc()
       finally:
         self.lock.release()  
   
   def checkportloop(self):
     if not self.test:
       while True:
         time.sleep(1.5)
         self.lock.acquire()
         try:
           reply=self.sendrec('?\n').strip()
           if reply=='ERR' or reply=='LOCKED':
             self.olds=''
             continue
           (s,e,target,position,eastlimit,westlimit,limitsenabled,freeram)=reply.split(',')
           if s!=self.olds or e!=self.olde or target!=self.oldtarget or position!=self.oldposition:
             self.olds=s
             self.olde=e
             self.oldtarget=target
             self.oldposition=position
             err=int(e)
             if err==0:
               msg=statuses[int(s)]
             else:
               msg=errors[err]
             packet=PacketACTION(actionmessage="Notification(Parabolica,%s\nDestino %s posicion %s,1500,/storage/actuator/parabolica.png)" % (msg,target,position), actiontype=ACTION_BUTTON)
             packet.send(self.sock,('localhost',9777))
         except:
           traceback.print_exc()
         finally:    
           self.lock.release()
         
   def close(self):
     if not self.test:
       self.lock.acquire()
       if self.opened:
         self.opened=False
         self.serial.close()
       self.lock.release()  
   
   def sendrec(self,send):
     if self.test:
       #return send.upper()
       return "s,e,100,200,el,wl,le,freeram"
     result=''
     self.lock.acquire()
     try:
       tid=threading.current_thread().ident
       if send=="LOCK\n":
         if self.exclusive==0 or self.exclusive==tid:
           self.exclusive=tid
           return "Locked\n"
         else:
           return "Already locked\n"
       if send=="UNLOCK\n":
         if self.exclusive==0 or self.exclusive==tid:
           self.exclusive=0
           return "Unlocked\n"
         else:
           return "Not locked by this client\n"
       if self.exclusive!=0 and self.exclusive!=tid and send!='?\n':
         return "LOCKED\n"
       if not self.opened:
         self.open()
       if self.opened:
         try:  
           self.serial.write(send)
           result=self.serial.readline()  
         except:
           traceback.print_exc()
           self.close()
           result=''  
     finally:
       self.lock.release()
     if result=='':  
       result='ERR\n'  
     return result  
             

parser = argparse.ArgumentParser(description= "TCP socket <-> serial actuator multiplexer")
parser.add_argument("serial", help="serial port connected to the actuator")
parser.add_argument("-p","--port", help="tcp port to listen to (default 12345)", default=12345,
                    type=int)
parser.add_argument("-t", "--test", help="echo the input without using the serial port", default=False,
                    action="store_true")
args = parser.parse_args()

ac=actuator(args.serial,test=args.test)
checkthread=threading.Thread(target=ac.checkportloop)
checkthread.setDaemon(True)
checkthread.start()

def signal_handler(sig, frame):
   print "Terminating"
   sys.exit(0)
   
signal.signal(signal.SIGINT, signal_handler)
signal.signal(signal.SIGTERM, signal_handler)         

class Handler(SocketServer.StreamRequestHandler):
  def setup(self):
    print "New connection from ", self.client_address
    return SocketServer.StreamRequestHandler.setup(self)
    
  def handle(self):
    while True:
     try:
       data = self.rfile.readline()
       if not data:
         break
     except:
       traceback.print_exc()
       break
     reply=ac.sendrec(data)
     self.wfile.write(reply)
       
  def finish(self):
    print "Closed connection from",self.client_address
    ac.sendrec("UNLOCK\n") #in case this connection locked the serial port
    return SocketServer.StreamRequestHandler.finish(self)
       
class ThreadedTcpServer(SocketServer.ThreadingMixIn, SocketServer.TCPServer):
  pass
  
SocketServer.TCPServer.allow_reuse_address=True
server = ThreadedTcpServer(('',args.port), Handler)
server.daemon_threads = True
server.serve_forever()
