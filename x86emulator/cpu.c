#include "emulator.h"
#include "assert.h"
#include "stdio.h"


#define E_FAILURE (-1)

#define PANIC(str) printf("ERROR ON LINE %d: " str " \n", __LINE__); __debugbreak(); return E_FAILURE;


byte memory[MEM_COUNT];

reg registers[R_COUNT];

int halted = 0;


#define AX (&registers[R_A])
#define CX (&registers[R_C])
#define DX (&registers[R_D])
#define BX (&registers[R_B])

#define AL (low(AX))
#define AH (high(AX))

#define CL (low(CX))
#define CH (high(CX))

#define DL (low(DX))
#define DH (high(DX))

#define BL (low(BX))
#define BH (high(BX))


typedef struct {
	union {
		void* ptr;
		word* pword;
		byte* pbyte;
	};
	address addr; //contains the address value if referencing modrm memory
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




#define SIGN_BIT(op, bits) (((op) >> (bits - 1)) & 1)

#define SET_ZERO(res) set_flag(FLAG_ZERO,res == 0);
#define SET_SIGN(res, bits) set_flag(FLAG_SIGN,SIGN_BIT(res,bits));
#define SET_CARRY(dst,res,sign) if(sign) {set_flag(FLAG_CARRY,dst > res);} else {set_flag(FLAG_CARRY,dst < res);}
#define SET_AUX_CARRY(dst,res) set_flag(FLAG_AUX_CARRY, (dst & 0b10000) != (res & 0b10000));
#define SET_OVERFLOW(dst, src, res, sign, bits) if(sign) {set_flag(FLAG_OVERFLOW,(SIGN_BIT(src,bits) == SIGN_BIT(dst,bits)) && (SIGN_BIT(res,bits) != SIGN_BIT(dst,bits)));} else {set_flag(FLAG_OVERFLOW,(SIGN_BIT(src,bits) == SIGN_BIT(res,bits)) && (SIGN_BIT(res,bits) != SIGN_BIT(dst,bits)));}

//theres no need for this to be a define, and this will save on space
static void SET_PARITY(int res) {
	int parity = 1;
	for (size_t i = 0; i < 8; i++)
	{
		parity ^= (res >> 1) & 1;
	}
	set_flag(FLAG_PARITY, parity);
}

#define SET_PZS(res, bits) SET_PARITY(res); SET_ZERO(res); SET_SIGN(res, bits);
#define SET_OC(dst, src, res, sign, bits) SET_OVERFLOW(dst, src, res, sign, bits); SET_CARRY(dst, res, sign);
#define CLR_OC set_flag(FLAG_OVERFLOW, 0); set_flag(FLAG_CARRY, 0);

#define SET_OCAPZS(dst, src, res, sign, bits) SET_OC(dst, src, res, sign, bits); SET_AUX_CARRY(dst,res); SET_PZS(res, bits);



#define _ARITH_OP_TYPE(type, op, write) \
	type dst = *pdst.p##type;\
	type src = *psrc.p##type;\
	res = op;\
	if (write) {\
		*pdst.p##type = (type)res;\
	}


#define ARITH_OP(op, write, ...) \
	if (bits == 8) {\
		_ARITH_OP_TYPE(byte,src op dst, write)\
		__VA_ARGS__\
	}\
	else if (bits == 16) {\
		_ARITH_OP_TYPE(word, src op dst, write)\
		__VA_ARGS__\
	}\
	else {\
		PANIC("Invalid bit count");\
	}\




#define CUSTOM_OP(op, write, ...) \
	if (bits == 8) {\
		_ARITH_OP_TYPE(byte, op, write)\
		__VA_ARGS__\
	}\
	else if (bits == 16) {\
		_ARITH_OP_TYPE(word, op, write)\
		__VA_ARGS__\
	}\
	else {\
		PANIC("Invalid bit count");\
	}\


#define INSTR static inline void

#define SET_OCAPZS_FUNC(type, bits) \
void set_ocapzs_##type(type dst, type src, type res, int sign) { \
	SET_OCAPZS(dst, src, res, sign, bits); \
} 

SET_OCAPZS_FUNC(byte, 8)
SET_OCAPZS_FUNC(word, 16)
SET_OCAPZS_FUNC(dword, 32)
SET_OCAPZS_FUNC(qword, 64)


const char* opcode_strings[] = {
#define OPCODE_OF(X) STRINGIFY(X),
#define PREFIX_OF(X) STRINGIFY(:X)
	OPCODE_FOREACH
#undef OPCODE_OF
#undef PREFIX_OF
};


instr_t base8086map[] = {
	/*0x0*/	{I_ADD, A_RM, A_REG, 8},	{I_ADD, A_RM, A_REG, 16},	{I_ADD, A_REG, A_RM, 8},	{I_ADD, A_REG, A_RM, 16},	{I_ADD, A_AL, A_IMM8, 8},	{I_ADD, A_AX, A_IMM16, 16},	{I_PUSH,A_ES,0,16},		{I_POP,A_ES,0,16},		{I_OR, A_RM, A_REG, 8},		{I_OR, A_RM, A_REG, 16},	{I_OR, A_REG, A_RM, 8},		{I_OR, A_REG, A_RM, 16},	{I_OR, A_AL, A_IMM8, 8},	{I_OR, A_AX, A_IMM16, 16},	{I_PUSH,A_CS,0,16},		OP_INVALID,
	/*0x1*/	{I_ADC, A_RM, A_REG, 8},	{I_ADC, A_RM, A_REG, 16},	{I_ADC, A_REG, A_RM, 8},	{I_ADC, A_REG, A_RM, 16},	{I_ADC, A_AL, A_IMM8, 8},	{I_ADC, A_AX, A_IMM16, 16},	{I_PUSH,A_SS,0,16},		{I_POP,A_SS,0,16},		{I_SBB, A_RM, A_REG, 8},	{I_SBB, A_RM, A_REG, 16},	{I_SBB, A_REG, A_RM, 8},	{I_SBB, A_REG, A_RM, 16},	{I_SBB, A_AL, A_IMM8, 8},	{I_SBB, A_AX, A_IMM16, 16},	{I_PUSH,A_DS,0,16},		{I_POP,A_DS,0,16},
	/*0x2*/	{I_AND, A_RM, A_REG, 8},	{I_AND, A_RM, A_REG, 16},	{I_AND, A_REG, A_RM, 8},	{I_AND, A_REG, A_RM, 16},	{I_AND, A_AL, A_IMM8, 8},	{I_AND, A_AX, A_IMM16, 16},	{P_ES,A_SS,0,16},		{I_DAA,0,0,16},			{I_SUB, A_RM, A_REG, 8},	{I_SUB, A_RM, A_REG, 16},	{I_SUB, A_REG, A_RM, 8},	{I_SUB, A_REG, A_RM, 16},	{I_SUB, A_AL, A_IMM8, 8},	{I_SUB, A_AX, A_IMM16, 16},	{P_CS,0,0,16},			{I_DAS,0,0,16},
	/*0x3*/	{I_XOR, A_RM, A_REG, 8},	{I_XOR, A_RM, A_REG, 16},	{I_XOR, A_REG, A_RM, 8},	{I_XOR, A_REG, A_RM, 16},	{I_XOR, A_AL, A_IMM8, 8},	{I_XOR, A_AX, A_IMM16, 16},	{P_SS,A_SS,0,16},		{I_AAA,0,0,16},			{I_CMP, A_RM, A_REG, 8},	{I_CMP, A_RM, A_REG, 16},	{I_CMP, A_REG, A_RM, 8},	{I_CMP, A_REG, A_RM, 16},	{I_CMP, A_AL, A_IMM8, 8},	{I_CMP, A_AX, A_IMM16, 16},	{P_DS,0,0,16},			{I_AAS,0,0,16},
	/*0x4*/ {I_INC,A_AX,0,16},			{I_INC,A_CX,0,16},			{I_INC,A_DX,0,16},			{I_INC,A_BX,0,16},			{I_INC,A_SP,0,16},			{I_INC,A_BP,0,16},			{I_INC,A_SI,0,16},		{I_INC,A_DI,0,16},		{I_DEC,A_AX,0,16},			{I_DEC,A_CX,0,16},			{I_DEC,A_DX,0,16},			{I_DEC,A_BX,0,16},			{I_DEC,A_SP,0,16},			{I_DEC,A_BP,0,16},			{I_DEC,A_SI,0,16},		{I_DEC,A_DI,0,16},
	/*0x5*/ {I_PUSH,A_AX,0,16},			{I_PUSH,A_CX,0,16},			{I_PUSH,A_DX,0,16},			{I_PUSH,A_BX,0,16},			{I_PUSH,A_SP,0,16},			{I_PUSH,A_BP,0,16},			{I_PUSH,A_SI,0,16},		{I_PUSH,A_DI,0,16},		{I_POP,A_AX,0,16},			{I_POP,A_CX,0,16},			{I_POP,A_DX,0,16},			{I_POP,A_BX,0,16},			{I_POP,A_SP,0,16},			{I_POP,A_BP,0,16},			{I_POP,A_SI,0,16},		{I_POP,A_DI,0,16},
	/*0x6*/ OP_INVALID,					OP_INVALID,					OP_INVALID,					OP_INVALID,					OP_INVALID,					OP_INVALID,					OP_INVALID,				OP_INVALID,				OP_INVALID,					OP_INVALID,					OP_INVALID,					OP_INVALID,					OP_INVALID,					OP_INVALID,					OP_INVALID,				OP_INVALID,
	/*0x7*/ {I_JO,A_JB,0,8},			{I_JNO,A_JB,0,8},			{I_JB,A_JB,0,8},			{I_JNB,A_JB,0,8},			{I_JZ,A_JB,0,8},			{I_JNZ,A_JB,0,8},			{I_JBE,A_JB,0,8},		{I_JA,A_JB,0,8},		{I_JS,A_JB,0,8},			{I_JNS,A_JB,0,8},			{I_JPE,A_JB,0,8},			{I_JPO,A_JB,0,8},			{I_JL,A_JB,0,8},			{I_JGE,A_JB,0,8},			{I_JLE,A_JB,0,8},		{I_JG,A_JB,0,8},
	/*0x8*/ {I_GRP1,A_RM,A_IMM8,8},		{I_GRP1,A_RM,A_IMM16,16},	{I_GRP1,A_RM,A_IMM8,8},		{I_GRP1,A_RM,A_IMM8,16},	{I_TEST,A_REG, A_RM,8},		{I_TEST,A_REG, A_RM,16},	{I_XCHG,A_REG, A_RM,8},	{I_XCHG,A_REG, A_RM,16},{I_MOV,A_RM,A_REG,8},		{I_MOV,A_RM,A_REG,16},		{I_MOV,A_REG,A_RM,8},		{I_MOV,A_REG,A_RM,16},		{I_MOV,A_RM,A_SEG,16},		{I_LEA,A_REG,A_MEM,16},		{I_MOV,A_SEG,A_RM,16},	{I_POP,A_RM,0,16},
	/*0x9*/ {I_NOP,A_CX,A_AX,16},		{I_XCHG,A_CX,A_AX,16},		{I_XCHG,A_DX,A_AX,16},		{I_XCHG,A_BX,A_AX,16},		{I_XCHG,A_SP,A_AX,16},		{I_XCHG,A_BP,A_AX,16},		{I_XCHG,A_SI,A_AX,16},	{I_XCHG,A_DI,A_AX,16},	{I_CBW,0,0,8},				{I_CWD,0,0,16},				{I_CALL,A_ADDR,0,16},		{I_WAIT,0,0,16},			{I_PUSHF,0,0,16},			{I_POPF,0,0,16},			{I_SAHF,0,0,16},		{I_LAHF,0,0,16},
	/*0xA*/ {I_MOV,A_AL, A_OFF8,8},		{I_MOV,A_AX, A_OFF16,16},	{I_MOV,A_OFF8, A_AL,8},		{I_MOV,A_OFF16, A_AX,16},	{I_MOVSB,0,0,8},			{I_MOVSW,0,0,16},			{I_CMPSB,0,0,8},		{I_CMPSW,0,0,16},		{I_TEST,A_AL,A_IMM8,8},		{I_TEST,A_AX,A_IMM16,16},	{I_STOSB,0,0,8},			{I_STOSW,0,0,16},			{I_LODSB,0,0,8},			{I_LODSW,0,0,16},			{I_SCASB,0,0,8},		{I_SCASW,0,0,16},
	/*0xB*/ {I_MOV,A_AL, A_IMM8, 8},	{I_MOV,A_CL, A_IMM8, 8},	{I_MOV,A_DL, A_IMM8, 8},	{I_MOV,A_BL, A_IMM8, 8},	{I_MOV,A_AH, A_IMM8, 8},	{I_MOV,A_CH, A_IMM8, 8},	{I_MOV,A_DH, A_IMM8, 8},{I_MOV,A_BH, A_IMM8, 8},{I_MOV,A_AX,A_IMM16,16},	{I_MOV,A_CX,A_IMM16,16},	{I_MOV,A_DX,A_IMM16,16},	{I_MOV,A_BX,A_IMM16,16},	{I_MOV,A_SP,A_IMM16,16},	{I_MOV,A_BP,A_IMM16,16},	{I_MOV,A_SI,A_IMM16,16},{I_MOV,A_DI,A_IMM16,16},
	/*0xC*/ OP_INVALID,					OP_INVALID,					{I_RET,A_IMM16,0,16},		{I_RET,0,0,16},				{I_LES,A_REG,A_MEM,16},		{I_LDS,A_REG,A_MEM,16},		{I_MOV,A_RM,A_IMM8,8},	{I_MOV,A_RM,A_IMM16,16},OP_INVALID,					OP_INVALID,					{I_RETF,A_IMM16,0,16},		{I_RETF,0,0,16},			{I_INT,A_3,0,16},			{I_INT,A_IMM8,0,16},		{I_INTO,0,0,16},		{I_IRET,0,0,16},
	/*0xD*/	{I_GRP2,A_RM,A_1,8},		{I_GRP2,A_RM,A_1,16},		{I_GRP2,A_RM,A_CL,8},		{I_GRP2,A_RM,A_CL,16},		{I_AAM,A_OBYTE,0,8},		{I_AAD,A_OBYTE,0,8},		OP_INVALID,				{I_XLATB,0,0,16},		OP_INVALID,					OP_INVALID,					OP_INVALID,					OP_INVALID,					OP_INVALID,					OP_INVALID,					OP_INVALID,				OP_INVALID,
	/*0xE*/	{I_LOOPNZ,A_JB,0,8},		{I_LOOPZ,A_JB,0,8},			{I_LOOP,A_JB,0,8},			{I_JCXZ,A_JB,0,8},			{I_IN,A_AL,A_IMM8,8},		{I_IN,A_AX,A_IMM8,16},		{I_OUT,A_IMM8,A_AL,8},	{I_OUT,A_IMM8,A_AX,16},	{I_CALL,A_JV,0,16},			{I_JMP,A_JV,0,16},			{I_JMP,A_ADDR,0,16},		{I_JMP,A_JB,0,8},			{I_IN,A_AL,A_DX,8},			{I_IN,A_AX,A_DX,16},		{I_OUT,A_DX,A_AL,8},	{I_OUT,A_DX,A_AX,16},
	/*0xF*/	{I_LOCK,0,0,8},				OP_INVALID,					{I_REPNZ,0,0,16},			{I_REPZ,0,0,16},			{I_HLT,0,0,16},				{I_CMC,0,0,16},				{I_GRP3a,A_RM,0,8},		{I_GRP3b,A_RM,0,16},	{I_CLC,0,0,16},				{I_STC,0,0,16},				{I_CLI,0,0,16},				{I_STI,0,0,16},				{I_CLD,0,0,16},				{I_STD,0,0,16},				{I_GRP4,A_RM,0,8},		{I_GRP4,A_RM,0,16}
};


static inline byte* low(word* word) {
	byte* arr = (byte*)word;
	return &arr[LO_IDX];
}
static inline byte* high(word* word) {
	byte* arr = (byte*)word;
	return &arr[HI_IDX];
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


static byte* get_mapped_memory(address addr) {

	address_physical phys = get_physical_addr(R_DS, addr);
	return map_byte(phys);
}

static byte next_instr() {
	return *map_byte(get_physical_addr(R_CS, registers[R_IP]++));
}

static word next_instr_word() {
	word instr = next_instr();
	instr |= next_instr() << 8;
	return instr;
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






inline address get_modrm_memory_addr(byte rm) {
	switch (rm & 0b111)
	{
	case 0b000: return (registers[R_B] + registers[R_SI]);
	case 0b001: return (registers[R_B] + registers[R_DI]);
	case 0b010: return (registers[R_BP] + registers[R_SI]);
	case 0b011: return (registers[R_BP] + registers[R_DI]);
	case 0b100: return (registers[R_SI]);
	case 0b101: return (registers[R_DI]);
	case 0b110: return (registers[R_BP]);
	case 0b111: return (registers[R_B]);
	}

	return NULL;
}

struct decoded_modrm {
	void* reg;
	void* rm;
	int decoded;
	address rm_addr;
	byte grp; // top two bits 
};


void decode_modrm(byte modrm, int bits, struct decoded_modrm* mod_str) {

	byte reg_idx = (modrm >> 3) & 0b111;
	byte rm_idx = modrm & 0b111;



	mod_str->decoded = 1;
	mod_str->grp = (modrm >> 6) & 0b11;
	 
	switch (mod_str->grp)
	{
	case 0b00:
		mod_str->reg = &registers[reg_idx];

		if (rm_idx == 0b110) { //special case
			mod_str->rm = get_mapped_memory(mod_str->rm_addr = next_instr_word());
		}
		else {
			mod_str->rm = get_mapped_memory(mod_str->rm_addr = get_modrm_memory_addr(rm_idx));
		}
		return;
	case 0b01:
		mod_str->reg = &registers[reg_idx];
		mod_str->rm = get_mapped_memory(mod_str->rm_addr = (get_modrm_memory_addr(rm_idx) + next_instr()));
		return;
	case 0b10:
		mod_str->reg = &registers[reg_idx];
		mod_str->rm = get_mapped_memory(mod_str->rm_addr = (get_modrm_memory_addr(rm_idx) + next_instr_word()));
		return;
	case 0b11: //register register mode
		if (bits == 16) {
			mod_str->reg = &registers[reg_idx];
			mod_str->rm = &registers[rm_idx];
		}
		else { //treat anything else as byte
			mod_str->reg = (reg_idx & 0b100) ? high(&registers[reg_idx & 0b11]) : low(&registers[reg_idx & 0b11]);
			mod_str->rm = (rm_idx & 0b100) ? high(&registers[rm_idx & 0b11]) : low(&registers[rm_idx & 0b11]);
			mod_str->rm_addr = -1;
		}
		return;
	}



}





//this should never be overwritten, and only used as SRC
uint32_t one = 1;
uint32_t three = 3;


word address_bus;

operand get_addressing_single(byte mode, byte bits, struct decoded_modrm* modrm) {
	
	operand op;
	op.ptr = NULL;
	op.addr = -1;
	switch (mode)
	{
	case A_NONE: return (operand){NULL,0};
	case A_RM:
		if (!modrm->decoded) decode_modrm(next_instr(), bits, modrm);
		op.ptr = modrm->rm;
		op.addr = modrm->rm_addr;
		break;
	case A_REG:
		if (!modrm->decoded) decode_modrm(next_instr(), bits, modrm);
		op.ptr = modrm->reg;
		break;

	case A_SEG:
		if (!modrm->decoded) decode_modrm(next_instr(), bits, modrm);
		//transform into seg register

		uintptr_t seg_off = (uintptr_t)modrm->reg - (uintptr_t)&registers[R_A]; //get offset

		seg_off = seg_off % (4 * sizeof(reg)); //transform to correct range
		seg_off += (uintptr_t)&registers[R_ES]; //move to start of segment registers

		op.ptr = (void*)seg_off;
		break;

	case A_MEM: 
		if (!modrm->decoded) {
			decode_modrm(next_instr(), bits, modrm);
		}
		assert(modrm->grp != 0b11);
		op.ptr = modrm->rm;
		op.addr = modrm->rm_addr;
		break;

	case A_OBYTE:
	case A_JB:
	case A_IMM8: address_bus = *map_byte(get_physical_addr(R_CS, registers[R_IP]++)); op.ptr = &address_bus; break;
	case A_JV:
	case A_IMM16: address_bus = *map_word(get_physical_addr(R_CS, registers[R_IP]++)); op.ptr = &address_bus; break;
	case A_ADDR:
		registers[R_IP] += 4; //move 4 bytes
		op.ptr = map_byte(get_physical_addr(R_CS, registers[R_IP] - 4));
		break;
	case A_OFF8:
		address_bus = *map_byte(get_physical_addr(R_DS, next_instr_word()));
		op.ptr = &address_bus;
		break;
	case A_OFF16:
		address_bus = *map_word(get_physical_addr(R_DS, next_instr_word()));
		op.ptr = &address_bus;
		break;
		

	case A_1: op.ptr = &one; break;
	case A_3: op.ptr = &three; break;


	case A_AX: op.ptr = AX; break;
	case A_CX: op.ptr = CX; break;
	case A_DX: op.ptr = DX; break;
	case A_BX: op.ptr = BX; break;
	case A_SP: op.ptr = &registers[R_SP]; break;
	case A_BP: op.ptr = &registers[R_BP]; break;
	case A_SI: op.ptr = &registers[R_SI]; break;
	case A_DI: op.ptr = &registers[R_DI]; break;

	case A_AL: op.ptr = AL; break;
	case A_CL: op.ptr = CL; break;
	case A_DL: op.ptr = DL; break;
	case A_BL: op.ptr = BL; break;
	case A_AH: op.ptr = BH; break;
	case A_CH: op.ptr = CH; break;
	case A_DH: op.ptr = DH; break;
	case A_BH: op.ptr = BH; break;

	case A_ES: op.ptr = &registers[R_ES]; break;
	case A_CS: op.ptr = &registers[R_CS]; break;
	case A_SS: op.ptr = &registers[R_SS]; break;
	case A_DS: op.ptr = &registers[R_DS]; break;


	//case A_JB:
	//	;
	//	byte jb = next_instr();
	//	op.ptr = map_word(get_physical_addr(R_DS, jb + registers[R_IP]));
	//	break;
	//case A_JV:
	//	;
	//	byte jv = next_instr_word();
	//	op.ptr = map_word(get_physical_addr(R_DS, jv + registers[R_IP]));
	//	break;
	default:

		printf("ERROR: UNKNOWN ADDRESSING MODE : %d\n", mode);
		return (operand){NULL,0};
	}

	return op;
}

void get_addressing(instr_t instr, operand* dst, operand* src) {

	struct decoded_modrm modrm;
	modrm.decoded = 0;

	*dst = get_addressing_single(instr.dst, instr.bits, &modrm);
	if (instr.src != A_NONE) {
		*src = get_addressing_single(instr.src, instr.bits, &modrm);
	}
}


byte read_port_byte(uint16_t port) {
	return 0;
}
word read_port_word(uint16_t port) {
	return 0;
}

void write_port_byte(uint16_t port, byte v) {
	return;
}
void write_port_word(uint16_t port, word v) {
	return;
}

void jump8(byte offset) {
	sbyte v = (sbyte)offset;
	registers[R_IP] += v;
}


//returns W_FAILURE on error
int execute(instr_t instr, operand pdst, operand psrc) {

	byte bits = instr.bits;
	int64_t res;
	switch (instr.opcode)
	{
	case I_AAA:
	{
		byte mask = *AL & 0b1111;
		if ((mask <= 9) && (get_flag(FLAG_AUX_CARRY) == 0)) {
			//skip next step
		}
		else if (((mask >= 'A') && (mask <= 'F')) || (get_flag(FLAG_AUX_CARRY) == 1)) {
			*AL += 6;
			++(*AH);
			set_flag(FLAG_AUX_CARRY, 1);
			set_flag(FLAG_CARRY, 1);
		}
		*AL &= 0b1111;
	}
	break;
	case I_AAD:
	{
		*AL += *AH * 0x0A;
		*AH = 0;
		word res = *AL;
		SET_PZS(res,8)
	}
	break;

	case I_AAM:
	{
		*AH = *AL / 0x0A;
		*AL = *AL % 0x0A;
		word res = *AL;
		SET_PZS(res,8)
	}
	break;
	case I_AAS:
	{
		byte mask = *AL & 0b1111;
		if ((mask <= 9) && (get_flag(FLAG_AUX_CARRY) == 0)) {
			*AL = mask;
			set_flag(FLAG_CARRY, get_flag(FLAG_AUX_CARRY));
			break;
		}
		else if (((mask >= 'A') && (mask <= 'F')) || (get_flag(FLAG_AUX_CARRY) == 1)) {
			*AL -= 6;
			--(*AH);
			set_flag(FLAG_AUX_CARRY, 1);
			set_flag(FLAG_CARRY, 1);
		}
		*AL &= 0b1111;
	}
	break;

	case I_ADC:
		ARITH_OP(+, 1, SET_OCAPZS(dst, src, res, 1, bits))
		break;
	case I_ADD:
		ARITH_OP(+ get_flag(FLAG_CARRY) +, 1, SET_OCAPZS(dst, src, res, 1, bits))
		break;
	case I_AND:
		ARITH_OP(&, 1, CLR_OC SET_PZS(res,bits))
		break;

	case I_CALL:

		switch (instr.src)
		{
		case A_ADDR:
			push_stack(registers[R_CS]);
			push_stack(registers[R_IP]);
			registers[R_IP] = pdst.pbyte[0] | (pdst.pbyte[1] << 8);
			registers[R_CS] = pdst.pbyte[2] | (pdst.pbyte[3] << 8);
			break;
		case A_JV:
			push_stack(registers[R_IP]);
			registers[R_IP] = *pdst.pword;
			break;
		default:
			printf("INVALID CALL ADDRESS MODE: \'%d\'\n", instr.src);
		}

		break;
	case I_CBW:
		if ((*AL) & (1 << 7)) {
			*AH = 0xFF;
		}
		else {
			*AH = 0;
		}
		break;
	case I_CLC: set_flag(FLAG_CARRY, 0); break;
	case I_CLD: set_flag(FLAG_DIRECTION, 0); break;
	case I_CLI: set_flag(FLAG_INT_ENABLE, 0); break;
	case I_CMC: set_flag(FLAG_CARRY, !get_flag(FLAG_CARRY)); break;
	case I_CMP:
		ARITH_OP(-,0, SET_OCAPZS(dst, src, res, 0, bits))
		break;
	case I_CMPSB:
	{
		byte dst = *map_byte(get_physical_addr(R_DS, registers[R_DI]));
		byte src = *map_byte(get_physical_addr(R_DS, registers[R_SI]));
		res = src - dst;
		if (get_flag(FLAG_DIRECTION)) {
			registers[R_DI]--;
			registers[R_SI]--;
		}
		else {
			registers[R_DI]++;
			registers[R_SI]++;
		}
		SET_OCAPZS(dst, src, res, 0, 8)
	}
	break;
	case I_CMPSW:
	{
		word dst = *map_word(get_physical_addr(R_DS, registers[R_DI]));
		word src = *map_word(get_physical_addr(R_DS, registers[R_SI]));
		res = src - dst;
		if (get_flag(FLAG_DIRECTION)) {
			registers[R_DI] -= 2;
			registers[R_SI] -= 2;
		}
		else {
			registers[R_DI] += 2;
			registers[R_SI] += 2;
		}
		SET_OCAPZS(dst, src, res, 0, 16)
	}
	break;
	case I_CWD:
		if (((registers[R_A]) & (1 << 15))) {
			registers[R_D] = 0xFFFF;
		}
		else {
			registers[R_D] = 0;
		}
		break;
	case I_DAA:
	{
		byte mask = *AL & 0x1111;
		if ((mask >= 'A' && mask <= 'F') || get_flag(FLAG_AUX_CARRY)) {
			*AL += 0x06;
			set_flag(FLAG_AUX_CARRY, 1);
		}
		mask = (*AL >> 4) & 0b1111;
		if ((mask > 9) || get_flag(FLAG_CARRY)) {
			*AL += 0x60;
			set_flag(FLAG_CARRY, 1);
		}

		SET_PZS((*AL),8)
	}
	break;
	case I_DAS:
	{
		byte mask = *AL & 0x1111;
		if ((mask >= 'A' && mask <= 'F') || get_flag(FLAG_AUX_CARRY)) {
			*AL -= 0x06;
			set_flag(FLAG_AUX_CARRY, 1);
		}
		mask = (*AL >> 4) & 0b1111;
		if ((mask > 9) || get_flag(FLAG_CARRY)) {
			*AL -= 0x60;
			set_flag(FLAG_CARRY, 1);
		}

		SET_PZS((*AL),8)
	}
	break;
	case I_DEC:
		CUSTOM_OP(dst-1, 1, SET_PZS(res, bits) SET_AUX_CARRY(res, bits) set_flag(FLAG_OVERFLOW, res > dst););
	break;


	case I_DIV:
	{
		if (bits == 16) {
			word ax = *AX;
			word result = ax / *pdst.pbyte;

			if (result > 0xFF) {
				//interrupt(0);
			}
			else {
				*AL = result & 0xFF;
				*AH = (ax % *pdst.pbyte) & 0xFF;
			}
		}
		else if (bits == 32) {
			dword dxax = (*DX << 16) | *AX;
			dword result = dxax / *pdst.pword;

			if (result > 0xFFFF) {
				//interrupt(0);
			}
			else {
				*AX = result & 0xFFFF;
				*DX = (dxax % *pdst.pword) & 0xFFFF;
			}
		}
		else {
			PANIC("Invalid bit count");
		}

	}
	break;
	//case I_ESC:

	case I_HLT: halted = 1; break;

	case I_IDIV:
	{
		if (bits == 16) {
			sword ax = (sword)*AX;
			sword result = ax / ((sbyte)*pdst.pbyte);

			if (result > 0x7F) {
				//interrupt(0);
			}
			else {
				*AL = ((word)result) & 0xFF;
				*AH = ((word)(ax % *pdst.pbyte)) & 0xFF;
			}
		}
		else if (bits == 32) {
			sdword dxax = (sdword)((*DX << 16) | *AX);
			sdword result = dxax / ((sword)*pdst.pword);

			if (result > 0x7FFF) {
				//interrupt(0);
			}
			else {
				*AX = ((dword)result) & 0xFFFF;
				*DX = ((dword)(dxax % *pdst.pword)) & 0xFFFF;
			}
		}
		else {
			PANIC("Invalid bit count");
		}

	}
	break;
	case I_IMUL: {
		if (bits == 8) {
			sbyte dst = *((sbyte*)pdst.pbyte);
			sbyte src = *((sbyte*)psrc.pbyte);
			res = dst * src;

			*AL = res & 0xFF;
			*AH = (res >> 8) & 0xFF;

			if (*AH == 0 || *AH == 0xFF) {
				set_flag(FLAG_OVERFLOW, 0);
				set_flag(FLAG_CARRY, 0);
			}
			else {
				set_flag(FLAG_OVERFLOW, 1);
				set_flag(FLAG_CARRY, 1);
			}
		}
		else if (bits == 16) {
			sword dst = *((sword*)pdst.pword);
			sword src = *((sword*)psrc.pword);
			res = dst * src;

			*AX = res & 0xFFFF;
			*DX = (res >> 16) & 0xFFFF;

			if (*DX == 0 || *DX == 0xFFFF) {
				set_flag(FLAG_OVERFLOW, 0);
				set_flag(FLAG_CARRY, 0);
			}
			else {
				set_flag(FLAG_OVERFLOW, 1);
				set_flag(FLAG_CARRY, 1);
			}
		}
		else {
			PANIC("Invalid bit count");
		}
	}

	break;
	case I_IN:
		if (bits == 8) {
			*AL = read_port_byte(*psrc.pword);
		}
		else if (bits == 16) {
			*AX = read_port_word(*psrc.pword);
		}
		else {
			PANIC("Invalid bit count");
		}
	break;
	case I_INC:
		CUSTOM_OP(dst+1, 1, SET_PZS(res, bits) SET_AUX_CARRY(res, bits) set_flag(FLAG_OVERFLOW, res < dst););
		break;
	case I_INT:
		push_stack(registers[R_FLAGS]);
		set_flag(FLAG_INT_ENABLE, 0);
		set_flag(FLAG_TRAP, 0);
		push_stack(registers[R_CS]);

		int val = (4 * (*pdst.pbyte)) & 0b111;
		registers[R_CS] = val + 2;
		push_stack(registers[R_IP]);
		registers[R_IP] = val;

		break;
	case I_INTO:
		if (get_flag(FLAG_OVERFLOW)) {
			push_stack(registers[R_FLAGS]);
			set_flag(FLAG_INT_ENABLE, 0);
			set_flag(FLAG_TRAP, 0);
			push_stack(registers[R_CS]);
			registers[R_CS] = 0x12;
			push_stack(registers[R_IP]);
			registers[R_IP] = 0x10;
		}
		break;
	case I_IRET:
		registers[R_IP] = pop_stack();
		registers[R_CS] = pop_stack();
		registers[R_FLAGS] = pop_stack();
		break;
	case I_JA: if ((get_flag(FLAG_CARRY) == 0) && (get_flag(FLAG_ZERO) == 0)) jump8(*pdst.pbyte); break;
	case I_JNB: if ((get_flag(FLAG_CARRY) == 0)) jump8(*pdst.pbyte); break;
	case I_JB: if ((get_flag(FLAG_CARRY) == 1)) jump8(*pdst.pbyte); break;
	case I_JBE: if ((get_flag(FLAG_CARRY) == 1) || (get_flag(FLAG_ZERO) == 1)) jump8(*pdst.pbyte); break;
	case I_JCXZ: if ((*CX == 0)) jump8(*pdst.pbyte); break;
	case I_JZ: if ((get_flag(FLAG_ZERO) == 1)) jump8(*pdst.pbyte); break;
	case I_JG: if ((get_flag(FLAG_ZERO) == 0) && (get_flag(FLAG_SIGN) == get_flag(FLAG_OVERFLOW))) jump8(*pdst.pbyte); break;
	case I_JGE: if ((get_flag(FLAG_SIGN) == get_flag(FLAG_OVERFLOW))) jump8(*pdst.pbyte); break;
	case I_JL: if ((get_flag(FLAG_SIGN) != get_flag(FLAG_OVERFLOW))) jump8(*pdst.pbyte); break;
	case I_JLE: if ((get_flag(FLAG_ZERO) == 1) || (get_flag(FLAG_SIGN) != get_flag(FLAG_OVERFLOW))) jump8(*pdst.pbyte); break;


	case I_JMP: {
		switch (instr.dst)
		{
		case A_JB: jump8(*pdst.pbyte); break;
		case A_ADDR:
			registers[R_IP] = pdst.pbyte[0] | (pdst.pbyte[1] << 8);
			registers[R_CS] = pdst.pbyte[2] | (pdst.pbyte[3] << 8);
			break;
		case A_JV:
		case A_RM:
			registers[R_IP] = *pdst.pword;
			break;
		case A_MEM:
			registers[R_IP] = *pdst.pword;
			registers[R_CS] = *(pdst.pword + 1); //TODO - validate this doesnt go out of bounds
			break;
		default:
			break;
		}
	}
	break;
	case I_JNZ: if ((get_flag(FLAG_ZERO) == 0)) jump8(*pdst.pbyte); break;
	case I_JNO: if ((get_flag(FLAG_OVERFLOW) == 0)) jump8(*pdst.pbyte); break;
	case I_JPO: if ((get_flag(FLAG_PARITY) == 0)) jump8(*pdst.pbyte); break;
	case I_JNS: if ((get_flag(FLAG_SIGN) == 0)) jump8(*pdst.pbyte); break;
	case I_JO: if ((get_flag(FLAG_OVERFLOW) == 1)) jump8(*pdst.pbyte); break;
	case I_JP: if ((get_flag(FLAG_PARITY) == 1)) jump8(*pdst.pbyte); break;
	case I_JS: if ((get_flag(FLAG_SIGN) == 1)) jump8(*pdst.pbyte); break;

	case I_LAHF:
		*AH = registers[R_FLAGS] & 0xFF;
		break;

	case I_LDS:
	{
		*pdst.pword = *psrc.pword;
		//uintptr_t offset = ((uintptr_t)psrc.pword) - ((uintptr_t)memory);
		//if (offset >= MEM_COUNT) PANIC("Address out of range");

		//TODO - make this better so that it supports things mapped in memory
		registers[R_DS] = *map_word(get_physical_addr(R_DS, psrc.addr));
	}
	break;
	case I_LEA:
		*pdst.pword = psrc.addr;
		break;
	case I_LES:
	{
		*pdst.pword = *psrc.pword;
		registers[R_ES] = *map_word(get_physical_addr(R_DS, psrc.addr));
	}
	break;

	case I_LOCK: PANIC("LOCK not implemented");


	case I_LODSB:
	{

		*AL = map_byte(get_physical_addr(R_DS, registers[R_SI]));

		if (get_flag(FLAG_DIRECTION)) {
			registers[R_SI]--;
		}
		else {
			registers[R_SI]++;
		}
	}
	break;
	case I_LODSW:
	{

		*AX = map_word(get_physical_addr(R_DS, registers[R_SI]));

		if (get_flag(FLAG_DIRECTION)) {
			registers[R_SI] -= 2;
		}
		else {
			registers[R_SI] += 2;
		}
	}
	break;

	case I_LOOP:
	{
		*CX -= 1;
		if (*CX != 0) {
			jump8(*pdst.pbyte);
		}

	}
	break;

	case I_LOOPZ:
	{
		*CX -= 1;
		if ((*CX != 0) && get_flag(FLAG_ZERO)) {
			jump8(*pdst.pbyte);
		}
	}
	break;
	case I_LOOPNZ:
	{
		*CX -= 1;
		if ((*CX != 0) && (get_flag(FLAG_ZERO) == 0)) {
			jump8(*pdst.pbyte);
		}
	}
	break;
	case I_MOV:
		if (bits == 8) {
			*pdst.pbyte = *psrc.pbyte;
			res = *pdst.pbyte;
		}
		else if (bits == 16) {
			*pdst.pword = *psrc.pword;
			res = *pdst.pword;
		}
		else {
			PANIC("Invalid bit count");
		}
		break;
	case I_MOVSB:
		*map_byte(get_physical_addr(R_DS, registers[R_DI])) = *map_byte(get_physical_addr(R_DS, registers[R_SI]));
		if (get_flag(FLAG_DIRECTION)) {
			registers[R_DI]--;
			registers[R_SI]--;
		}
		else {
			registers[R_DI]++;
			registers[R_SI]++;
		}
		break;
	case I_MOVSW:
		*map_word(get_physical_addr(R_DS, registers[R_DI])) = *map_word(get_physical_addr(R_DS, registers[R_SI]));
		if (get_flag(FLAG_DIRECTION)) {
			registers[R_DI]-=2;
			registers[R_SI]-=2;
		}
		else {
			registers[R_DI]+=2;
			registers[R_SI]+=2;
		}
		break;

	case I_MUL:
	{
		if (bits == 8) {
			byte dst = *pdst.pbyte;

			res = dst * (*AL);

			*AL = res & 0xFF;
			*AH = (res >> 8) & 0xFF;

			if (*AH == 0 || *AH == 0xFF) {
				set_flag(FLAG_OVERFLOW, 0);
				set_flag(FLAG_CARRY, 0);
			}
			else {
				set_flag(FLAG_OVERFLOW, 1);
				set_flag(FLAG_CARRY, 1);
			}

		}
		else if (bits == 16) {
			word dst = *pdst.pword;

			res = dst * (*AX);

			*AX = res & 0xFFFF;
			*DX = (res >> 16) & 0xFFFF;

			if (*DX == 0 || *DX == 0xFFFF) {
				set_flag(FLAG_OVERFLOW, 0);
				set_flag(FLAG_CARRY, 0);
			}
			else {
				set_flag(FLAG_OVERFLOW, 1);
				set_flag(FLAG_CARRY, 1);
			}
		}
		else {
			PANIC("Invalid bit count");
		}
	}
	break;
	case I_NEG:
		CUSTOM_OP(0 - dst, 1, SET_OCAPZS(dst,src,res,0,bits))
	break;
	case I_NOP:
		//do nothing
	break;

	case I_NOT:
		CUSTOM_OP(~(dst), 1)
		break;
	case I_OR:
		ARITH_OP(|,1, CLR_OC SET_PZS(res,bits))
		break;
	case I_OUT:
		if (bits == 8) {
			write_port_byte(*psrc.pword, *AL);
		}
		else if (bits == 16) {
			write_port_word(*psrc.pword, *AX);
		}
		else {
			PANIC("Invalid bit count");
		}
		break;
	case I_POP:
		*pdst.pword = pop_stack();
		break;
	case I_POPF:
		registers[R_FLAGS] = pop_stack();
		break;
	case I_PUSH:
		push_stack(*pdst.pword);
		break;
	case I_PUSHF:
		push_stack(registers[R_FLAGS]);
		break;
	case I_RCL:
		CUSTOM_OP((dst << (src % (bits + 1))) | (get_flag(FLAG_CARRY) << ((src - 1) % (bits + 1))) | dst >> ((bits + 1) - (src % (bits + 1))), 1, set_flag(FLAG_CARRY, SIGN_BIT(dst << ((src - 1) % (bits + 1)),bits));)
		break;
	case I_RCR:
		CUSTOM_OP((dst >> (src % (bits + 1))) | (get_flag(FLAG_CARRY) << (bits - (src % (bits + 1)))) | dst << ((bits + 1) - (src % (bits + 1))), 1, set_flag(FLAG_CARRY, (dst >> ((src-1) % (bits + 1)) & 1, bits));)
		break; 

	case I_REPZ: // z is 1
	case I_REPNZ: // z is 0
	{

		instr_t ni = base8086map[next_instr()];
		int z = 0;
		int zbool = instr.opcode == I_REPZ;
		switch (ni.opcode)
		{
		case I_MOVSB:
		case I_MOVSW:
		case I_LODSB:
		case I_LODSW:
		case I_STOSB:
		case I_STOSW:
			z = 0; //dont care
			break;
		case I_CMPSB:
		case I_CMPSW:
		case I_SCASB:
		case I_SCASW:
			z = 1;
			break;
		default:
			PANIC("Illegal instruction for REP")
			break;
		}
		operand nidst;
		operand nisrc;
		get_addressing(ni, &nidst, &nisrc);

		while (*CX) {
			execute(ni, nidst, nisrc);
			*CX -= 1;
			if (z) {
				if (get_flag(FLAG_ZERO) == zbool) {
					break;
				}
			}
		}
	}
	break;

	case I_RETF:
		registers[R_IP] = pop_stack();
		registers[R_CS] = pop_stack();
		if (instr.dst == A_IMM16) {
			registers[R_SP] += *pdst.pword;
		}
		break;
	case I_RET:
		registers[R_IP] = pop_stack();
		if (instr.dst == A_IMM16) {
			registers[R_SP] += *pdst.pword;
		}
		break;

		break;


	case I_GRP1: {

		byte modrm = *map_byte(get_physical_addr(R_CS, registers[R_IP] - 1)); //retrieve the modrm byte from the last ip

		instr_t in = instr;
		switch ((modrm >> 3) & 0b111)
		{
		case 0: in.opcode = I_ADD; break;
		case 1: in.opcode = I_OR; break;
		case 2: in.opcode = I_ADC; break;
		case 3: in.opcode = I_SBB; break;
		case 4: in.opcode = I_AND; break;
		case 5: in.opcode = I_SUB; break;
		case 6: in.opcode = I_XOR; break;
		case 7: in.opcode = I_CMP; break;
		}
		return execute(in, pdst, psrc);
	}
	case I_GRP2: {

		byte modrm = *map_byte(get_physical_addr(R_CS, registers[R_IP] - 1)); //retrieve the modrm byte from the last ip

		instr_t in = instr;
		switch ((modrm >> 3) & 0b111)
		{
		case 0: in.opcode = I_ROL; break;
		case 1: in.opcode = I_ROR; break;
		case 2: in.opcode = I_RCL; break;
		case 3: in.opcode = I_RCR; break;
		case 4: in.opcode = I_SHL; break;
		case 5: in.opcode = I_SHR; break;
		case 6: in.opcode = I_ERROR; break;
		case 7: in.opcode = I_SAR; break;
		}
		return execute(in, pdst, psrc);
	}
	case I_GRP3a: {
		byte modrm = *map_byte(get_physical_addr(R_CS, registers[R_IP] - 1)); //retrieve the modrm byte from the last ip

		instr_t in;
		switch ((modrm >> 3) & 0b111)
		{
		case 0: in = (instr_t){ I_TEST,A_RM,A_IMM8,8 }; psrc.ptr = map_byte(get_physical_addr(R_CS, registers[R_IP]++)); break;//TESTB(cpu, ops); break;
		case 1: in = (instr_t){ I_ERROR,0,0,8 }; break;//INV_OP(cpu, ops); break;
		case 2: in = (instr_t){ I_NOT,A_RM,0,8 }; break;//NOTB(cpu, ops); break;
		case 3: in = (instr_t){ I_NEG,A_RM,0,8 }; break;//NEGB(cpu, ops); break;
		case 4: in = (instr_t){ I_MUL,A_RM,0,8 }; break;//MULB(cpu, ops); break;
		case 5: in = (instr_t){ I_IMUL,A_RM,0,8 }; break;//IMULB(cpu, ops); break;
		case 6: in = (instr_t){ I_DIV,A_RM,0,16 }; break;//DIVW(cpu, ops); break;
		case 7: in = (instr_t){ I_IDIV,A_RM,0,16 }; break;//IDIVW(cpu, ops); break;
		}
		return execute(in, pdst, psrc);
	}
	case I_GRP3b: {
		byte modrm = *map_byte(get_physical_addr(R_CS, registers[R_IP] - 1)); //retrieve the modrm byte from the last ip

		instr_t in;
		switch ((modrm >> 3) & 0b111)
		{
		case 0: in = (instr_t){ I_TEST,A_RM,A_IMM16,16 }; psrc.ptr = map_word(get_physical_addr(R_CS, registers[R_IP]++)); break;//TESTB(cpu, ops); break;
		case 1: in = (instr_t){ I_ERROR,0,0,16 }; break;//INV_OP(cpu, ops); break;
		case 2: in = (instr_t){ I_NOT,A_RM,0,16 }; break;//NOTB(cpu, ops); break;
		case 3: in = (instr_t){ I_NEG,A_RM,0,16 }; break;//NEGB(cpu, ops); break;
		case 4: in = (instr_t){ I_MUL,A_RM,0,16 }; break;//MULB(cpu, ops); break;
		case 5: in = (instr_t){ I_IMUL,A_RM,0,16 }; break;//IMULB(cpu, ops); break;
		case 6: in = (instr_t){ I_DIV,A_RM,0,32 }; break;//DIVW(cpu, ops); break;
		case 7: in = (instr_t){ I_IDIV,A_RM,0,32 }; break;//IDIVW(cpu, ops); break;
		}
		return execute(in, pdst, psrc);
	}

	case I_GRP4: {

		byte modrm = *map_byte(get_physical_addr(R_CS, registers[R_IP] - 1)); //retrieve the modrm byte from the last ip

		instr_t in = instr;
		switch ((modrm >> 3) & 0b111)
		{
		case 0: in.opcode = I_INC; break;
		case 1: in.opcode = I_DEC; break;
		case 2: in.opcode = I_ERROR; break;
		case 3: in.opcode = I_ERROR; break;
		case 4: in.opcode = I_ERROR; break;
		case 5: in.opcode = I_ERROR; break;
		case 6: in.opcode = I_ERROR; break;
		case 7: in.opcode = I_ERROR; break;
		}
		return execute(in, pdst, psrc);
	}
	//GRP5 not implmented yet due to weird instructions

	case I_ERROR:
		PANIC("Invalid instruction")
	default:
		printf("ERROR: UNKNOWN OPCODE \'%d\'\n", instr.opcode);
		return E_FAILURE;
	}

#ifdef LOG_INSTRS
	printf("(%04X) %s\n",registers[R_IP], opcode_strings[instr.opcode]);
#endif // LOG_INSTRS

	return 0;
}

int next() {
	byte instr_raw = next_instr();

	instr_t instr = base8086map[instr_raw];

	operand pdst;
	operand psrc;
	get_addressing(instr, &pdst.ptr, &psrc.ptr);
	return execute(instr, pdst, psrc);
}

void run() {


	while (next() != E_FAILURE)
	{
	}
}
