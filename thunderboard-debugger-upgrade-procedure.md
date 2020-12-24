# Thunderboard Sense 2 Debugger Upgrade Procedure
The Thunderboard Sense 2, like other Mbed-enabled boards, actually contains two microcontrollers. One microcontroller is dedicated to acting as a debugger/programmer, and the other microcontroller is the chip where your target code will run.

With new Thunderboards, it is likely that the firmware for the debugger microcontroller needs upgrading, before you can transfer the target code into the main microcontroller.

The prodecure here can be used to perform the upgrade. 

First download a development environment called Simplicity Studio 5 from https://www.silabs.com/developers/simplicity-studio

In Windows 10 you can right-click the downloaded .iso file to mount it, and then double-click on setup.exe to kick off the installation.
Plug in the Thunderboard Sense 2, so that Simplicity Studio can automatically install based on the connected device, and then accept all the defaults until the Simplicity Studio installation is complete. After the software restarts, you should see a main workspace called v5_workspace appear, and the pane on the left side will list the Thunderboard Sense 2 as the connected debug adapter. Click on it, and then in the large main pane under General Information you should see J-Link Silicon Labs as the connection. Click on Configure.


