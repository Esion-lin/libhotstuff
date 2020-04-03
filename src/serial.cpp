#include "IOTA_communication/serial.h"
bool byte_to_dic(unsigned int &seq, uint8_t * hash, char * input, int input_size){
	if(input_size != 164)
		return false;
	seq = input[0] * 256 + input[1];
	for(int i =0 ; i < 162; i++){
		hash[i] = input[i+2];
	}
	return true;
}