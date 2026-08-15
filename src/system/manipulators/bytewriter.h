#pragma once
class bytewriter
{
public:
	static inline unsigned __int64 DEFAULT_WRITER_SIZE = 2048;

	unsigned char* buffer_pointer;
	unsigned __int64 buffer_size;
	unsigned __int64 buffer_position;

	void write_int8(char value);
	void write_uint8(unsigned char value);
	void write_int16(short value);
	void write_uint16(unsigned short value);
	void write_int32(int value);
	void write_uint32(unsigned int value);
	void write_float(float value);
	void write_int64(__int64 value);
	void write_uint64(unsigned __int64 value);
	void write_double(double value);
	void write_text(void* value);
	void write_buffer(void* pointer, __int64 size);

	#define wint8(value) write_int8(value)
	#define wuint8(value) write_uint8(value)
	#define wint16(value) write_int16(value)
	#define wuint16(value) write_uint16(value)
	#define wint32(value) write_int32(value)
	#define wuint32(value) write_uint32(value)
	#define wfloat(value) write_float(value)
	#define wint64(value) write_int64(value)
	#define wuint64(value) write_uint64(value)
	#define wdouble(value) write_double(value)
	#define wtext(value) write_text((void*)value)
	#define wbuffer(pointer, size) write_buffer(pointer, size)

	void round_data(size_t roundDivider);
	void resize_buffer(__int64 size);

	bytewriter();
	bytewriter(__int64 size = DEFAULT_WRITER_SIZE);
	bytewriter(void* pointer, __int64 size = 0xFFFFFFFF, __int64 position = 0);
};

