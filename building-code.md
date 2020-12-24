# Building the Code

The details differ depending on which microcontroller board is used.

## Silicon Labs Thunderboard Sense 2

The Thunderboard Sense 2 uses the Mbed development environment.

### Setting up the Development Environment
The steps here will centre around using Mbed, which is an online, browser-based development environment that doesn’t need any PC application to be installed. The usual web browser (such as Chrome) is used, and the code is typed within the browser. A click of a button will compile and build the code, and download it to your PC from the web. Next, you plug in the Thunderboard Sense 2 board into your PC (it has a USB connection) and you can drag-and-drop the built code into the USB storage space that will appear, just as if it were a USB memory stick.

First off, go to https://os.mbed.com/ and create a free account. Then from the top select Hardware->Boards and search or filter for the Thunderboard Sense 2. Click on it to go to the dedicated page for it, and you can bookmark it so that you can refer to it in future. It has useful links and pinout information for the board. On the right side of the page select the button to add the board to your compiler, and then open the Mbed Compiler. 

Create a new program, and select the Thunderboard as the platform and choose an example program. You’ll eventually see a main.cpp file listed. Click on it, and the contents will appear in an editor inside the browser! Now you can edit the code, save it, and press Compile when you’re ready to try it out. If the code compiles successfully then a .bin file will automatically be downloaded by the browser, into your usual downloaded items folder. Then you can drag-and-drop the .bin file into the Thunderboard folder as described earlier.

For now, just to test the process, you could immediately hit Compile without making any changes to main.cpp, and see the built code download onto your PC, and then you can drag it into the Thunderboard folder.

