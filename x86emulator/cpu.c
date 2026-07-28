#include "emulator.h"
#include "assert.h"


#define AS(type) (type*)&




enum REG_TYPES {
	R_A,
	R_B,
	R_C,
	R_D,

	R_SP,
	R_DS,
	R_SS,
	R_ES,
	R_FLAGS,
	R_IP,

	R_COUNT
};

enum FLAGS {
	FLAG_CARRY		= 0b0000000000000001,
	FLAG_PARITY		= 0b0000000000000100,
	FLAG_AUX_CARRY	= 0b0000000000010000,
	FLAG_ZERO		= 0b0000000001000000,
	FLAG_SIGN		= 0b0000000010000000,
	FLAG_TRAP		= 0b0000000100000000,
	FLAG_INT_ENABLE = 0b0000001000000000,
	FLAG_DIRECTION	= 0b0000010000000000,
	FLAG_OVERFLOW	= 0b0000100000000000,
};

#define MEM_COUNT 1024

byte memory[MEM_COUNT];


reg registers[R_COUNT];


word address_bus;

typedef struct {
	union {
		void* ptr;
		word* pword;
		byte* pbyte;
	};
} operand;


static inline void set_flag(enum FLAGS flag, int value) {
	if (value) {
		registers[R_FLAGS] |= flag;
	}
	else {
		registers[R_FLAGS] &= ~flag;
	}
}
static inline int get_flag(enum FLAGS flag) {
	return (registers[R_FLAGS] & flag) ? 1 : 0;
}


#define SIGN_BIT(op, bits) ((op >> (bits - 1)) & 1)

#define SET_ZERO(res) set_flag(FLAG_ZERO,res == 0);
#define SET_SIGN(res, bits) set_flag(FLAG_SIGN,SIGN_BIT(res,bits));
#define SET_CARRY(dst,res,sign) if(sign) {set_flag(FLAG_CARRY,dst > res);} else {set_flag(FLAG_CARRY,dst < res);}
#define SET_AUX_CARRY(dst,res) set_flag(FLAG_AUX_CARRY, (dst & 0b10000) != (res & 0b10000));
#define SET_OVERFLOW(dst, src, res, sign, bits) if(sign) {set_flag(FLAG_OVERFLOW,(SIGN_BIT(src,bits) == SIGN_BIT(dst,bits)) && (SIGN_BIT(res,bits) != SIGN_BIT(dst,bits)));} else {set_flag(FLAG_OVERFLOW,(SIGN_BIT(src,bits) == SIGN_BIT(res,bits)) && (SIGN_BIT(res,bits) != SIGN_BIT(dst,bits)));}

#define SET_PARITY(res) \
	{ \
		int parity = 1;\
		for (size_t i = 0; i < 8; i++)\
		{\
			parity ^= (res >> 1) & 1;\
		}\
		set_flag(FLAG_PARITY, parity);\
	} \

#define SET_PZS(res, bits) SET_PARITY(res) SET_ZERO(res) SET_SIGN(res, bits)
#define SET_OC(dst, src, res, sign, bits) SET_OVERFLOW(dst, src, res, sign, bits) SET_CARRY(dst, res, sign)
#define CLR_OC set_flag(FLAG_OVERFLOW, 0); set_flag(FLAG_CARRY, 0);

#define SET_OCAPZS(dst, src, res, sign, bits) SET_OC(dst, src, res, sign, bits) SET_AUX_CARRY(dst,res) SET_PZS(res, bits)


static byte get_low(word w) {
	return (byte)(w & 0xFF);
}
static byte get_high(word w) {
	return (byte)((w>>8) & 0xFF);
}

static void set_low(word* w, byte value) {
	*w = (*w & 0xFF00) | (value & 0xFF);
}
static void set_high(word* w, byte value) {
	*w = (*w & 0x00FF) | ((value & 0xFF) << 8);
}

static byte* map_byte(address_physical phys) {
	assert(phys < MEM_COUNT);
	return (&memory[phys]);
}

static word* map_word(address_physical phys) {
	assert(phys < (MEM_COUNT - 1));
	return (word*)(&memory[phys]);
}

static address_physical get_physical_addr(int segment_reg, int index) {

	address_physical seg_base = (address_physical)(registers[segment_reg]);
	address_physical a = ((seg_base << 4) + index) % 0xFFFFF;
	return a;
}


static byte get_mapped_memory(address addr) {

	address_physical phys = get_physical_addr(R_DS, addr);
	return *map_byte(phys);
}


static void push_stack(word value) {
	registers[R_SP] -= 2;
	*map_word(get_physical_addr(R_SS, registers[R_SP])) = value;
}

static word pop_stack() {
	word value = *map_word(get_physical_addr(R_SS, registers[R_SP]));
	registers[R_SP] += 2;
	return value;
}


#define ARITH_OP(type, dst, op, src, res, write) \
	type dst = *ops[0].p##type; \
	type src = *ops[1].p##type; \
	type res = src op dst; \
	if(write) { *ops[0].p##type = res;  }

void ADD(operand* ops) {
	ARITH_OP(word, dst, +, src, res, 1)
	SET_OCAPZS(dst,src,res,1,16)
}



void run(uint8_t opcode) {


	switch (opcode)
	{
	default:
		break;
	}
}