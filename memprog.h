#pragma once

#include <stdint.h>

typedef enum {
	MEMPROG_STATUS_OK                   = 0x00,
	MEMPROG_STATUS_BUSY                 = 0x01,

	MEMPROG_STATUS_ERR_PARAM            = 0x80,
	MEMPROG_STATUS_ERR_EXECUTION        = 0x81,
	MEMPROG_STATUS_ERR_TIMEOUT          = 0x82,

	MEMPROG_STATUS_ERR_OTHER            = 0xFF,
} MEMPROG_STATUS;

// NOTE: Make sure to update MemProg::COMMAND_KEY_MAP and MemProg::COMMAND_MAP
//  when changing the MEMPROG_CMD enum
typedef enum {
	MEMPROG_CMD_NONE                    = 0x00000000,
	MEMPROG_CMD_QUERY_CAP               = 0x00000001,

	MEMPROG_CMD_MASS_ERASE              = 0x00000010,

	MEMPROG_CMD_PROG_INIT               = 0x00000020,
	MEMPROG_CMD_PROG                    = 0x00000021,
	MEMPROG_CMD_PROG_FINI               = 0x00000022,
} MEMPROG_CMD;

// NOTE: Make sure to update memprog.py if these flags change
typedef enum {
	// Execute mass erase every time `program_via_algorithm` is called
	MEMPROG_OOCD_FLAG_ERASE             = 0x00000001,
	// Execute default verification algorithm at the end of `program_via_algorithm` (read back memory over SWD)
	//  This obviously won't work for memory that is not directly accessible over SWD
	MEMPROG_OOCD_FLAG_VERIFY            = 0x00000002,
} MEMPROG_OOCD_FLAG;

typedef struct {
	union {
		uint32_t StatusCodeReg;
		struct {
			MEMPROG_STATUS Status: 8;
			uint32_t Code: 24;
		};
	};
	union {
		uint32_t CommandReg;
		MEMPROG_CMD Command;
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
	uint32_t Interface;
} MEMPROG_PARAM;
