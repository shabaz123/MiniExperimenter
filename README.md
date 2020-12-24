# MiniExperimeter
## Sensing and capturing data for school science experiments

This project connects a Casio calculator to an off-the-shelf sensor/microcontroller board. The aim is to allow sensed data to be captured and charted by the calculator.
This repository contains the code that will run on the sensor/microcontroller board. The board connects to the calculator using a 3-pin connector. The calculator contains built-in data reporting and charting functions, so no calculator programming is required.

<img src="images/casio-chart.jpg" width="320" style="float:left">

Note: The code in this repository is a work-in-progress. It partially works.
The working functionality as of December 2020 is:
* Ability to report the ambient light level
* Ability to chart the ambient light level
<img src="images/casio-report.jpg" width="320" style="float:left">

The known issues are:
* The light level is currently in arbitrary units, it is not in Lux
* The time axis on the charts is not usable, because the code just sends the measured light level as it becomes available, and not at the required intervals. To fix this, a timer needs to be implemented in the code.
* If the calculator sends an error message, the code doesn't recover gracefully in all circumstances, because code still needs to be written to gracefully recover in all states. This means that the Reset button on the microcontroller board needs to be pressed to manually recover.
* Prior to capturing the data for charting purposes, the Casio calculator sends some setup information. Occasionally this doesn't work, and the calculator sends an error response to the microcontroller board, and the calculator displays an error message. When this occurs, the user needs to press Exit on the calculator, and then reattempt. I don't know the reason why the calculator generates an error.
* High speed capture (less than 0.2 seconds per sample) is not currently possible, because the Casio calculator uses a slightly different mechanism (a non-real-time bulk streaming of data) for high speed, and this is undocumented, and experiments so far have been unsuccessful and reverse-engineering it.
* Only a single channel and particular mode can be configured. Currently the code cannot chart different channels. The calculator needs to be set to Channel 1, Voltage mode. Other channels and modes are not recognized by the microcontroller code currently.
<img src="images/casio-comm-error.jpg" width="320" style="float:left">

## Using the Project
To use the project, at a high level, there are these main steps which are described in more detail later:
* [Solder a 3-pin connector](hardware-connections.md) to the off-the-shelf sensor/microcontroller board
* [Build the code](building-code.md), to create a .bin file
* [Transfer the code](building-code.md) to the sensor/microcontroller board
* Connect the board to the calculator using the 3-pin connector, and supply power to it (can use a USB supply or plug into a PC)
* On your Casio calculator (which should be running the latest firmware from the Casio website) select Menu->E-CON4. E-CON4 is the built-in application for data capture/logging/charting. You should see a screen titled Time-based Sampling and row 1 will be highlighted (row 1 represents channel 1; the calculator supports multiple data acquisition channels).
* Configure the calculator as follows: Press Shift then Setup, and select the Data Logger to be EA-200 and then press EXE or Exit. ensure row 1 is selected on the screen (it should be by default, otherwise use the cursor keys to select the row number 1) and then press the Sensor soft-key and select CASIO from the list, and then select Voltage from the sub-list that appears. Press the Config soft-key, and set the sampling interval to 0.2 seconds or higher, and the Samples value to something reasonable for a first test, such as 100 samples. Do not set a sampling interval lower than 0.2 seconds, due to one of the known issues listed above. Once you're set, press Exit until you're back on the display titled Time-based Sampling. You should eventually see a voltage value displayed immediately under the row labeled "1:Voltage". It may take around 10 seconds for this to occur. If it doesn't, press the Reset button on the sensor/microcontroller board, and wait another 10 seconds.
* Now that the calculator is reporting the current light level, you can try charting it. Press the Start soft-key. You will see a checklist appear. Press Exe and the calculator will try to set (configure) the sensor/microcontroller board with the configured values such as the interval time and desired number of samples. You may see a Communication Error message (see the list of known issues). If this occurs, press Exit until you're back at the Time-based Sampling screen, wait until you see a measured value reported again, and then try pressing Start again. If you can't get this to work, press the Reset button on the sensor/microcontroller board. Eventually it should work.
* If all goes well, the calculator will have successfully transferred the sampling configuration parameters to the microcontroller board, and you should now see a screen with the text "Start sampling? Press EXE". Press EXE, and the values should be plotted. Try changing the light level around the board, and the chart line should go up or down. Once the chart has plotted the configured number of data points (e.g. 100), you can do things like (say) press the Trace button to see a cursor on the chart, and move left/right with the cursor keys to see each sample value. Press Exit to get out of Trace mode. Press Exit again to get back to the Time-based Sampling screen.



