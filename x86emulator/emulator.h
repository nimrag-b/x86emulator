#pragma once

#include <stdint.h>
#include "opcodes.h"

typedef uint8_t byte;
typedef uint16_t word;
typedef uint32_t dword;
typedef uint64_t qword;
typedef int8_t sbyte;
typedef int16_t sword;
typedef int32_t sdword;
typedef int64_t sqword;

typedef uint16_t address;


typedef uint32_t address_physical;


typedef uint16_t reg;


#define AS(type) (type*)&

//little endian machines store bytes smallest first
#define LO_IDX (0)
#define HI_IDX (1)

enum REG_TYPES {
	R_A,
	R_C,
	R_D,
	R_B,

	R_SP,
	R_BP,
	R_SI,
	R_DI,

	R_ES,
	R_CS,
	R_SS,
	R_DS,

	R_FLAGS,
	R_IP,

	R_COUNT
};

enum FLAGS {
	FLAG_CARRY = 0b0000000000000001,
	FLAG_PARITY = 0b0000000000000100,
	FLAG_AUX_CARRY = 0b0000000000010000,
	FLAG_ZERO = 0b0000000001000000,
	FLAG_SIGN = 0b0000000010000000,
	FLAG_TRAP = 0b0000000100000000,
	FLAG_INT_ENABLE = 0b0000001000000000,
	FLAG_DIRECTION = 0b0000010000000000,
	FLAG_OVERFLOW = 0b0000100000000000,
};

#define MEM_COUNT 1048576



enum ADDRESSING_MODE {
	A_NONE = 0,
	A_RM, //mod section of modrm
	A_REG, //reg section of modrm
	A_SEG, //reg selects segment register
	A_MEM, //reg selects memory only

	A_OBYTE, //immediate 1 byte (same as IMM8 but used for AAM and AAD) Suppressed in ASM when 0xA
	A_IMM8, //immediate 1 byte
	A_IMM16, //immediate 2 byte
	A_ADDR, //32 bit address pointer
	A_OFF8, //no modrm, offset encoded as word
	A_OFF16,


	A_1, //implict 1
	A_3, //implicit 3


	A_AX,
	A_CX,
	A_DX,
	A_BX,
	A_SP,
	A_BP,
	A_SI,
	A_DI,

	A_AL,
	A_CL,
	A_DL,
	A_BL,
	A_AH,
	A_CH,
	A_DH,
	A_BH,

	A_ES,
	A_CS,
	A_SS,
	A_DS,

	//jump byte and word for branch ops
	A_JB,
	A_JV,
};

typedef struct {
	uint16_t opcode;
	uint8_t dst;
	uint8_t src;
	uint8_t bits;
} instr_t;


#define OP_INVALID {I_ERROR,0,0,0}


void run();