#pragma once
#include "memprog.h"
#include <cstring>

// Base class which defines the constructor and provides stubs for command methods
class MemProg {
public:
	MemProg() : LocalParam(), CurrentHandler(nullptr), Active(false)
	{
	}
	virtual ~MemProg() = default;

	/// nullptr terminated array of MemProg *
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

		Param->Status = MEMPROG_STATUS_IDLE;
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
		bool Ran = CurrentInterface->Run();
	}

	constexpr uint8_t BufferIndex(const volatile uint8_t * const Address) {
		return (Address - Buffer) / BufferSize;
	}

	volatile uint8_t * AcquireBuffer() {
		// Loop through BDTs until a free one is found
		uint8_t i;
		for (i = 0; i < NumBuffers; i++) {
			volatile MEMPROG_BDT &bdt = BufferDescriptors[i];
			if (bdt.Status == MEMPROG_BUFFER_STATUS_FREE) {
				bdt.Status = MEMPROG_BUFFER_STATUS_PENDING;
				bdt.Interface = Interface;
				return static_cast<volatile uint8_t*>(Buffer + (i * BufferSize));
			}
		}
		return nullptr;
	}

	volatile uint8_t * GetNextFullBuffer() {
		// Find a full buffer assigned to this interface. If there are multiple, return the one with the lowest address
		uint32_t Address = 0xFFFFFFFF;
		uint8_t i;
		for (i = 0; i < NumBuffers; i++) {
			volatile MEMPROG_BDT &bdt = BufferDescriptors[i];
			if (bdt.Status == MEMPROG_BUFFER_STATUS_FULL && bdt.Address < Address) {
				Address = bdt.Address;
			}
		}
		return Address == 0xFFFFFFFF ? nullptr : reinterpret_cast<volatile uint8_t*>(Address);
	}

	void FillBuffer(const volatile uint8_t * const Address) {
		BufferDescriptors[BufferIndex(Address)].Status = MEMPROG_BUFFER_STATUS_FULL;
	}

	void ReleaseBuffer(const volatile uint8_t * const Address) {
		BufferDescriptors[BufferIndex(Address)].Status = MEMPROG_BUFFER_STATUS_FREE;
	}

protected:
	MEMPROG_PARAM LocalParam;

	using CMD_FUNC = void (MemProg::*)();

	virtual void Init() {}

	virtual CMD_FUNC GetHandler(MEMPROG_CMD Command) {
		return nullptr;
	}

private:
	CMD_FUNC CurrentHandler;
	bool Active;
	uint8_t Interface;

//	const uint32_t Interface;
    // TODO test this. The friend class `Programming` must set up these static const members
//	friend Programming;
	static MemProg * const * Interfaces;
	static volatile MEMPROG_PARAM * const Param;
	static volatile MEMPROG_BDT * const BufferDescriptors;
	static volatile uint8_t * const Buffer;
	static const uint32_t BufferSize;
	static const uint32_t NumBuffers;

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
			case MEMPROG_CMD_QUERY_CAP:
				return &MemProg::CMD_QUERY_CAP;

			default:
				return GetHandler(Command);
		}
	}

	// Return true if a command was run
	bool Run() {
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
			if (Param->Status == MEMPROG_STATUS_START) {
				// Acknowledge the command by changing status to IDLE and passing token back after copying Params
				Param->Status = MEMPROG_STATUS_IDLE;
				// Copy the volatile params to LocalParams
				Active = true;
				memcpy(&LocalParam, Param, sizeof(LocalParam));

				// Check if a handler for this command exists
				if (!(CurrentHandler = BaseGetHandler(Param->Command))) {
					LocalParam.Status = MEMPROG_STATUS_ERR_PARAM;
				}
			}
		}

		if (Active) {
			// if status == START (hasn't changed yet), keep running the command
			if (LocalParam.Status == MEMPROG_STATUS_START) {
				(this->*CurrentHandler)();
			}

			// if status != START, the command has finished; notify the host by modifying Param.Status
			if (LocalParam.Status != MEMPROG_STATUS_START) {
				if (Param->Status == MEMPROG_STATUS_IDLE) {
					// We can only write to Param if Status != IDLE, otherwise we would be overwriting a pending command
					// or returned data from another interface. In this case just return and try again next time
					return;
				}

				Active = false;
				CurrentHandler = nullptr;

//				LocalParam.Token = MEMPROG_TOKEN_TARGET; This is necessarily already TOKEN_TARGET based on the first if statement in this function

				memcpy(Param, &LocalParam, sizeof(MEMPROG_PARAM));
				// Params.Token must be changed after all the other params. It indicates to the host
				// that all other params are valid to read
				Param->Token = MEMPROG_TOKEN_HOST;
			}
		}

		// Allow host to process
		Param.Token = MEMPROG_TOKEN_HOST;
	}
};
