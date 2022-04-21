#pragma once
#include "memprog.h"
#include <cstring>
#include <string>

class MemProgDebugMixin {
private:
	// NOTE ============= Configuration options
//#define MEMPROG_LOGGING
//#define MEMPROG_DEBUGGING

	// Optional debug putc function.
	static void (* const dputc)(uint8_t c);
	// Optional debug pin write function.
	static void (* const dset)(uint8_t pin, bool state);
	// NOTE ===================================

	static constexpr uint8_t hex[] = "0123456789ABCDEF";

protected:
//	static constexpr uint8_t PIN_BUFFER1 = 0;
//	static constexpr uint8_t PIN_BUFFER0 = 1;
//	static constexpr uint8_t PIN_HANDLER1 = 2;
//	static constexpr uint8_t PIN_HANDLER0 = 3;

	static void DBGSET(uint8_t pin, bool state) {
#ifdef MEMPROG_DEBUGGING
		if (dset) {
			dset(pin, state);
		}
#endif
	}

	static void DBGC(uint8_t c) {
#ifdef MEMPROG_LOGGING
		if (dputc) {
			dputc(c);
		}
#endif
	}
	static void DBGS(const char* s) {
#ifdef MEMPROG_LOGGING
		if (dputc) {
			while (*s) {
				dputc(*s);
				s++;
			}
		}
#endif
	}
	static void DBGEND() {
#ifdef MEMPROG_LOGGING
		DBGC('\n');
#endif
	}
	static void DBGH1(uint8_t v, bool space= true) {
#ifdef MEMPROG_LOGGING
		DBGC(hex[v >> 4]);
		DBGC(hex[v & 0x0F]);
		if (space) DBGC(' ');
#endif
	}
	static void DBGH4(uint32_t v, bool space= true) {
#ifdef MEMPROG_LOGGING
		DBGH1(v >> 24, false);
		DBGH1(v >> 16, false);
		DBGH1(v >> 8, false);
		DBGH1(v, space);
#endif
	}
};

// Base class which defines the constructor and provides stubs for command methods
class MemProg : public MemProgDebugMixin {
private:
	// NOTE ============= These must be defined in a source file in the MCU firmware
	// Array of base pointers to the MemProg subclass instances
	static MemProg * const Interfaces[];
	// Address of the parameters structure
	static volatile MEMPROG_PARAM * const Param;
	// Address of the buffer descriptor tables structure
	static volatile MEMPROG_BDT * const BufferDescriptors;
	// Address of the buffers
	static volatile uint8_t * const Buffers;
	// Size of each buffer chunk
	static const uint32_t BufferSize;
	// Number of chunks to split the buffer into
	// With 2 or more chunks, memprog will run command handlers at the same time as receiving data over SWD
	static const uint32_t NumBuffers;
	// Timekeeping function. Return millseconds.
	static uint32_t (* const volatile time_ms)();
	// NOTE ========================================================================

	static inline uint8_t NumInterfaces = 0;
	// Maximum duration `StaticRun` should run for
	static constexpr uint32_t HANDLER_TIMEOUT_MS           = 30;
	// Maximum duration to wait for the token before giving up and returning to test shell
	static constexpr uint32_t TOKEN_TIMEOUT_MS             = 10;

public:
	virtual ~MemProg() = default;

	static void StaticInit() {
		MemProg * const * ptr = Interfaces;

		uint32_t i;
		// Clear buffer descriptors
		for (i = 0; i < NumBuffers; i++) {
			memset((void*)(BufferDescriptors + i), 0, sizeof(MEMPROG_BDT));
		}

		// Initialize each interface
		for (; *ptr; ptr++) {
			(*ptr)->Interface = NumInterfaces++;
			(*ptr)->Init();
		}

		Param->Status = _MEMPROG_STATUS_IDLE;
		ReleaseToken();
	}

	static void StaticRun() {
		MemProg * inst;

		// if we have the token
		if (TryAcquireToken()) {
			bool released = false;

			DBGS("sr ");
			DBGH1(Param->Status);
			DBGH1(Param->Interface);
			DBGEND();

			// Check if host wants to start a command
			if (Param->Status == _MEMPROG_STATUS_START) {
				inst = Interfaces[Param->Interface];
				// Copy the volatile params to LocalParams
				inst->Active = true;
				memcpy(&inst->LocalParam, (const void *)Param, sizeof(LocalParam));

				DBGH1(inst->Interface);
				DBGS("start ");
				DBGH1(inst->LocalParam.Command);
				DBGEND();

				// Acknowledge the command by changing status to IDLE and passing token back after copying Params
				Param->Status = _MEMPROG_STATUS_ACK;
				ReleaseToken();
				released = true;

				// Check if a handler for this command exists
				if (!(inst->CurrentHandler = inst->BaseGetHandler(Param->Command))) {
					inst->LocalParam.Status = MEMPROG_STATUS_ERR_IMPLEMENTATION;
				} else {
					inst->TXSequence = 0;
					inst->RXSequence = 0;
				}
			} else if (Param->Status == _MEMPROG_STATUS_IDLE) {
				// If not, check if anything needs to be returned
				uint8_t i = 0;
				for (; (inst = Interfaces[i]); i++) {
					// if status has been set, the command has finished; notify the host by modifying Param.Status
					if (inst->Active && inst->LocalParam.Status >= MEMPROG_STATUS_OK) {
						DBGH1(inst->Interface);
						DBGS("return ");
						DBGH1(inst->LocalParam.Command);
						DBGEND();

						inst->Active = false;
						inst->CurrentHandler = nullptr;

//						LocalParam.Token = MEMPROG_TOKEN_TARGET; This is necessarily already TOKEN_TARGET based on the first if statement in this function

						memcpy((void *)Param, &inst->LocalParam, sizeof(MEMPROG_PARAM));
						// Params.Token must be changed after all the other params. It indicates to the host
						// that all other params are valid to read
						ReleaseToken();
						released = true;
						break;
					}
				}
			} else {
				// host may accidentally pass us the token before reading out return data. Do nothing in this case
				DBGS("BAD STATUS ");
				DBGH1(Param->Status);
				DBGEND();
			}

			if (!released) {
				ReleaseToken();
			}
		}

		// then run any active handlers
		uint32_t start_time = time_ms();
		uint8_t i = 0;
		for (; (inst = Interfaces[i]); i++) {
			if (!inst->Active || inst->LocalParam.Status >= MEMPROG_STATUS_OK) {
				continue;
			}

			// if status hasn't been set yet, keep running the command
//			DBGSET(PIN_HANDLER0 - i, true);
			(inst->*inst->CurrentHandler)();
//			DBGSET(PIN_HANDLER0 - i, false);

			// Status will be START the first time this is called. Handlers can use this fact to reset their state
			// After the first run, change Status to something else, unless it has already changed to a valid return value
			if (inst->LocalParam.Status < MEMPROG_STATUS_OK) {
				inst->LocalParam.Status = _MEMPROG_STATUS_IDLE;
			} else {
				DBGH1(inst->Interface);
				DBGS("finish ");
				DBGH1(inst->LocalParam.Command);
				DBGEND();
			}

			uint32_t elapsed = time_ms() - start_time;

			if (elapsed > HANDLER_TIMEOUT_MS) {
				if (elapsed > (HANDLER_TIMEOUT_MS * 2)) {
					// only print error message if significantly overran (double)
					DBGS("LOOP OVERRUN ");
					DBGH4(elapsed);
					DBGEND();
				}
				break;
			}
		}

		// release any unused buffers
		PassBuffers();
	}

private:
	inline void DEFAULT_HANDLER() { LocalParam.Status = MEMPROG_STATUS_ERR_IMPLEMENTATION; }

protected:
	MEMPROG_PARAM LocalParam {};

	using CMD_FUNC = void (MemProg::*)();

	virtual void Init() {}

	virtual CMD_FUNC GetHandler(MEMPROG_CMD Command) {
		return nullptr;
	}

	// stubs for global commands
	virtual inline void CMD_MASS_ERASE() { DEFAULT_HANDLER(); }
	virtual inline void CMD_ERASE_RANGE() { DEFAULT_HANDLER(); }
	virtual inline void CMD_PROG_VERIFY() { DEFAULT_HANDLER(); }
	virtual inline void CMD_CRC() { DEFAULT_HANDLER(); }
	virtual inline void CMD_READ() { DEFAULT_HANDLER(); }

	static uint8_t * GetBufferAddress(uint8_t BufferIndex) {
		return const_cast<uint8_t *>(Buffers + BufferIndex * BufferSize);
	}

	void AcquireBuffer(int *BufferIndex, const uint8_t ** Buffer, uint32_t *Size) const {
		// Loop through BDTs until a free one is found
		uint8_t i;

		*BufferIndex = -1;
		for (i = 0; i < NumBuffers; i++) {
			volatile MEMPROG_BDT &bdt = BufferDescriptors[i];
			if (bdt.Token != MEMPROG_TOKEN_TARGET) {
				continue;
			}
			memory_sync();
			if (bdt.Status == MEMPROG_BUFFER_STATUS_FREE) {
				bdt.Status = MEMPROG_BUFFER_STATUS_PENDING;
				bdt.Interface = Interface;

				*Buffer = const_cast<uint8_t *>(Buffers + i * BufferSize);
				*Size = BufferSize;

				DBGH1(Interface);
				DBGS("acquire ");
				DBGH1(i);
				DBGEND();
				*BufferIndex = i;
			}
		}
//		if (*BufferIndex >= 0) {
//			DBGSET(PIN_BUFFER0 - Interface, false);
//		} else {
//			DBGSET(PIN_BUFFER0 - Interface, true);
//		}
	}

	void GetNextFullBuffer(int * BufferIndex, bool *Last, uint32_t *Address, uint32_t *Length) {
		// Find a full buffer with the matching sequence number assigned to this interface
		*BufferIndex = -1;
		*Last = false;

		uint8_t i;
		for (i = 0; i < NumBuffers; i++) {
			volatile MEMPROG_BDT &bdt = BufferDescriptors[i];
			if (bdt.Token != MEMPROG_TOKEN_TARGET) {
				continue;
			}
			memory_sync();
			if (bdt.Status == MEMPROG_BUFFER_STATUS_FULL && bdt.Interface == Interface && (bdt.Sequence == RXSequence || bdt.Sequence & 0x80)) {
				*Address = bdt.Address;
				*Length = bdt.Length;
				*BufferIndex = i;

				if (bdt.Sequence & 0x80) {
					RXSequence = 0x80;
					*Last = true;
				} else {
					RXSequence = (RXSequence + 1) % 0x80;
				}
				break;
			}
		}
		if (*BufferIndex >= 0) {
//			DBGSET(PIN_BUFFER0 - Interface, false);
			DBGH1(Interface);
			DBGS("get ");
			DBGH1(*BufferIndex);
			DBGH4(*Address);
			DBGH4(*Length);
			DBGEND();
		} else {
//			DBGSET(PIN_BUFFER0 - Interface, true);
		}
	}

	void FillBuffer(uint8_t BufferIndex, bool Last, uint32_t Address, uint32_t Length) {
		if (Last) {
			TXSequence = 0x80;
		}
		DBGH1(Interface);
		DBGS("fill ");
		DBGH1(BufferIndex);
		DBGH1(TXSequence);
		DBGEND();

		BufferDescriptors[BufferIndex].Status = MEMPROG_BUFFER_STATUS_FULL;
		BufferDescriptors[BufferIndex].Interface = Interface;
		BufferDescriptors[BufferIndex].Sequence = TXSequence;
		BufferDescriptors[BufferIndex].Address = Address;
		BufferDescriptors[BufferIndex].Length = Length;
		memory_sync();
		BufferDescriptors[BufferIndex].Token = MEMPROG_TOKEN_HOST;
		if (!(TXSequence & 0x80)) {
			TXSequence = (TXSequence + 1) % 0x80;
		}
	}

	void ReleaseBuffer(uint8_t BufferIndex) const {
		DBGH1(Interface);
		if (BufferIndex < 0) {
			DBGS("BAD RELEASE");
			return;
		}
		DBGS("release ");
		DBGH1(BufferIndex);
		DBGEND();

		BufferDescriptors[BufferIndex].Status = MEMPROG_BUFFER_STATUS_FREE;
		memory_sync();
		BufferDescriptors[BufferIndex].Token = MEMPROG_TOKEN_HOST;
	}

	static void PassBuffers() {
		// Pass the token of any unused buffers
		uint8_t i;
		for (i = 0; i < NumBuffers; i++) {
			volatile MEMPROG_BDT &bdt = BufferDescriptors[i];
			if (bdt.Token != MEMPROG_TOKEN_TARGET) {
				continue;
			}
			memory_sync();
			if (bdt.Status == MEMPROG_BUFFER_STATUS_FREE) {
				bdt.Token = MEMPROG_TOKEN_HOST;
			} else if ((bdt.Interface < NumInterfaces) && !Interfaces[bdt.Interface]->Active) {
				// NOTE: this is an orphaned buffer
				DBGS("Orphan buffer detected:");
				DBGH1(i);
				DBGH1(bdt.Interface);
				DBGH1(bdt.Status);
				DBGEND();
				bdt.Token = MEMPROG_TOKEN_HOST;
			}
		}
	}

	static uint32_t CRC32(uint8_t *Data, uint32_t Length, uint32_t LastCRC= 0) {
		int8_t i;
		uint32_t Mask;

		uint32_t CRC = ~LastCRC;
		while (Length--) {
			CRC = CRC ^ *Data;
			Data++;
			for (i = 7; i >= 0; i--) {
				Mask = -(CRC & 1);
				CRC = (CRC >> 1) ^ (0xEDB88320 & Mask);
			}
		}
		return ~CRC;
	}

private:
	CMD_FUNC CurrentHandler = nullptr;
	bool Active = false;
	uint8_t Interface = 0;

	uint8_t TXSequence = 0;
	uint8_t RXSequence = 0;

	void CMD_QUERY_CAP() {
		LocalParam.Code = MEMPROG_VERSION;
		LocalParam.P1 = (uint32_t)BufferDescriptors;
		LocalParam.P2 = (uint32_t)Buffers;
		LocalParam.P3 = (NumBuffers << 24) | BufferSize;
		LocalParam.Status = MEMPROG_STATUS_OK;
	}

	CMD_FUNC BaseGetHandler(MEMPROG_CMD Command) {
		switch (Command) {
			// Static commands
			case MEMPROG_CMD_QUERY_CAP:
				return &MemProg::CMD_QUERY_CAP;

			// Global commands
			case MEMPROG_CMD_MASS_ERASE:
				return &MemProg::CMD_MASS_ERASE;
			case MEMPROG_CMD_ERASE_RANGE:
				return &MemProg::CMD_ERASE_RANGE;
			case MEMPROG_CMD_PROG_VERIFY:
				return &MemProg::CMD_PROG_VERIFY;
			case MEMPROG_CMD_CRC:
				return &MemProg::CMD_CRC;
			case MEMPROG_CMD_READ:
				return &MemProg::CMD_READ;

			// Interface-specific commands
			default:
				return GetHandler(Command);
		}
	}

	static inline void memory_sync() {
		__asm volatile("dmb");
		__asm volatile("dsb");
		__asm volatile("isb");
	}

	static bool TryAcquireToken() {
//		DBGSET(PIN_ACQUIRE, true);

		if (Param->Token == MEMPROG_TOKEN_TARGET) {
//			DBGSET(PIN_ACQUIRE, false);
			DBGS("at\n");
//			DBGSET(PIN_TOKEN, true);
			return true;
		}
//		DBGSET(PIN_ACQUIRE, false);
		return false;
	}

	static void ReleaseToken() {
		DBGS("rt\n");
		memory_sync();
		Param->Token = MEMPROG_TOKEN_HOST;
//		DBGSET(PIN_TOKEN, false);
	}
};
