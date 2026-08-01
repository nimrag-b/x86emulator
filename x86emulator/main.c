#include <stdio.h>
#include <stdint.h>
#include "emulator.h"

reg tregisters[R_COUNT];

static inline byte* low(word* word) {
	byte* arr = (byte*)word;
	return &arr[LO_IDX];
}
static inline byte* high(word* word) {
	byte* arr = (byte*)word;
	return &arr[HI_IDX];
}

int main() {

	printf("Hello World!\n");

	uint16_t endian_test = 1;

	uint8_t* arr = (uint8_t*)(&endian_test);


	if (arr[0] == 1) { //little endian
		printf("little endian\n");
	}
	else if (arr[1] == 1) { //big endian
		printf("big endian\n");
	}
	else { //error
		printf("no endian\n");
	}


	run();

	return 0;
}