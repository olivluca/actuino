#Actuino
This is an arduino program, schematics and utilities to control an old stile linear or horizon to horizon actuator to steer a satellite dish.
This kind of actuator usually needs a 36V supply (I drive mine at 12V to reduce noise) and has a reed relay that gives pulses while the motor is running. By counting the pulses you know the position of the actuator (and the position of the dish).

A previous iteration used a parallel port and a Linux device driver, but since it's increasingly difficult to find a PC with a parallel port and there's an abundance of cheap TV boxes with usb ports,
I decided to write an arduino version controlled with an usb to serial cable.

I'm using it with a tv box running kodi and tvheadend (the trunk version of tvheadend can use an external command to move the dish).

#Components

* an arduino pro mini (atmega 168, but you can use another model)
* a 2 relays module,
* an usb to ttl adapter (to power the arduino and the relays)
* an fram chip FUJITSU MB85RC04V to store the satellite position
* an optocoupler to decouple the reed sensor from the arduino



#Schematics
I made the schematics in Fritzing (the file actuino.fzz).
I also provide a png rendering of the [breadboard view](https://bitbucket.org/olivluca/actuino/src/default/actuino_bb.png) and [schematics view](https://bitbucket.org/olivluca/actuino/src/default/actuino_schem.png) .

 [Breadboard view


#Arduino program

In order to avoid burning the internal EEPROM, I use an FRAM chip to keep the current position of the dish. 
The chip is connected using I2C and I'm using the I2C-Master-Library, however I had to fork it in order to add functions to identify the chip.
My forked version is available on [github](https://github.com/olivluca/I2C-Master-Library).

I decided that the count increases when the dish moves west and decreases when it moves east.

##Commands

Usually you'll use the provided utilities to move the dish, but for debugging purposes you can send commands directly with a terminal program (though you'll have to type them quickly since the arduino will time out otherwise).
The commands are a single character, some of the commands need a parameter. There is no space between the command and the parameter. The commands are terminated with CR or LF.

The available commands are:

* **H**  stops the motor
* **D** disables the software limits
* **E** enables the software limits
* **\-** stores the actual position as the east limit
* **\+** stores the actual position as the west limit
* **<** *parameter* moves the dish east. A positive parameters specifies the number of steps the dish has to move, while a negative parameter specifies the duration of the movements in ms
* **\>** *parameter* moves the dish west. Like < it accepts a positive or negative number as a parameter.
* **G** *parameter* moves the dish to the specified position
* **?** shows the current status (see below)
* **X**  clears the internal EEPROM (which only stores the limits)
* **P** *parameter* sets the current position

##Status

The status returned by ? is a line with various fields separated with commas. The format of the line is

**status code,error code,target,position,east limit,west limit,limits enabled,free ram**

The status code is 0 with the dish stopped, 1 when moving west and 2 when moving east.
The possible error codes are:

* 0 no error
* 1 position lost (could be a faulty fram or a write operation interrupted by a power loss)
* 2 east limit reached
* 3 west limit reached
* 4 dish not moving (no pulses from the reed switch)

#Utilities

These are some python scripts useful for controlling the dish:

* **sermux.py** multiplexes the serial port to multiple tcp connections
* **manual.py** allows to move the dish manually with a curses interface

##sermux.py

Since only one program at a time can open the serial port, this script keeps the port open and allows other programs to control the actuator through
a TCP connection.
This way multiple programs can control the actuator at the same time (though care should be taken to avoid conflicting movements).
It also sends status messages to the running kodi instance at localhost to give some feedback about the dish movement.
By default it listens on tcp port 12345, but you can specify a different port with the --port parameter.
It should be left running all the time.

##manual.py

This program provides a curses interface to control the dish. It connects to the host specified as its parameter, port 12345. It gives a list
of the available options and shows the current status of the actuator.
Also, it allows to lock the serial port to avoid other clients to move the dish (they can still check the status).







