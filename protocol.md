# Protocols

The Casio calculator communications protocol is partially described in the [Casio EA-200 Technical Reference PDF](https://support.casio.com/en/manual/004/EA200_TechnicalReference_EN.pdf). The EA-200 is a Casio data acquisition unit.
Unfortunately, the documentation is incomplete, and in places hard to follow.

In brief, the layer 1 communication at the physical voltage level works using 3.3V UART Tx/Rx signals (Idle is high, i.e. opposite of RS-232) at 38400 baud, 8 bits, no parity, and 2 stop bits. The connector is a 2.5 mm 3-pin plug. When plugged into the calculator, the tip is CASIO_TX (output signal from the calculator), the ring is CASIO_RX (input to the calculator), and the sleeve is Ground. 

At the next layer, which I've called the 'Low Layer Protocol', bytes of data are sent in bundles (let's call them packets). Usually the first byte in the packet is the header byte ':', and the last byte is a checksum. There are single-byte packets too however, with no header or checksum. It is mostly all described in the EA-200 document mentioned above, with a few things missing unfortunately. The protocol has several procedures, of which two are called Send38K and Receive38K.

Next, there is a 'High Layer Protocol', which uses multiple low-layer procedures to achieve things. 
