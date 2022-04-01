#pragma once
#include "memprog.h"
#include <cstring>
#include <string>

// Base class which defines the constructor and provides stubs for command methods
class MemProg {
private:
	// These must be defined in a source file in the MCU firmware

	// Array of base pointers to the derived MemProg instances
	static MemProg * const Interfaces[];
	// Address of the parameters structure
	static volatile MEMPROG_PARAM * const Param;
	// Address of the buffer descriptor tables structure
	static volatile MEMPROG_BDT * const BufferDescriptors;
	// Address of the buffers
	static volatile uint8_t * const Buffer;
	// Size of each buffer
	static const uint32_t BufferSize;
	// Number of buffers
	static const uint32_t NumBuffers;

	// Debug putc function. Set as nullptr to disable debugging
	static void (* const putc)(uint8_t);
	// Debug logging verbosity. 0 = highest verbosity
	static const uint8_t LogLevel;

protected:
//	static void LogPrints(std::string_view Message, uint8_t Level) {
//		if (dputc && LogLevel <= Level) {
//			for (auto c: Message) {
//				dputc(c);
//			}
//		}
//	}
	static void lputc(uint8_t c, uint8_t Level=LOG_DEBUG) {
		if (putc && LogLevel <= Level) {
			putc(c);
		}
	}
//	static void LogDebug(std::string_view Message) { LogPrints(Message, 0); }
//	static void LogInfo(std::string_view Message) { LogPrints(Message, 10); }
//	static void LogWarn(std::string_view Message) { LogPrints(Message, 20); }
//	static void LogError(std::string_view Message) { LogPrints(Message, 30); }
//	static void LogDebug(uint8_t c) { LogPrintc(c, 0); }
//	static void LogInfo(uint8_t c) { LogPrintc(c, 10); }
//	static void LogWarn(uint8_t c) { LogPrintc(c, 20); }
//	static void LogError(uint8_t c) { LogPrintc(c, 30); }

public:
	static constexpr uint8_t LOG_DEBUG = 0;
	static constexpr uint8_t LOG_INFO = 0;
	static constexpr uint8_t LOG_WARN = 0;
	static constexpr uint8_t LOG_ERROR = 0;

//	MemProg() : LocalParam(), CurrentHandler(nullptr), Active(false), Interface(0)
//	{
//	}
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
		Param->Token = MEMPROG_TOKEN_HOST;
	}

	static void StaticRun() {
		// TODO select which instance to run based on:
		//  - whether it's active (has a command running)
		//  - whether it's waiting for a free buffer (must be active for this to be true)
		//  - which instance ran last time (don't run the same one twice if another command is active/waiting)

		// Currently, we just run them sequentially
		MemProg * const * ptr = Interfaces;

		for (; *ptr; ptr++) {
			// find the index of the current interface
			if (*ptr == CurrentInterface) {
				// and choose the next one
				CurrentInterface = *(ptr + 1);
				break;
			}
		}

		// if we reached the end of the list, go back to the start
		if (!CurrentInterface) {
			CurrentInterface = Interfaces[0];
		}

		// TODO if nothing ran, try another interface (until all are tried, then return to shell)
		CurrentInterface->Run();
	}

private:
	static inline uint8_t BufferIndex(const volatile MEMPROG_BDT * const bdt) {
		return bdt - BufferDescriptors;
	}
	inline void DEFAULT_HANDLER() { LocalParam.Status = MEMPROG_STATUS_ERR_OTHER; }

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

	static uint8_t * GetBufferAddress(const MEMPROG_BDT * const bdt) {
		return const_cast<uint8_t *>(Buffer + BufferIndex(bdt) * BufferSize);
	}

	MEMPROG_BDT * AcquireBuffer() const {
		// Loop through BDTs until a free one is found
		uint8_t i;
		for (i = 0; i < NumBuffers; i++) {
			volatile MEMPROG_BDT &bdt = BufferDescriptors[i];
			if (bdt.Status == MEMPROG_BUFFER_STATUS_FREE) {
				bdt.Status = MEMPROG_BUFFER_STATUS_PENDING;
				bdt.Interface = Interface;
				return const_cast<MEMPROG_BDT *>(&bdt);
			}
		}
		return nullptr;
	}

	MEMPROG_BDT * GetNextFullBuffer() const {
		// Find a full buffer assigned to this interface. If there are multiple, return the one with the lowest address
		uint32_t Address = 0xFFFFFFFF;
		volatile MEMPROG_BDT * bestbdt = nullptr;

		uint8_t i;
		for (i = 0; i < NumBuffers; i++) {
			volatile MEMPROG_BDT &bdt = BufferDescriptors[i];
			if (bdt.Interface == Interface && bdt.Status == MEMPROG_BUFFER_STATUS_FULL && bdt.Address < Address) {
				Address = bdt.Address;
				bestbdt = &bdt;
			}
		}
		return const_cast<MEMPROG_BDT *>(bestbdt);
	}

	static void FillBuffer(const MEMPROG_BDT * const bdt) {
		BufferDescriptors[BufferIndex(bdt)].Status = MEMPROG_BUFFER_STATUS_FULL;
	}

	static void ReleaseBuffer(const MEMPROG_BDT * const bdt) {
		BufferDescriptors[BufferIndex(bdt)].Status = MEMPROG_BUFFER_STATUS_FREE;
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

	// Return true if a command was run
	void Run() {
		// TODO since OpenOCD doesn't support multiple parameter bases, maybe stop supporting it here too?
		//  While we're at it, stop supporting multiple BDT bases, buffer bases?
		//  ALl that info would be passed to StaticInit instead of constructor.
		//  Interface number could be implied from the order of the Interfaces array
		if (Param->Token != MEMPROG_TOKEN_TARGET) {
			return;
		}

		if (!Active) {
			// Interface only needs to be checked to start a new command
			//  If a command is already active, it doesn't read from Params, so we can just run it
			if (Param->Interface != Interface) {
				return;
			}

			// Check if host wants to start a command
			if (Param->Status == _MEMPROG_STATUS_START) {
				// Copy the volatile params to LocalParams
				Active = true;
				memcpy(&LocalParam, (const void *)Param, sizeof(LocalParam));

				lputc(0x00);
				lputc(Interface);
				lputc(LocalParam.Command);

				// Acknowledge the command by changing status to IDLE and passing token back after copying Params
				Param->Status = _MEMPROG_STATUS_IDLE;

				// Check if a handler for this command exists
				if (!(CurrentHandler = BaseGetHandler(Param->Command))) {
					LocalParam.Status = MEMPROG_STATUS_ERR_PARAM;
				}
			}
		}

		if (Active) {
			// if status hasn't bene set yet, keep running the command
			if (LocalParam.Status < MEMPROG_STATUS_OK) {
				// Status will be START the first time this is called. Handlers can use this fact to reset their state
				(this->*CurrentHandler)();
				// After the first run, change Status to something else, unless it has already changed to a valid return value
				if (LocalParam.Status < MEMPROG_STATUS_OK) {
					LocalParam.Status = _MEMPROG_STATUS_IDLE;
				}
			}

			// if status has been set, the command has finished; notify the host by modifying Param.Status
			if (LocalParam.Status > MEMPROG_STATUS_OK) {
				lputc(0x01);
				lputc(Interface);
				lputc(LocalParam.Command);

				if (Param->Status == _MEMPROG_STATUS_IDLE) {
					// We can only write to Param if Status != IDLE, otherwise we would be overwriting a pending command
					// or returned data from another interface. In this case just return and try again next time
					return;// TODO ?
				}

				lputc(0x02);

				Active = false;
				CurrentHandler = nullptr;

//				LocalParam.Token = MEMPROG_TOKEN_TARGET; This is necessarily already TOKEN_TARGET based on the first if statement in this function

				memcpy((void *)Param, &LocalParam, sizeof(MEMPROG_PARAM));
				// Params.Token must be changed after all the other params. It indicates to the host
				// that all other params are valid to read
				Param->Token = MEMPROG_TOKEN_HOST;
			}
		}

		// Allow host to process
		Param->Token = MEMPROG_TOKEN_HOST;
	}
};
