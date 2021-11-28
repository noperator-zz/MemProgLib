#include "memprog.hpp"

volatile MEMPROG_PARAM PARAM;
volatile uint8_t BUFFER[0x800];

class test : public MemProg {
	using MemProg::MemProg;
	virtual bool CMD_START_MASS_ERASE() {return false;};
};

test m {0, PARAM, BUFFER, 0x400, 2};
//MemProg m {0, PARAM, BUFFER, 0x400, 2};

int main(int argc, char **argv) {
	PARAM.Command = MEMPROG_CMD_MASS_ERASE;
	PARAM.Status = MEMPROG_STATUS_BUSY;

	m.Run();
	return PARAM.Status;
}
