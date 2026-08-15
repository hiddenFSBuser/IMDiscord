#include "pch.h"
#include "bytewriter.h"

void bytewriter::write_int8(char value) {
	*(buffer_pointer + buffer_position) = value;
	buffer_position++;
}
void bytewriter::write_uint8(unsigned char value) {
	*(buffer_pointer + buffer_position) = value;
	buffer_position++;
}
void bytewriter::write_int16(short value) {
	*(short*)(buffer_pointer + buffer_position) = value;
	buffer_position += 2;
}
void bytewriter::write_uint16(unsigned short value) {
	*(unsigned short*)(buffer_pointer + buffer_position) = value;
	buffer_position += 2;
}
void bytewriter::write_int32(int value) {
	*(int*)(buffer_pointer + buffer_position) = value;
	buffer_position += 4;
}
void bytewriter::write_uint32(unsigned int value) {
	*(unsigned int*)(buffer_pointer + buffer_position) = value;
	buffer_position += 4;
}
void bytewriter::write_float(float value) {
	*(float*)(buffer_pointer + buffer_position) = value;
	buffer_position += 4;
}
void bytewriter::write_int64(__int64 value) {
	*(__int64*)(buffer_pointer + buffer_position) = value;
	buffer_position += 8;
}
void bytewriter::write_uint64(unsigned __int64 value) {
	*(unsigned __int64*)(buffer_pointer + buffer_position) = value;
	buffer_position += 8;
}
void bytewriter::write_double(double value) {
	*(double*)(buffer_pointer + buffer_position) = value;
	buffer_position += 8;
}
void bytewriter::write_text(void* value) {
	size_t stringSize = ccslenf((char*)value);
	ccpy(buffer_pointer + buffer_position, value, stringSize);
	*(buffer_pointer + buffer_position + stringSize) = 0;
	buffer_position += stringSize + 1;
}
void bytewriter::write_buffer(void* pointer, __int64 size) {
	ccpy(buffer_pointer + buffer_position, pointer, size);
	buffer_position += size;
}

void bytewriter::round_data(size_t roundDivider) {
	if (buffer_position % roundDivider != 0) {
		int padding = roundDivider - (buffer_position % roundDivider);
		for (int i = 0; i < padding; i++)
			write_uint8(0);
	}
}

void bytewriter::resize_buffer(__int64 size) {
	void* newBuffer = memalloc(size);
	ccpy(newBuffer, buffer_pointer, buffer_position);
	memfree(buffer_pointer);
	buffer_pointer = (unsigned char*)newBuffer;
}

bytewriter::bytewriter() {
	buffer_pointer = 0;
	buffer_size = 0;
	buffer_position = 0;
}

bytewriter::bytewriter(__int64 size) {
	buffer_pointer = (unsigned char*)memalloc(size);
	buffer_size = size;
	buffer_position = 0;
}

bytewriter::bytewriter(void* pointer, __int64 size, __int64 position) {
	buffer_pointer = (unsigned char*)pointer;
	buffer_size = size;
	buffer_position = position;
}