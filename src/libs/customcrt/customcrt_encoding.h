#pragma once
// utf8to16
int utf8to16(const char* src, int src_len, wchar_t* dst, int dst_len);

inline int utf8to16(const void* src, int src_len, const void* dst, int dst_len) { return utf8to16((char*)src, src_len, (wchar_t*)dst, dst_len); }
inline int utf8to16(void* src, int src_len, const void* dst, int dst_len) { return utf8to16((char*)src, src_len, (wchar_t*)dst, dst_len); }
inline int utf8to16(const void* src, int src_len, void* dst, int dst_len) { return utf8to16((char*)src, src_len, (wchar_t*)dst, dst_len); }
inline int utf8to16(void* src, int src_len, void* dst, int dst_len) { return utf8to16((char*)src, src_len, (wchar_t*)dst, dst_len); }

inline int utf8to16(unsigned long long src, int src_len, void* dst, int dst_len) { return utf8to16((char*)src, src_len, (wchar_t*)dst, dst_len); }
inline int utf8to16(void* src, int src_len, unsigned long long dst, int dst_len) { return utf8to16((char*)src, src_len, (wchar_t*)dst, dst_len); }
inline int utf8to16(unsigned long long src, int src_len, unsigned long long dst, int dst_len) { return utf8to16((char*)src, src_len, (wchar_t*)dst, dst_len); }

inline int utf8to16(unsigned long long src, int src_len, const void* dst, int dst_len) { return utf8to16((char*)src, src_len, (wchar_t*)dst, dst_len); }
inline int utf8to16(const void* src, int src_len, unsigned long long dst, int dst_len) { return utf8to16((char*)src, src_len, (wchar_t*)dst, dst_len); }

// utf16to8
int utf16to8(const wchar_t* src, int src_len, char* dst, int dst_len);

inline int utf16to8(const void* src, int src_len, const void* dst, int dst_len) { return utf16to8((wchar_t*)src, src_len, (char*)dst, dst_len); }
inline int utf16to8(void* src, int src_len, const void* dst, int dst_len) { return utf16to8((wchar_t*)src, src_len, (char*)dst, dst_len); }
inline int utf16to8(const void* src, int src_len, void* dst, int dst_len) { return utf16to8((wchar_t*)src, src_len, (char*)dst, dst_len); }
inline int utf16to8(void* src, int src_len, void* dst, int dst_len) { return utf16to8((wchar_t*)src, src_len, (char*)dst, dst_len); }

inline int utf16to8(unsigned long long src, int src_len, void* dst, int dst_len) { return utf16to8((wchar_t*)src, src_len, (char*)dst, dst_len); }
inline int utf16to8(void* src, int src_len, unsigned long long dst, int dst_len) { return utf16to8((wchar_t*)src, src_len, (char*)dst, dst_len); }
inline int utf16to8(unsigned long long src, int src_len, unsigned long long dst, int dst_len) { return utf16to8((wchar_t*)src, src_len, (char*)dst, dst_len); }

inline int utf16to8(unsigned long long src, int src_len, const void* dst, int dst_len) { return utf16to8((wchar_t*)src, src_len, (char*)dst, dst_len); }
inline int utf16to8(const void* src, int src_len, unsigned long long dst, int dst_len) { return utf16to8((wchar_t*)src, src_len, (char*)dst, dst_len); }

// utf7to16
int utf7to16(const char* src, int src_len, wchar_t* dst, int dst_len);

inline int utf7to16(const void* src, int src_len, const void* dst, int dst_len) { return utf7to16((char*)src, src_len, (wchar_t*)dst, dst_len); }
inline int utf7to16(void* src, int src_len, const void* dst, int dst_len) { return utf7to16((char*)src, src_len, (wchar_t*)dst, dst_len); }
inline int utf7to16(const void* src, int src_len, void* dst, int dst_len) { return utf7to16((char*)src, src_len, (wchar_t*)dst, dst_len); }
inline int utf7to16(void* src, int src_len, void* dst, int dst_len) { return utf7to16((char*)src, src_len, (wchar_t*)dst, dst_len); }

inline int utf7to16(unsigned long long src, int src_len, void* dst, int dst_len) { return utf7to16((char*)src, src_len, (wchar_t*)dst, dst_len); }
inline int utf7to16(void* src, int src_len, unsigned long long dst, int dst_len) { return utf7to16((char*)src, src_len, (wchar_t*)dst, dst_len); }
inline int utf7to16(unsigned long long src, int src_len, unsigned long long dst, int dst_len) { return utf7to16((char*)src, src_len, (wchar_t*)dst, dst_len); }

inline int utf7to16(unsigned long long src, int src_len, const void* dst, int dst_len) { return utf7to16((char*)src, src_len, (wchar_t*)dst, dst_len); }
inline int utf7to16(const void* src, int src_len, unsigned long long dst, int dst_len) { return utf7to16((char*)src, src_len, (wchar_t*)dst, dst_len); }

// utf16to7
int utf16to7(const wchar_t* src, int src_len, char* dst, int dst_len);

inline int utf16to7(const void* src, int src_len, const void* dst, int dst_len) { return utf16to7((wchar_t*)src, src_len, (char*)dst, dst_len); }
inline int utf16to7(void* src, int src_len, const void* dst, int dst_len) { return utf16to7((wchar_t*)src, src_len, (char*)dst, dst_len); }
inline int utf16to7(const void* src, int src_len, void* dst, int dst_len) { return utf16to7((wchar_t*)src, src_len, (char*)dst, dst_len); }
inline int utf16to7(void* src, int src_len, void* dst, int dst_len) { return utf16to7((wchar_t*)src, src_len, (char*)dst, dst_len); }

inline int utf16to7(unsigned long long src, int src_len, void* dst, int dst_len) { return utf16to7((wchar_t*)src, src_len, (char*)dst, dst_len); }
inline int utf16to7(void* src, int src_len, unsigned long long dst, int dst_len) { return utf16to7((wchar_t*)src, src_len, (char*)dst, dst_len); }
inline int utf16to7(unsigned long long src, int src_len, unsigned long long dst, int dst_len) { return utf16to7((wchar_t*)src, src_len, (char*)dst, dst_len); }

inline int utf16to7(unsigned long long src, int src_len, const void* dst, int dst_len) { return utf16to7((wchar_t*)src, src_len, (char*)dst, dst_len); }
inline int utf16to7(const void* src, int src_len, unsigned long long dst, int dst_len) { return utf16to7((wchar_t*)src, src_len, (char*)dst, dst_len); }