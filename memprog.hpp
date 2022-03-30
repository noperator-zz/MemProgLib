#pragma once
#include "memprog.h"
#include <cstring>

// Base class which defines the constructor and provides stubs for command methods
class MemProg {
public:
	MemProg() = delete;
	MemProg(uint32_t Interface, volatile MEMPROG_PARAM & Param, volatile MEMPROG_BDT * const BufferDescriptors,
			volatile uint8_t * const Buffer, uint32_t BufferSize, uint32_t NumBuffers) :
			LocalParam(), Interface(Interface), Param(Param), BufferDescriptors(BufferDescriptors),
			Buffer(Buffer),	BufferSize(BufferSize), NumBuffers(NumBuffers),
			CurrentHandler(nullptr), Active(false)
	{
		Param.Status = MEMPROG_STATUS_OK;
		uint32_t i;
		// Clear buffer descriptors
		for (i = 0; i < NumBuffers; i++) {
			memset((void*)(BufferDescriptors + i), 0, sizeof(MEMPROG_BDT));
		}
	}
	virtual ~MemProg() = default;

	/// nullptr terminated array of MemProg *
	static void StaticInit(MemProg ** Instances) {
		MemProg * const * inst;
		MemProg::Interfaces = Instances;
		MemProg * const * ptr = Interfaces;

		// Initialize each interface
		for (; *ptr; ptr++) {
			(*ptr)->Init();
		}
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

		CurrentInterface->Run();
	}

	constexpr uint32_t BufferIndex(const volatile uint8_t * const Address) {
		return (Address - Buffer) / BufferSize;
	}

	volatile uint8_t * AcquireBuffer() {
		// Loop through BDTs until a free one is found
		uint32_t i;
		for (i = 0; i < NumBuffers; i++) {
			if (BufferDescriptors[i].Status == MEMPROG_BUFFER_STATUS_FREE) {
				BufferDescriptors[i].Status = MEMPROG_BUFFER_STATUS_PENDING;
				BufferDescriptors[i].Interface = Interface;
				return static_cast<volatile uint8_t*>(Buffer + (i * BufferSize));
			}
		}
		return nullptr;
	}

	volatile uint8_t * GetNextFullBuffer() {
		// Find a full buffer assigned to this interface. If there are multiple, return the one with the lowest address
		uint32_t Address = 0xFFFFFFFF;
		uint32_t i;
		for (i = 0; i < NumBuffers; i++) {
			if (BufferDescriptors[i].Status == MEMPROG_BUFFER_STATUS_FULL && BufferDescriptors[i].Address < Address) {
				Address = BufferDescriptors[i].Address;
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
	static inline MemProg * const * Interfaces = nullptr;
	static inline MemProg * CurrentInterface = nullptr;

	void CMD_QUERY_CAP() {
		LocalParam.Code = MEMPROG_VERSION;
		LocalParam.P1 = (uint32_t)BufferDescriptors;
		LocalParam.P2 = (uint32_t)Buffer;
		LocalParam.P3 = (NumBuffers << 24) | BufferSize;
		LocalParam.Status = MEMPROG_STATUS_OK;
	}

	const uint32_t Interface;
	volatile MEMPROG_PARAM & Param;
	volatile MEMPROG_BDT * const BufferDescriptors;
	volatile uint8_t * const Buffer;
	const uint32_t BufferSize;
	const uint32_t NumBuffers;

	CMD_FUNC CurrentHandler;
	bool Active;

	CMD_FUNC BaseGetHandler(MEMPROG_CMD Command) {
		switch (Command) {
			case MEMPROG_CMD_QUERY_CAP:
				return &MemProg::CMD_QUERY_CAP;

			default:
				return GetHandler(Command);
		}
	}

	void Run() {
		if (Param.Token != MEMPROG_TOKEN_TARGET) {
			return;
		}
		if (Param.Interface != Interface) {
			return;
		}

		if (!Active) {
			// Check if host wants to start a command
			if (Param.Status == MEMPROG_STATUS_BUSY) {
				// Copy the volatile params to LocalParams
				Active = true;
				memcpy(LocalParam, Param, sizeof(LocalParam));

				// Check if a handler for this command exists
				if (!(CurrentHandler = BaseGetHandler(Param.Command))) {
					LocalParam.Status = MEMPROG_STATUS_ERR_PARAM;
				}
			}
		}

		if (Active) {
			// if status == busy, keep running the command
			if (LocalParam.Status == MEMPROG_STATUS_BUSY) {
				(this->*CurrentHandler)();
			}

			// if status != busy, the command has finished; notify the host by modifying Params.Status
			if (LocalParam.Status != MEMPROG_STATUS_BUSY) {
				Active = false;
				CurrentHandler = nullptr;

//				LocalParam.Token = MEMPROG_TOKEN_TARGET; This is necessarily already TOKEN_TARGET based on the first if statement in this function

				memcpy(Param, LocalParam, sizeof(MEMPROG_PARAM));
				// Params.Token must be changed after all the other params. It indicates to the host
				// that all other params are valid to read
				Param.Token = MEMPROG_TOKEN_HOST;
			}
		}
	}
};
