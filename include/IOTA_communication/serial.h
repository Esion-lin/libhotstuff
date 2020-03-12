#include <string.h>
#include <stdint.h>
/**
*from stream to id of hash and milestone hash
*id_length:16, hash_length:256
**/
bool byte_to_dic(unsigned int &seq, uint8_t * hash, char * input, int input_size);