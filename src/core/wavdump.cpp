#include "pch.h"
#include "wavdump.h"
#include "log.h"

#include "audio/audio.h"
#include "system/io/ufile.h"

namespace
{
    // Half an hour of stereo at 48 kHz. Far more than any diagnosis needs, and
    // small enough that forgetting the switch on costs a disk, not a machine.
    const unsigned int SIZE_LIMIT = 340u * 1024u * 1024u;

    int g_on = -1;      // -1 not yet asked

    // A canonical 44 byte PCM header. The two length fields are written as
    // zero and patched when the file is closed, so a recording that ends in a
    // crash still opens - players read to the end of the data they find.
    void write_header(HANDLE file)
    {
        unsigned char h[44];
        ccfset(h, 0, sizeof(h));

        const int rate = AUDIO_SAMPLE_RATE;
        const int channels = AUDIO_CHANNELS;
        const int bits = 16;
        const int block = channels * bits / 8;
        const int bytes_per_sec = rate * block;

        ccpy(h + 0, "RIFF", 4);
        ccpy(h + 8, "WAVEfmt ", 8);
        h[16] = 16;                                   // fmt chunk size
        h[20] = 1;                                    // PCM
        h[22] = (unsigned char)channels;
        h[24] = (unsigned char)(rate & 0xFF);
        h[25] = (unsigned char)((rate >> 8) & 0xFF);
        h[26] = (unsigned char)((rate >> 16) & 0xFF);
        h[28] = (unsigned char)(bytes_per_sec & 0xFF);
        h[29] = (unsigned char)((bytes_per_sec >> 8) & 0xFF);
        h[30] = (unsigned char)((bytes_per_sec >> 16) & 0xFF);
        h[32] = (unsigned char)block;
        h[34] = (unsigned char)bits;
        ccpy(h + 36, "data", 4);

        DWORD done = 0;
        WriteFile(file, h, sizeof(h), &done, 0);
    }

    void patch_sizes(HANDLE file, unsigned int data_bytes)
    {
        unsigned int riff = data_bytes + 36;
        DWORD done = 0;

        SetFilePointer(file, 4, 0, FILE_BEGIN);
        WriteFile(file, &riff, 4, &done, 0);

        SetFilePointer(file, 40, 0, FILE_BEGIN);
        WriteFile(file, &data_bytes, 4, &done, 0);
    }
}

bool wavdump::enabled()
{
    if (g_on < 0)
    {
        wchar_t value[8];
        g_on = GetEnvironmentVariableW(L"IMD_AUDIODUMP", value, 8) > 0 ? 1 : 0;
        if (g_on) log_line("wavdump: запись звука включена (IMD_AUDIODUMP)");
    }
    return g_on == 1;
}

void wavdump::start(sink* s, const wchar_t* name)
{
    ccfset(s, 0, sizeof(*s));

    int i = 0;
    while (name[i] && i < 62) { s->name[i] = name[i]; i++; }
    s->name[i] = 0;
}

void wavdump::write(sink* s, const short* pcm, int samples)
{
    if (!s || samples <= 0 || !enabled()) return;
    if (s->written >= SIZE_LIMIT) return;

    if (!s->opened)
    {
        s->opened = true;

        wchar_t path[MAX_PATH];
        if (!ufile::app_path(s->name, path, MAX_PATH)) return;

        HANDLE file = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, 0,
                                  CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
        if (file == INVALID_HANDLE_VALUE) return;

        write_header(file);
        s->file = file;

        char utf8[128];
        wcstochar(path, utf8, sizeof(utf8));
        log_line("wavdump: пишу %s", utf8);
    }

    if (!s->file) return;

    DWORD done = 0;
    WriteFile((HANDLE)s->file, pcm, (DWORD)(samples * (int)sizeof(short)), &done, 0);
    s->written += done;
}

void wavdump::finish(sink* s)
{
    if (!s || !s->file) return;

    patch_sizes((HANDLE)s->file, s->written);
    CloseHandle((HANDLE)s->file);

    s->file = 0;
    s->written = 0;
    s->opened = false;
}
