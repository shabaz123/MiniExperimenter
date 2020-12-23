# MiniExperimeter
Sensing and capturing data for school science experiments
This project connects a Casio calculator to an off-the-shelf sensor/microcontroller board. The aim is to allow sensed data to be captured and charted by the calculator.
This repository contains the code that will run on the sensor/microcontroller board. The board connects to the calculator using a 3-pin connector. The calculator contains built-in data reporting and charting functions, so no calculator programming is required.

Note: The code in this repository is a work-in-progress. It partially works.
The working functionality as of December 2020 is:
* Ability to report the ambient light level
* Ability to chart the ambient light level
The known issues are:
* The light level is currently in arbitrary units, it is not in Lux
* The time axis on the charts is not usable, because the code just sends the measured light level as it becomes available, and not at the required intervals. To fix this, a timer needs to be implemented in the code.
* If the calculator sends an error message, the code doesn't recover gracefully in all circumstances, because code still needs to be written to gracefully recover in all states. This means that the Reset button on the microcontroller board needs to be pressed to manually recover.
* Prior to capturing the data for charting purposes, the Casio calculator sends some setup information. Occasionally this doesn't work, and the calculator sends an error response to the microcontroller board, and the calculator displays an error message. When this occurs, the user needs to press Exit on the calculator, and then reattempt. I don't know the reason why the calculator generates an error.
* High speed capture (less than 0.2 seconds per sample) is not currently possible, because the Casio calculator uses a slightly different mechanism (a non-real-time bulk streaming of data) for high speed, and this is undocumented, and experiments so far have been unsuccessful and reverse-engineering it.
* Only a single channel and particular mode can be configured. Currently the code cannot chart different channels. The calculator needs to be set to Channel 1, Voltage mode. Other channels and modes are not recognized by the microcontroller code currently.
