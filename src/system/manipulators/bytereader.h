#pragma once
class bytereader
{
public:
	unsigned char* buffer_pointer;
	unsigned __int64 buffer_position;
	unsigned __int64 buffer_size;

	char				read_int8();
	unsigned char		read_uint8();
	short				read_int16();
	unsigned short		read_uint16();
	int					read_int32();
	unsigned int		read_uint32();
	__int64				read_int64();
	unsigned __int64	read_uint64();
	float				read_float();
	double				read_double();
	void*				read_text();
	void*				read_buffer(__int64 size);

	#define rint8() read_int8()
	#define ruint8() read_uint8()
	#define rint16() read_int16()
	#define ruint16() read_uint16()
	#define rint32() read_int32();
	#define ruint32() read_uint32()
	#define rint64() read_int64()
	#define ruint64() read_uint64()
	#define rfloat() read_float()
	#define rdouble() read_double()
	#define rtext() read_text()
	#define rbuffer(size) read_buffer(size)

	bytereader(void* pointer, __int64 size = 0xFFFFFFFF, __int64 position = 0);
};	

