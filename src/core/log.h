#pragma once

// Line based log into %LOCALAPPDATA%\IMDiscord\imdiscord.log plus
// OutputDebugStringA. Without a CRT there is no stderr to fall back on, so this
// is the only channel for diagnostics.
void log_init();
void log_shutdown();
void log_line(const char* fmt, ...);
void log_bytes(const char* tag, const void* data, unsigned int size);

// Installs an unhandled exception filter that records the fault address as an
// offset from the image base, which is what a map file needs to be useful.
void log_install_crash_handler();

#define LOGF(...) log_line(__VA_ARGS__)
