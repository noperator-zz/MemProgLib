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

	virtual void Init() {}

	/// nullptr terminated array of MemProg *
	static void StaticInit(MemProg ** Instances) {
		MemProg * inst;
		MemProg::Interfaces = Instances;

		// Initialize each interface
		for (inst = *Interfaces; inst; inst++) {
			inst->Init();
		}
	}

	static void StaticRun() {
		// TODO select which instance to run based on:
		//  - whether it's active (has a command running)
		//  - whether it's waiting for a free buffer
		//  - which instance ran last time (don't run the same one twice if another command is active/waiting)
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
//	MEMPROG_STATUS Status;
//	uint32_t Code;

	using CMD_FUNC = void (MemProg::*)();

	virtual CMD_FUNC GetHandler(MEMPROG_CMD Command) {
		return nullptr;
	}



private:
	static MemProg ** Interfaces;

//	inline bool _CMD_DEFAULT() {
//		return true;
//	}

	virtual void CMD_QUERY_CAP() {
		LocalParam.P1 = (uint32_t)Buffer;
		LocalParam.P2 = BufferSize;
		LocalParam.P3 = NumBuffers;
		LocalParam.Status = MEMPROG_STATUS_OK;
	}
//	virtual bool CMD_CONTINUE_QUERY_CAP() { return _CMD_DEFAULT(); }// __attribute__ ((weak, alias ("_ZN7MemProg12_CMD_DEFAULTEv")));
//	virtual bool CMD_START_MASS_ERASE() { return _CMD_DEFAULT(); }// __attribute__ ((weak, alias ("_ZN7MemProg12_CMD_DEFAULTEv")));
//	virtual bool CMD_CONTINUE_MASS_ERASE() { return _CMD_DEFAULT(); }// __attribute__ ((weak, alias ("_ZN7MemProg12_CMD_DEFAULTEv")));
//	virtual bool CMD_START_PROG_INIT() { return _CMD_DEFAULT(); }// __attribute__ ((weak, alias ("_ZN7MemProg12_CMD_DEFAULTEv")));
//	virtual bool CMD_CONTINUE_PROG_INIT() { return _CMD_DEFAULT(); }// __attribute__ ((weak, alias ("_ZN7MemProg12_CMD_DEFAULTEv")));
//	virtual bool CMD_START_PROG() { return _CMD_DEFAULT(); }// __attribute__ ((weak, alias ("_ZN7MemProg12_CMD_DEFAULTEv")));
//	virtual bool CMD_CONTINUE_PROG() { return _CMD_DEFAULT(); }// __attribute__ ((weak, alias ("_ZN7MemProg12_CMD_DEFAULTEv")));
//	virtual bool CMD_START_PROG_FINI() { return _CMD_DEFAULT(); }// __attribute__ ((weak, alias ("_ZN7MemProg12_CMD_DEFAULTEv")));
//	virtual bool CMD_CONTINUE_PROG_FINI() { return _CMD_DEFAULT(); }// __attribute__ ((weak, alias ("_ZN7MemProg12_CMD_DEFAULTEv")));

//	struct CMD_INFO {
//		MEMPROG_CMD Command;
//		CMD_FUNC Func;
//	};

//	// NOTE: Make sure to update memprog.h:MEMPROG_CMD if changing these
//	static constexpr CMD_INFO COMMAND_MAP[]{
//			{MEMPROG_CMD_QUERY_CAP,  &MemProg::CMD_QUERY_CAP},
//			{MEMPROG_CMD_MASS_ERASE, &MemProg::CMD_MASS_ERASE},
//			{MEMPROG_CMD_PROG_INIT,  &MemProg::CMD_PROG_INIT},
//			{MEMPROG_CMD_PROG,       &MemProg::CMD_PROG},
//			{MEMPROG_CMD_PROG_FINI,  &MemProg::CMD_PROG_FINI},
//	};

	const uint32_t Interface;
	volatile MEMPROG_PARAM & Param;
	volatile MEMPROG_BDT * const BufferDescriptors;
	volatile uint8_t * const Buffer;
	const uint32_t BufferSize;
	const uint32_t NumBuffers;

//	const CMD_INFO * CurrentCommandInfo;
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
		if (Param.Token == MEMPROG_TOKEN_HOST) {
			return;
		}
		if (Param.Interface != Interface) {
			return;
		}

		if (!Active) {
			if (Param.Status == MEMPROG_STATUS_BUSY) {
				Active = true;
//				LocalParam = MEMPROG_PARAM {};
				LocalParam.Status = MEMPROG_STATUS_BUSY;
				// Host wants to start a command
				// Check if a handler for this command exists
				if (!(CurrentHandler = BaseGetHandler(Param.Command))) {
					LocalParam.Status = MEMPROG_STATUS_ERR_PARAM;
				}
			}
		}

		if (Active) {
			if (LocalParam.Status == MEMPROG_STATUS_BUSY) {
				(this->*CurrentHandler)();
			}

			if (LocalParam.Status != MEMPROG_STATUS_BUSY) {
				Active = false;
				CurrentHandler = nullptr;

				MEMPROG_STATUS RealStatus = LocalParam.Status;
				LocalParam.Status = MEMPROG_STATUS_BUSY;

				memcpy(Param, LocalParam, sizeof(MEMPROG_PARAM));
				Param.Status = RealStatus;
			}
		}
	}

//	void Inspect(MEMPROG_CMD & CurrentCommand, MEMPROG_STATUS &CurrentStatus, uint32_t &CurrentCode) {
//		if (CurrentCommandInfo) {
//			CurrentCommand = CurrentCommandInfo->Command;
//		} else {
//			CurrentCommand = MEMPROG_CMD_NONE;
//		}
//
//		CurrentStatus = Status;
//		CurrentCode = Code;
//	}
};
