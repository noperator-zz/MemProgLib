#pragma once

#include <stdint.h>

//inline void MEMPROG_SET8(uint8_t * const Buffer, uint8_t Data) {
//	*Buffer = Data;
//}
//inline uint8_t MEMPROG_GET8(const uint8_t * const Buffer) {
//	return *Buffer;
//}
//inline void MEMPROG_SET32(uint8_t * const Buffer, uint8_t Data) {
//	Buffer[0] = (Data >> 0) & 0xFF;
//	Buffer[1] = (Data >> 8) & 0xFF;
//	Buffer[2] = (Data >> 16) & 0xFF;
//	Buffer[3] = (Data >> 24) & 0xFF;
//}
//inline uint32_t MEMPROG_GET32(const uint8_t * const Buffer) {
//	return
//			(Buffer[3] << 24) |
//			(Buffer[2] << 16) |
//			(Buffer[1] << 8) |
//			(Buffer[0] << 0);
//}
//
//// Only use these on little-endian targets. Ok to use on the MCU side to save space/time compared to the generic version
//inline void MEMPROG_SET32_LE(uint8_t * const Buffer, uint8_t Data) {
//	*((uint32_t*)Buffer) = Data;
//}
//inline uint32_t MEMPROG_GET32_LE(const uint8_t * const Buffer) {
//	return *((uint32_t*)Buffer);
//}

#define MEMPROG_MAJOR_VERSION  ((uint32_t)2)
#define MEMPROG_MINOR_VERSION  ((uint32_t)0)
#define MEMPROG_PATCH_VERSION  ((uint32_t)0)
#define MEMPROG_VERSION        ((MEMPROG_MAJOR_VERSION << 24) | ((MEMPROG_MINOR_VERSION) << 16) | ((MEMPROG_PATCH_VERSION << 8)))


// NOTE: Make sure to update memprog.py if these flags change
// TODO move this to another file. Host specific stuff should not be here
typedef enum {
	// Execute mass erase every time `program_via_algorithm` is called
	MEMPROG_OOCD_FLAG_ERASE             = 0x00000001,
	// Execute default verification algorithm at the end of `program_via_algorithm` (read back memory over SWD)
	//  This obviously won't work for memory that is not directly accessible over SWD
	MEMPROG_OOCD_FLAG_VERIFY            = 0x00000002,
} MEMPROG_OOCD_FLAG;


/// Command execution status. Host will set this to BUSY when it wants to execute a new command.
/// Target will change it to another value when finished the command.
typedef enum __attribute__((__packed__)) {
	MEMPROG_STATUS_OK                   = 0x00,
	MEMPROG_STATUS_BUSY                 = 0x01,

	MEMPROG_STATUS_ERR_PARAM            = 0x80,
	MEMPROG_STATUS_ERR_EXECUTION        = 0x81,
	MEMPROG_STATUS_ERR_TIMEOUT          = 0x82,

	MEMPROG_STATUS_ERR_OTHER            = 0xFF,
} MEMPROG_STATUS;

// NOTE: Make sure to update MemProg::COMMAND_MAP when changing the MEMPROG_CMD enum
typedef enum __attribute__((__packed__)) {
//	MEMPROG_CMD_NONE                    = 0x00,

//	MEMPROG_CMD_MASS_ERASE              = 0x10,
//
//	MEMPROG_CMD_PROG_INIT               = 0x20,
//	MEMPROG_CMD_PROG                    = 0x21,
//	MEMPROG_CMD_PROG_FINI               = 0x22,

	// Bit 7 indicates that the host should read from buffers
	// In general, 'read' type commands should set this bit, even if they don't use buffers
	MEMPROG_CMD_QUERY_CAP               = 0x80,
} MEMPROG_CMD;

typedef enum __attribute__((__packed__)) {
	MEMPROG_TOKEN_HOST                  = 0x00,
	MEMPROG_TOKEN_TARGET                = 0x01,
} MEMPROG_TOKEN;

//typedef struct {
//	uint8_t Raw[
//		1 + // Status
//		1 + // Token
//		1 + // Interface
//		1 + // Command
//		4 + // Code / Sequence
//		4 + // Address / P1
//		4 + // Length / P2
//		4   // BufferAddress / P3
//	];
//} MEMPROG_PARAM;

// FIXME struct packing is compiler defined. This is almost guaranteed to not work correctly on some combination
//  of target / compiler. Should instead use `uint8_t Params[]` and have e.g. `void SetAddress(uint32_t Address) { Params[4] = Address & 0xFF, ...}`
typedef struct __attribute__((__packed__)) {
	MEMPROG_TOKEN Token: 8;
	MEMPROG_STATUS Status: 8;
	uint8_t Interface: 8;
	MEMPROG_CMD Command: 8;

	union {
		uint32_t Code;
		uint32_t Sequence;
	};
	union {
		uint32_t Address;
		uint32_t P1;
	};
	union {
		uint32_t Length;
		uint32_t P2;
	};
	union {
		uint32_t BufferAddress;
		uint32_t P3;
	};
} MEMPROG_PARAM;

typedef enum __attribute__((__packed__)) {
	MEMPROG_BUFFER_STATUS_FREE          = 0x00,
	MEMPROG_BUFFER_STATUS_PENDING       = 0x01,
	MEMPROG_BUFFER_STATUS_FULL          = 0x02,
} MEMPROG_BUFFER_STATUS;

typedef struct __attribute__((__packed__)) {
	// Whether the buffer is currently free, being filled, or full
	MEMPROG_BUFFER_STATUS Status: 8;
	// Which interface the buffer is currently used by (valid if status != free)
	uint8_t Interface: 8;

	uint16_t _RESERVERD1: 16;
	// Where the data was read from / where it should be written to
	uint32_t Address;
	// Amount of data in the buffer
	uint32_t Length;
} MEMPROG_BDT;
