lib has a pure C header file for compat w/ openocd and test firmware. It is the true source of information:
Command enum
...


test firmware portion should be built as a class, which can be instantiated multiple times to support 'parallel' programming of multiple devices
on the board (mcu flash, wifi flash, touch controller). Each instance is given a consecutive `interface numebr`, used to select it via the paranmeters


Parameter structure:
Status / Code: 32 -  lower 16 are status, upper 16 are return code
Command: 32
Address / P1: 32
Length / P2: 32
Buffer Address / P3: 32
Interface: 32

In addition to the parameter structure, a chunk of RAM is allocated on the MCU to act as a copy buffer. There must be


Commands:
Query Buffer: Args: none. Return: the buffer size and number of buffers in P1 and P2, respectively
Query Commands: TODO

Mass erase: Erase all flash. Args: address
Section erase: TODO

Program init: Called before each new section. Args: section start address, section length
Program: Program up to one buffers worth of data. Args: flash address, length, buffer address
Program finalize: Called at the end of each section. Args: none 
 
