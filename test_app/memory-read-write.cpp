#include <iostream>
#include "../instrumentation_dummies/approx.h"
//#include "immintrin.h"

using namespace ApproxSS;

#define SIZE 20000

void copyArray(uint16_t* destination, uint16_t* source) {
	for (int i = 0; i < SIZE; i++) {
		destination[i] = source[i];
		//next_period();
	}
}

void printArray(uint16_t* a) {
	for (int i = 0; i < SIZE/2; ++i) {
		std::cout << a[i] << " ";
		//a[i] = 32000;
		//next_period();
	}


	for (int i = SIZE/2+1; i < SIZE; ++i) {
		std::cout << a[i] << " ";
		//a[i] = 1000;
		//next_period();
	}
	std::cout << std::endl << std::endl;
}

void setArray(uint16_t* a, uint16_t value) {
	for (int i = 0; i < SIZE; i++) {
		a[i] = value;
		//next_period();
	}
}

int main() {
	uint16_t *begin_array, *array_end;
	begin_array = (uint16_t*) malloc(sizeof(uint16_t)*SIZE);
	array_end = begin_array + SIZE;
	add_approx(begin_array, array_end, 1, 1, sizeof(uint16_t));
	
	std::cout << std::string(20, '*') << std::endl;
	std::cout << std::endl << "INICIALIZANDO VETOR DE " << SIZE << " CASAS:" << std::endl;
	setArray(begin_array, 0);
	printArray(begin_array);
	
	start_level();
	std::cout << std::string(20, '*') << std::endl;
	std::cout << "LEITURA APROXIMADA" << std::endl;
	printArray(begin_array);
	std::cout << std::endl;
	end_level();

	std::cout << std::string(20, '*') << std::endl;
	std::cout << std::endl << "LEITURA PRECISA APÓS APROXIMADA" << std::endl;
	printArray(begin_array);

	start_level();
	std::cout << std::string(20, '*') << std::endl;
	std::cout << "ESCRITA APROXIMADA" << std::endl;
	setArray(begin_array, 0);
	end_level();
	printArray(begin_array);

	start_level();
	std::cout << std::string(20, '*') << std::endl;
	std::cout << "ESCRITA APROXIMADA + LEITURA APROXIMADA" << std::endl;
	setArray(begin_array, 0);
	printArray(begin_array);
	end_level();

	setArray(begin_array, 0);
	uint16_t* secondArray = new uint16_t[SIZE];
	
	/*start_level();
	next_period();
	next_period();
	std::cout << std::string(20, '*') << std::endl;
	std::cout << "LEITURA APROXIMADA COM INTERMADIARIO" << std::endl;
	copyArray(secondArray, begin_array);
	printArray(secondArray);
	end_level();*/

	remove_approx(begin_array, array_end);
	disable_access_instrumentation();

	std::cout << std::string(20, '*') << std::endl;
	std::cout << "LEITURA PRECISA" << std::endl;
	printArray(begin_array);

	std::cout << std::endl << "FINALIZOU O TEST_APP" << std::endl << std::endl;
	
	//AVX2
	//uint16_t f[8] __attribute__ ((aligned(32))) ={1,2,0,3, 5,5,10,11};
	/*__m256i v = _mm256_load_epu16((__m256i*) &f[0]);
	v = _mm256_add_epu16(v, v);
	_mm256_store_epu16((__m256i*) &f[0], v);
	std::cout << "AVX2" << std::endl;
	for (int i = 0; i < 8; ++i) {
		std::cout << f[i] << " ";
	}*/
	free(begin_array);
	std::cout << std::endl;
}
