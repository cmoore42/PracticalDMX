# PracticalDMX
A battery powered standalone sACN node for remotely running on-stage practical lights.

The inspiration for this project came from a show I worked on that required lights on a moving platform.  
The lights were "practicals" - lights on stage that need to look like real world lights.
This is usually done by having the lights controlled by the lighting console, and having the console operator take
a cue to turn on the light when an actor flips a non-functional switch.
However, in this case, the lights were on moving platforms so couldn't be wired to either power or control.
The solution was a battery powered system with a 12V lead acid battery, a switch, and a 2W 12V light fixture.  
This meant that the actors had to actually flip a real switch to turn on the light.

This project is an sACN version of that.  It's a module that is powered by a 12V battery, can run 12V lights, 
but appears as an sACN node.
The lights can be completely portable, but can be controlled by a lighting console.

# Software
- Network configuration using WiFiManager
- sACN receive functionality using E131 library


# Hardware
- NodeMCU Devkit
- 0.96" I2C OLED display (From DIYMall)
- Custom PCB
- Four FQP30N06L N-Channel MOSFETs (From Sparkfun)
- Four 10K resistors
- Various connectors, headers, sockets

# Project Status
- PC Board designed and ordered
- PC Board built and tested with 12V input power
- Software done and tested, as much as possible without hardware

# To Do List
- Add code to allow for setting of universe and address offset
- Test PC board with actual 12V LED lights
- Add overcurrent protection to circuit
- Create 3D printable case
