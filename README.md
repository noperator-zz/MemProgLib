# MemProgLib - Memory Programming Library

MemProgLib provides a protocol to run commands on a target MCU and transfer data bidirectionally between a host PC and target MCU.
Multiple logical instances are supported to run several commands concurrently

The intended use case is programming flash memory or other memories attached to the target MCU, although it
can be adapted for any task where high speed data transfer between the PC and MCU is required.

MemProgLib consists of three components:
 - Host PC implementation: An implementation of MemProgLib runs on the host PC and is responsible for initiating
commands and memory transfers. An API allows external programs to use the interface
 - Target MCU implementation: An implementation of MemProgLib runs on the target MCU and is responsible for
receiving commands and handling incoming data buffers
 - Target command handlers: MemProgLib commands are handled on the target by command handler functions. These
are responsible for moving data into the appropriate location and providing a return status


## Terminology
- Interface: A logical instance which can run commands concurrently with other interfaces. All interfaces
  use the same parameter structure and buffers
- Parameters: The command parameter structure, MEMPROG_PARAM. Alternatively, the fields with this structure
  may be referred to as parameters
- BDT: Buffer Descriptor Table, MEMPROG_BDT. Holds information about each data transfer buffer


# Usage Guide
## Host Usage
See https://github.com/noperator-zz/openocd/blob/delta/README_MEMPROG.md for usage of the host implementation

## Target Usage
### Setup
MemProgLib is set up on the target by providing definitions for the static variables declared at the top of the MemProg class
Here is a minimal setup snippet:
```c++
#include "memprog.hpp"
// Include the files where the MemProg subclasses are defined (See section on Interface Creation)
#include "FLASHAlgorithm.h"
#include "UPDIResource.h"
#include "WIFIResource.h"

// Define the number of buffers and the size of each buffer
static constexpr uint32_t NUM_BUFFERS = 4;
static constexpr uint32_t BUFFER_SIZE = 0x400;

// Create the required MemProg data structures. Use a linker script to arrange the sections in memory as needed
volatile MEMPROG_PARAM Programming::ALGO_PARAMS __attribute__ ((used, section(".algo_param")));
volatile MEMPROG_BDT Programming::ALGO_BDT[NUM_BUFFERS] __attribute__ ((used, section(".algo_bdt")));
volatile uint8_t Programming::ALGO_BUFFER[BUFFER_SIZE * NUM_BUFFERS] __attribute__ ((used, section(".algo_buffer")));

// Define MemProg::Instances as an array of the MemProg subclasses, followed by a nullptr
MemProg * const MemProg::Interfaces[] {
        &FLASHAlgorithm::ALGO,
        &UPDIResource::ALGO,
        &WIFIResource::ALGO,
        nullptr};

// Define the rest of the MemProg variables as follows
volatile MEMPROG_PARAM * const MemProg::Param = &Programming::ALGO_PARAMS;
volatile MEMPROG_BDT * const MemProg::BufferDescriptors = Programming::ALGO_BDT;
volatile uint8_t * const MemProg::Buffers = Programming::ALGO_BUFFER;
const uint32_t MemProg::BufferSize = Programming::BUFFER_SIZE;
const uint32_t MemProg::NumBuffers = Programming::NUM_BUFFERS;
// A timekeeping function must be provided
uint32_t (* const volatile MemProg::time_ms)() = &SYSTICK::GetTimeMS;

// Define debugging functions as nullptr if not used
void (* const MemProgDebugMixin::dset)(uint8_t, bool) = nullptr;
void (* const MemProgDebugMixin::dputc)(uint8_t) = nullptr;
```

### Interface Creation
The `MemProg` class in `memprog.hpp` acts as an abstract base class. It should not be used directly as it does not provide
implementations for any command handlers.

A new interface is created by subclassing `MemProg` and overriding the command handler stub functions and/or adding
custom command handler functions. Here is a minimal example:
```c++
#include "memprog.hpp"

// Create a subclass of MemProg for each interface
class FLASHAlgorithm : public MemProg {
// Inherit the Base class constructor
using MemProg::MemProg;
public:
    void Init() override {
        // Place any code that should be run one time when the MCU starts up
    };

    // A single static instance of this class must be created.
    // For convenience, we declare the static instance to live inside the class itself.
    // NOTE: Another source file must define the instance as `FLASHAlgorithm FLASHAlgorithm::ALGO;`
    static FLASHAlgorithm ALGO;

private:
    void CMD_MASS_ERASE() override {
        // Implement mass erase command handler. See the later section on command handlers
    }

    // Custom commands require overriding an additional function to provide the correct handler function
    CMD_FUNC GetHandler(MEMPROG_CMD Command) override {
        if ((int)Command == 123) {
            return &FLASHAlgorithm::CustomHandler;
        }
        // return nullptr if no handler for this command is available
        return nullptr;
    }

    void CustomHandler() {
        // Implement a handler for a custom command
    }
};
```

### Command Handlers
Command handler functions implement the logic to run a command. See `MEMPROG_STATUS` in `memprog.h` for descriptions
of what each command is expected to do, and the parameters it receives and is expected to return.

Command handlers communicate with the rest of MemProgLib through the `LocalParam` variable:
 - When the command handler is first called, the structure contains a copy of the Parameters that were used to start the command
 - The handler indicates completions by changing `LocalParam.Status` to a value >= `MEMPROG_STATUS_OK`
 - Additionally, `LocalParam.Code` can be set in case of error to help narrow down the error location
 - `LocalParam.P1 - P6 ` shall be set to a value as expected by the command before completion oh the handler

Since MemProgLib typically runs alongside an existing application, command handlers should be designed to return
as quickly as possible to avoid blocking the main application. Typically, this is achieved by implementing a
state machine inside the command handler function. The state machine can be reset based on the value of `LocalParam`
as shown in the following minimal example:
```c++
void FLASHAlgorithm::CMD_MASS_ERASE() {
    static enum ERASE_STATE {
        INIT,
        WAIT,
    } State = INIT;

    // During the first execution, `LocalParam.Status` will equal `_MEMPROG_STATUS_START`
    // This can be used to reset the state machine
    if (LocalParam.Status == _MEMPROG_STATUS_START) {
        State = INIT;
    }

    switch (State) {
        case INIT: {
            // Start erasing all blocks
            FlashStartErase();

            // A naive implementation would busy wait here until the erase is finished.
            // A better, non-blocking approach is to move a different state which will check
            // if the erase is finished
            State = WAIT;

            return;
        }
        case WAIT: {
            // If the erase is still in progress, return
            if (FlashBusy()) {
                return;
            }

            // Set the return code and status
            LocalParam.Code = FlashGetError();

            if (LocalParam.Code) {
                LocalParam.Status = MEMPROG_STATUS_ERR_OTHER;
            } else {
                LocalParam.Status = MEMPROG_STATUS_OK;
            }
            return;
        }
    }
}
```

#### Buffer Usage
Some commands send data from the host to the target via the buffers. Here is an example command handler which uses
buffers in this manner:
```c++
void FLASHAlgorithm::CMD_PROG() {
    static PROG_STATE {
        INIT,
        GET_BUFFER,
        PROGRAM,
    } State = INIT;

    static int CurrentBufferIndex = -1;
    static bool WasLastBuffer = false;

    if (LocalParam.Status == _MEMPROG_STATUS_START) {
        State = INIT;
    }

    switch (State) {
        case INIT: {
            State = GET_BUFFER;
            return;
        }
        case GET_BUFFER: {
            if (WasLastBuffer) {
                // All data has been received. Move to another state, or return for this example
                LocalParam.Status = MEMPROG_STATUS_OK;
                return;
            }

            // Get the next buffer. `WasLastBuffer` will be set to true if this is the last buffer sent
            // by the host
            GetNextFullBuffer(&CurrentBufferIndex, &WasLastBuffer, &CurrentAddress, &CurrentBytesRemaining);
            if (CurrentBufferIndex < 0) {
                // not available yet. try again later
                return;
            }

            State = PROGRAM;
            return;
        }
        case PROGRAM: {
            // Use the buffer data
            uint8_8 * CurrentBuffer = GetBufferAddress(CurrentBufferIndex);
            // ...

            // Release the buffer so it can be refilled by the host
            ReleaseBuffer(CurrentBufferIndex);
            // Get the next buffer
            State = GET_BUFFER;
            return;
        }
    }
}
```

Buffers can also be acquired and filled directly by the command handler, instead of being received from the host.

TODO add an example of this usage

### Integration with Application and Build System
Since MemProgLib is a header only library, the build system only needs to be updated to add an include path for `memprog.hpp`

Integrating MemProgLib to run alongside an existing application is simple:
1. At startup, the application must call `MemProg::StaticInit()` one time
2. Afterwards, `MemProg::StaticRun()` should be called as often as possible to run the MemProgLib logic


# Protocol Description

## Overview
MemProgLib reserves a portion of the target MCU RAM to act as the command interface (known as the parameter structure). The host is able to initiate
commands by modifying this memory, typically via a debug adapter connected with the SWD protocol. The target polls
this memory to receive incoming commands, and writes a return status upon completion of the command.

Another portion of target RAM is reserved for the data transfer buffers. Each buffer has an associated
'buffer descriptor table' (BDT) which indicates the current owner and status of the buffer

## Memory Structure
MEMPROG_PARAM: A single instance of this structure exists in memory

| Field     | Description                                                                              |
|-----------|------------------------------------------------------------------------------------------|
| Token     | Controls which side has write access to this structure                                   |
| Status    | Used to initiate commands and return execution status                                    |
| Interface | Indicates which interface the data pertains to                                           |
| Command   | Indicates which command to run when starting a command                                   |
| Code      | Indicates which error occurred during command execution                                  |
| P1-P6     | General purpose parameters used to pass information when starting or finishing a command |

MEMPROG_BDT: A BDT exists for each data transfer buffer

| Field     | Description                                                  |
|-----------|--------------------------------------------------------------|
| Token     | Controls which side has write access to this structure       |
| Status    | Indicates whether the buffer is empty, pending, or full      |
| Interface | Indicates which interface the buffer belong to               |
| Sequence  | Sequence number used to receive buffers in the correct order |
| Address   | Address where the data should be written to / read from      |
| Length    | Amount of data in the buffer / amount of data to read        |

### Memory Layout
 - The target MCU RAM shall contain one instance of MEMPROG_PARAM, aligned to 4 bytes
 - The target MCU RAM shall contain at least one instance of MEMPROG_BDT, aligned to 4 bytes. Subsequent BDTs
must be placed directly after the previous, with no gaps
 - The target MCU RAM shall contain at least one data buffer, aligned to 4 bytes. Subsequent buffers must be
placed directly after the previous, with no gaps

Example layout:

| Address    | Description      |
|------------|------------------|
| 0x20003760 | MEMPROG_PARAM    |
| 0x20003780 | gap allowed here |
| 0x20003790 | MEMPROG_BDT 1    |
| 0x200037A0 | MEMPROG_BDT 2    |
| 0x200037B0 | gap allowed here |
| 0x20004000 | Buffer 1         |
| 0x20004400 | Buffer 2         |


## Access Control
In order to support concurrent execution, the shared memory needs to support access by any interface on the
host or target at any time.

To synchronize memory access between host and target, a token passing system is used.
The Parameter structure and BDTs contain one field which acts as a token. One value indicates that the host
'owns' the structure, while a different value indicates that the target 'owns' the structure.
Only the token holder is allowed to modify the memory.
Additionally, since the token holder may modify the data, any data read while not holding the token must be assumed to be invalid

In order to decrease latency, both sides shall make an effort to pass the tokens as soon as possible after receiving them.
This is handled internally in the MemProgLib implementation

See the `Data Transfer` section for more detail about BDT access controls

Synchronization between interfaces is implicitly controlled by the single-threaded implementation of MemProgLib on the host and target.
All pending operations pertaining to one interface are completed before moving onto the next interface.

## Command Execution
Command execution takes place in these steps:
1. The host starts a command by filling the `Command`, `Interface`, and `P1-P6` parameters, then setting 
`Status` to `_MEMPROG_STATUS_START`.
Other interfaces are blocked from using the parameters while `Status == _MEMPROG_STATUS_START`
2. The target acknowledges the command by setting `Status` to `_MEMPROG_STATUS_ACK`. 
Other interfaces are blocked from using the parameters while `Status == _MEMPROG_STATUS_ACK`
3. The host receives the acknowledgement and sets `Status` to `_MEMPROG_STATUS_IDLE` so that other interfaces
can use the parameter structure
4. The host and target transfer data to each other via the buffers
5. The target indicates that the command is finished by setting `Status` to a value >= `MEMPROG_STATUS_OK`
6. The host reads out the returned parameters and sets `Status` to `_MEMPROG_STATUS_IDLE` so that other interfaces
can use the parameter structure

## Data Transfer
Either side may acquire and fill a buffer, so long as the BDT `Token` and `Status` are in the correct state:

| BDT Token | BDT Status | BDT Interface | Implied Buffer State                                                  | Permitted Action                                                                                                            |
|-----------|------------|---------------|-----------------------------------------------------------------------|-----------------------------------------------------------------------------------------------------------------------------|
| HOST      | FREE       | ---           | Free for the host to acquire                                          | Any host interface can acquire the buffer by changing `Status` to `PENDING`                                                 |
| HOST      | PENDING    | X             | Host interface X is filling the buffer                                | Host interface X has exclusive access. After filling the buffer, change `Status` to `FULL` and immediately pass the token   |
| HOST      | FULL       | X             | Target interface X has just filled and passed this buffer to the host | Host interface X can read from the buffer, then change `Status` to `FREE`                                                   |
| TARGET    | FREE       | ---           | Free for the target to acquire                                        | Any target interface can acquire the buffer by changing `Status` to `PENDING`                                               |
| TARGET    | PENDING    | X             | Target interface X is filling the buffer                              | Target interface X has exclusive access. After filling the buffer, change `Status` to `FULL` and immediately pass the token |
| TARGET    | FULL       | X             | Host interface X has just filled and passed this buffer to the target | Target interface X can read from the buffer, then change `Status` to `FREE`                                                 |

BDTs contain a `Sequence` field to ensure buffers are received in the correct order in case several are sent from
one side to the other at once. The sequence must be set as follows by the buffer transmitter:
 - The first buffer transmitted after starting a command will have `Sequence = 0`
 - Each subsequent buffer transmitted shall have `Sequence` incremented by one, up to 0x7F. After 0x7F, it shall roll back to 0
 - The last buffer transmitted shall have `Sequence = 0x80`. This lets the other side know when all data has been transferred

It is beneficial to use multiple buffers per interface to avoid unnecessary waiting: one can be written to by the host,
while the other is being read out by the target.

Host implementations should default to a maximum of two buffers per interface, to avoid hogging buffers that could
be used to run commands in parallel on other interfaces


# Target Implementation Notes
TODO implementation notes / details

