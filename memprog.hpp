#pragma once
#include "memprog.h"

// Base class which defines the constructor and provides stubs for command methods
class MemProg {
protected:
	using CMD_FUNC = bool (MemProg::*)();

	struct CMD_INFO {
		MEMPROG_CMD Command;
		CMD_FUNC Start;
		CMD_FUNC Continue;
	};

public:
	MemProg(uint32_t Interface, volatile MEMPROG_PARAM & Param, const volatile uint8_t * Buffer,
			uint32_t BufferSize, uint32_t NumBuffers) :
			Interface(Interface), Param(Param),	Buffer(Buffer), BufferSize(BufferSize),
			NumBuffers(NumBuffers), CurrentCommandInfo(nullptr), Status(MEMPROG_STATUS_OK), Code(0)
			{
				Param.Status = MEMPROG_STATUS_OK;
			}


	void Run() {
		if (Param.Interface != Interface) {
			return;
		}

		bool Done = false;
		Status = MEMPROG_STATUS_OK;
		Code = 0;

		if (!CurrentCommandInfo) {
			if (Param.Status == MEMPROG_STATUS_BUSY) {
				// start command
				if (!(CurrentCommandInfo = GetInfo(Param.Command))) {
					Status = MEMPROG_STATUS_ERR_PARAM;
					Done = true;
				} else {
					Done = (this->*CurrentCommandInfo->Start)();
				}
			}
		} else {
			Done = (this->*CurrentCommandInfo->Continue)();
		}

		if (Done) {
			CurrentCommandInfo = nullptr;

			if (Status == MEMPROG_STATUS_BUSY) {
				// Busy is a special status that causes OpenOCD to wait. It cannot be used as a final
				//  return status. This indicates a problem in one of the command functions.
				Status = MEMPROG_STATUS_ERR_OTHER;
				// Modify `Code` to indicate that this occurred
				Code |= 0x00800000;
			}
			Param.Code = Code & 0x00FFFFFF;
			Param.Status = Status;
		}
	}

	void Inspect(MEMPROG_CMD & CurrentCommand, MEMPROG_STATUS &CurrentStatus, uint32_t &CurrentCode) {
		if (CurrentCommandInfo) {
			CurrentCommand = CurrentCommandInfo->Command;
		} else {
			CurrentCommand = MEMPROG_CMD_NONE;
		}

		CurrentStatus = Status;
		CurrentCode = Code;
	}

private:
	const uint32_t Interface;
	volatile MEMPROG_PARAM & Param;
	const volatile uint8_t * Buffer;
	const uint32_t BufferSize;
	const uint32_t NumBuffers;

	CMD_INFO * CurrentCommandInfo;

protected:
	MEMPROG_STATUS Status;
	uint32_t Code;

	volatile MEMPROG_PARAM & GetParam() { return Param; }

	virtual void Init() {}

	virtual bool CMD_START_QUERY_CAP() {
		Param.P1 = (uint32_t)Buffer;
		Param.P2 = BufferSize;
		Param.P3 = NumBuffers;
		return true;
	}
	virtual bool CMD_CONTINUE_QUERY_CAP() {return true;}

	virtual bool CMD_START_MASS_ERASE() {return true;}
	virtual bool CMD_CONTINUE_MASS_ERASE() {return true;}
	virtual bool CMD_START_PROG_INIT() {return true;}
	virtual bool CMD_CONTINUE_PROG_INIT() {return true;}
	virtual bool CMD_START_PROG() {return true;}
	virtual bool CMD_CONTINUE_PROG() {return true;}
	virtual bool CMD_START_PROG_FINI() {return true;}
	virtual bool CMD_CONTINUE_PROG_FINI() {return true;}

	// NOTE: Make sure to update memprog.h:MEMPROG_CMD if changing these
	static constexpr CMD_INFO COMMAND_MAP[]{
			{MEMPROG_CMD_QUERY_CAP,  &MemProg::CMD_START_QUERY_CAP,  &MemProg::CMD_CONTINUE_QUERY_CAP},
			{MEMPROG_CMD_MASS_ERASE, &MemProg::CMD_START_MASS_ERASE, &MemProg::CMD_CONTINUE_MASS_ERASE},
			{MEMPROG_CMD_PROG_INIT,  &MemProg::CMD_START_PROG_INIT,  &MemProg::CMD_CONTINUE_PROG_INIT},
			{MEMPROG_CMD_PROG,       &MemProg::CMD_START_PROG,       &MemProg::CMD_CONTINUE_PROG},
			{MEMPROG_CMD_PROG_FINI,  &MemProg::CMD_START_PROG_FINI,  &MemProg::CMD_CONTINUE_PROG_FINI},
	};

private:
	static constexpr CMD_INFO * GetInfo(MEMPROG_CMD Command) {
		// search for code in map
		for (auto & Info : COMMAND_MAP){
			if (Info.Command == Command) {
				return const_cast<CMD_INFO *>(&Info);
			}
		}
		return nullptr;
	}
};
