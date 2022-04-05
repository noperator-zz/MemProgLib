#pragma once
#include "memprog.h"
#include <cstring>
#include <string>

// Base class which defines the constructor and provides stubs for command methods
class MemProg {
private:
	// NOTE ============= These must be defined in a source file in the MCU firmware
	// Array of base pointers to the MemProg subclass instances
	static MemProg * const Interfaces[];
	// Address of the parameters structure
	static volatile MEMPROG_PARAM * const Param;
	// Address of the buffer descriptor tables structure
	static volatile MEMPROG_BDT * const BufferDescriptors;
	// Address of the buffers
	static volatile uint8_t * const Buffer;
	// Size of each buffer chunk
	static const uint32_t BufferSize;
	// Number of chunks to split the buffer into
	// With 2 or more chunks, memprog will run command handlers at the same time as receiving data over SWD
	static const uint32_t NumBuffers;

	// Optional Debug putc function. Set as nullptr to disable debugging
	static void (* const putc)(uint8_t);
	// Optional timekeeping function for debugging. Return millseconds. Set as nullptr if not used
	static uint32_t (* const volatile time_ms)();
	// NOTE ========================================================================

	// Maximum duration `StaticRun` should run for
	static constexpr uint32_t HANDLER_TIMEOUT_MS = 30;
	// Maximum duration to wait for the token before printing an error message. Will continue to wait after this timeout
	static constexpr uint32_t TOKEN_TIMEOUT_MS = 10;
	static constexpr uint8_t hex[] = {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'};

protected:
	static void lputc(uint8_t c) {
		if (putc) {
			putc(c);
		}
	}
	static void lputs(const char* s) {
		if (putc) {
			while(*s) {
				putc(*s);
				s++;
			}
		}
	}
	static void lend() {
		lputc('\n');
	}
	static void lputh1(uint8_t v, bool space=true) {
		lputc(hex[v >> 4]);
		lputc(hex[v & 0x0F]);
		if (space) lputc(' ');
	}
	static void lputh4(uint32_t v, bool space=true) {
		lputh1((uint8_t) (v >> 24), false);
		lputh1((uint8_t) (v >> 16), false);
		lputh1((uint8_t) (v >> 8), false);
		lputh1((uint8_t) v, space);
	}
	void log(const char *s) const {
		lputh1(Interface);
		lputs(s);
	}

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
			(*ptr)->Interface = (ptr - Interfaces);
			(*ptr)->Init();
		}

		Param->Status = _MEMPROG_STATUS_IDLE;
		ReleaseToken();
	}

	static void StaticRun() {
		// TODO select which instance to run based on:
		//  - whether it's active (has a command running)
		//  - whether it's waiting for a free buffer (must be active for this to be true)
		//  - which instance ran last time (don't run the same one twice if another command is active/waiting)

		MemProg * inst;

		// if we have the token
		if (TryAcquireToken()) {
			bool released = false;

			// Check if host wants to start a command
			if (Param->Status == _MEMPROG_STATUS_START) {
				inst = Interfaces[Param->Interface];
				// Copy the volatile params to LocalParams
				inst->Active = true;
				memcpy(&inst->LocalParam, (const void *)Param, sizeof(LocalParam));

				inst->log("start "); lputh1(inst->LocalParam.Command); lend();

				// Acknowledge the command by changing status to IDLE and passing token back after copying Params
				Param->Status = _MEMPROG_STATUS_ACK;
				ReleaseToken();
				released = true;

				// Check if a handler for this command exists
				if (!(inst->CurrentHandler = inst->BaseGetHandler(Param->Command))) {
					inst->LocalParam.Status = MEMPROG_STATUS_ERR_IMPLEMENTATION;
				}
			} else if (Param->Status == _MEMPROG_STATUS_IDLE) {
				// If not, check if anything needs to be returned
				uint8_t i = 0;
				for (; (inst = Interfaces[i]); i++) {
					// if status has been set, the command has finished; notify the host by modifying Param.Status
					if (inst->Active && inst->LocalParam.Status >= MEMPROG_STATUS_OK) {
						inst->log("return "); lputh1(inst->LocalParam.Command); lend();

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
				lputs("BAD STATUS "); lputh1(Param->Status); lend();
			}

			if (!released) {
				ReleaseToken();
			}
		}

		// then run any active handlers
		uint32_t start_time = time_ms ? time_ms() : 0;
		uint8_t i = 0;
		for (; (inst = Interfaces[i]); i++) {
			if ( ! (inst->Active && inst->LocalParam.Status < MEMPROG_STATUS_OK)) {
				continue;
			}

			// if status hasn't been set yet, keep running the command
			inst->log("run "); lputh1(inst->LocalParam.Command); lend();
			(inst->*inst->CurrentHandler)();

			// Status will be START the first time this is called. Handlers can use this fact to reset their state
			// After the first run, change Status to something else, unless it has already changed to a valid return value
			if (inst->LocalParam.Status < MEMPROG_STATUS_OK) {
				inst->LocalParam.Status = _MEMPROG_STATUS_IDLE;
			} else {
				inst->log("finish "); lputh1(inst->LocalParam.Command); lend();

				uint8_t NumBroken;
				inst->ForceReleaseBuffers(&NumBroken);
				if (NumBroken) {
					// Tack original status onto Code
					inst->LocalParam.Code = (inst->LocalParam.Code << 8) | ((uint8_t)inst->LocalParam.Status);
					inst->LocalParam.Status = _MEMPROG_STATUS_BUFFER;
					inst->log("BROKEN "); lputh1(NumBroken); lend();
				}
			}

			if (time_ms) {
				uint32_t elapsed = time_ms() - start_time;

				if (elapsed > HANDLER_TIMEOUT_MS) {
					if (elapsed > (HANDLER_TIMEOUT_MS * 2)) {
						// only print error message if significantly overran (double)
						lputs("LOOP OVERRUN "); lputh4(elapsed); lend();
					}
					break;
				}
			}
		}
	}

private:
	inline void DEFAULT_HANDLER() { LocalParam.Status = MEMPROG_STATUS_ERR_IMPLEMENTATION; }

protected:
	MEMPROG_PARAM LocalParam;

	using CMD_FUNC = void (MemProg::*)();

	virtual void Init() {}

	virtual CMD_FUNC GetHandler(MEMPROG_CMD Command) {
		return nullptr;
	}

	// stubs for global commands
	virtual inline void CMD_MASS_ERASE() { DEFAULT_HANDLER(); }
	virtual inline void CMD_ERASE_RANGE() { DEFAULT_HANDLER(); }
	virtual inline void CMD_PROG() { DEFAULT_HANDLER(); }
	virtual inline void CMD_PROG_VERIFY() { DEFAULT_HANDLER(); }

	static uint8_t * GetBufferAddress(uint8_t BufferIndex) {
		return const_cast<uint8_t *>(Buffer + BufferIndex * BufferSize);
	}


	// NOTE these four BDT functions may be called while the token is not held
	//  Should be OK because the host and target access to BDTs will not overlap in a destructive manner
	//  During a host write command:
	//   - host will only write to BDT once Status == FREE
	//   - target will only read from BDT once Status == FULL
	//   - target will only write to BDT to change Status back to FREE or PENDING
	//  During a host read command:
	//   - target will only write from BDT once Status == FREE
	//   - host will only read from BDT once Status == FULL
	//   - host will only write to BDT to change Status back to FREE or PENDING

	void AcquireBuffer(int *BufferIndex) const {
		// Loop through BDTs until a free one is found
		uint8_t i;

		*BufferIndex = -1;
		AcquireToken();
		for (i = 0; i < NumBuffers; i++) {
			volatile MEMPROG_BDT &bdt = BufferDescriptors[i];
			if (bdt.Status == MEMPROG_BUFFER_STATUS_FREE) {
				bdt.Status = MEMPROG_BUFFER_STATUS_PENDING;
				bdt.Interface = Interface;
				log("acquire "); lputh1(i); lend();
				*BufferIndex = i;
			}
		}
		ReleaseToken();
		log("acquire -\n");
	}

	void GetNextFullBuffer(int * BufferIndex, uint32_t *Address, uint32_t *Length) const {
		// Find a full buffer assigned to this interface. If there are multiple, return the one with the lowest address
		*Address = 0xFFFFFFFF;
		*BufferIndex = -1;

		uint8_t i;
		AcquireToken();
		for (i = 0; i < NumBuffers; i++) {
			volatile MEMPROG_BDT &bdt = BufferDescriptors[i];
			if (bdt.Status == MEMPROG_BUFFER_STATUS_FULL && bdt.Interface == Interface && bdt.Address < *Address) {
				*Address = bdt.Address;
				*Length = bdt.Length;
				*BufferIndex = i;
			}
		}
		ReleaseToken();
		if (*BufferIndex >= 0) {
			log("get "); lputh1(*BufferIndex); lputh4(*Address); lputh4(*Length); lend();
		} else {
			log("get -\n");
		}
	}

	void FillBuffer(uint8_t BufferIndex, uint32_t Address, uint32_t Length) const {
		log("fill "); lputh1(BufferIndex); lend();
		AcquireToken();
		BufferDescriptors[BufferIndex].Interface = Interface;
		BufferDescriptors[BufferIndex].Address = Address;
		BufferDescriptors[BufferIndex].Length = Length;
		BufferDescriptors[BufferIndex].Status = MEMPROG_BUFFER_STATUS_FULL;
		ReleaseToken();
	}

	void ReleaseBuffer(uint8_t BufferIndex) const {
		if (BufferIndex < 0) {
			log("BAD RELEASE");
		}
		log("release "); lputh1(BufferIndex); lend();
		AcquireToken();
		BufferDescriptors[BufferIndex].Status = MEMPROG_BUFFER_STATUS_FREE;
		ReleaseToken();
	}

private:
	CMD_FUNC CurrentHandler;
	bool Active;
	uint8_t Interface;

	static inline MemProg * CurrentInterface = nullptr;

	void CMD_QUERY_CAP() {
		LocalParam.Code = MEMPROG_VERSION;
		LocalParam.P1 = (uint32_t)BufferDescriptors;
		LocalParam.P2 = (uint32_t)Buffer;
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
			case MEMPROG_CMD_PROG:
				return &MemProg::CMD_PROG;
			case MEMPROG_CMD_PROG_VERIFY:
				return &MemProg::CMD_PROG_VERIFY;

			// Interface-specific commands
			default:
				return GetHandler(Command);
		}
	}

	static bool TryAcquireToken() {
		if (Param->Token == MEMPROG_TOKEN_TARGET) {
			AcquireToken();
			return true;
		}
		return false;
	}

	static void AcquireToken() {
		// NOTE should probably timeout, but if we can't get the token for some reason, the host is probably dead
		//  anyway
		uint32_t t = time_ms ? time_ms() : 0;

		lputs("wt\n");
		while (Param->Token != MEMPROG_TOKEN_TARGET) {
		}

		if (time_ms) {
			t = time_ms() - t;
			if (t > TOKEN_TIMEOUT_MS) {
				lputs("TOKEN TIMEOUT "); lputh4(t); lend();
			}
		}
		lputs("at\n");
	}

	static void ReleaseToken() {
		lputs("rt\n");
		Param->Token = MEMPROG_TOKEN_HOST;
	}

	void ForceReleaseBuffers(uint8_t *NumBroken) const {
		// NOTE only call this when token is not taken
		uint8_t i;
		*NumBroken = 0;

		AcquireToken();
		for (i = 0; i < NumBuffers; i++) {
			volatile MEMPROG_BDT &bdt = BufferDescriptors[i];
			if (bdt.Status == MEMPROG_BUFFER_STATUS_FREE || bdt.Interface != Interface) {
				continue;
			}

			(*NumBroken)++;
			bdt.Status = MEMPROG_BUFFER_STATUS_FREE;
		}
		ReleaseToken();
	}
};
