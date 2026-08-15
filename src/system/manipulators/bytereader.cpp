#include "pch.h"
#include "bytereader.h"

char				bytereader::read_int8() {
	buffer_position++;
	return *(buffer_pointer + buffer_position - 1);
}
unsigned char		bytereader::read_uint8() {
	buffer_position++;
	return *(buffer_pointer + buffer_position - 1);
}
short				bytereader::read_int16() {
	buffer_position += 2;
	return *(short*)(buffer_pointer + buffer_position - 2);
}
unsigned short		bytereader::read_uint16() {
	buffer_position += 2;
	return *(unsigned short*)(buffer_pointer + buffer_position - 2);
}
int					bytereader::read_int32() {
	buffer_position += 4;
	return *(int*)(buffer_pointer + buffer_position - 4);
}
unsigned int		bytereader::read_uint32() {
	buffer_position += 4;
	return *(unsigned int*)(buffer_pointer + buffer_position - 4);
}
float				bytereader::read_float() {
	buffer_position += 4;
	return *(float*)(buffer_pointer + buffer_position - 4);
}
__int64				bytereader::read_int64() {
	buffer_position += 8;
	return *(__int64*)(buffer_pointer + buffer_position - 8);
}
unsigned __int64	bytereader::read_uint64() {
	buffer_position += 8;
	return *(unsigned __int64*)(buffer_pointer + buffer_position - 8);
}
double				bytereader::read_double() {
	buffer_position += 8;
	return *(double*)(buffer_pointer + buffer_position - 8);
}
void*				bytereader::read_text() {
	size_t strLength = ccslenf((char*)(buffer_pointer + buffer_position));
	void* retString = memalloc(strLength + 1);
	ccpy(retString, buffer_pointer + buffer_position, strLength);
	*((char*)retString + strLength) = 0;
	buffer_position += strLength + 1;
	return retString;
}
void*				bytereader::read_buffer(__int64 size) {
	buffer_position += size;
	return buffer_pointer + buffer_position - size;
}

bytereader::bytereader(void* pointer, __int64 size, __int64 position) {
	buffer_pointer = (unsigned char*)pointer;
	buffer_size = size;
	buffer_position = position;
}