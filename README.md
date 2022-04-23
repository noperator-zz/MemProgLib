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


# Usage Guide
TODO
Host side...
Target side...
## Command Handlers
TODO

# Protocol Description
## Terminology
 - Interface: A logical instance which can run commands concurrently with other interfaces. All interfaces
use the same parameter structure and buffers
 - Parameters: The command parameter structure, MEMPROG_PARAM. Alternatively, the fields with this structure
may be referred to as parameters
 - BDT: Buffer Descriptor Table, MEMPROG_BDT. Holds information about each data transfer buffer

## Overview
MemProgLib reserves a portion of the target MCU RAM to act as the command interface (known as the **param**eter structure). The host is able to initiate
commands by modifying this memory, typically via a debug adapter connected with the SWD protocol. The target polls
this memory to receive incoming commands, and writes a return status upon completion of the command.

Another portion of target RAM is reserved for the data transfer buffers. Each buffer has an associated
'buffer descriptor table' (**BDT**) which indicates the current owner and status of the buffer

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

It is beneficial to use multiple buffers per interface to avoid unnecessary waiting: one can be written to by the host,
while the other is being read out by the target.

Host implementations should default to a maximum of two buffers per interface, to avoid hogging bandwidth that could
be used to run commands in parallel on other interfaces


# Host Implementation
The host interface is integrated into OpenOCD. For details, see https://pd-bitbucket.deltacontrols.com/projects/QAEP/repos/openocd/browse/README_MEMPROG.md

# Target Implementation
This repository contains the target implementation of MemProgLib
TODO implementation notes / details

