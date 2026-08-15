#include "pch.h"
#include "hwid.h"
#include "crypto.h"
#include <intrin.h>

#pragma comment(lib, "advapi32.lib")

namespace
{
    bool g_ready = false;
    unsigned char g_hwid[32];

    void feed_registry_machine_guid(crypto::sha256_ctx* ctx)
    {
        HKEY key = 0;
        // KEY_WOW64_64KEY matters: a 32-bit build must not read the redirected hive.
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Cryptography", 0,
                          KEY_READ | KEY_WOW64_64KEY, &key) != ERROR_SUCCESS)
            return;

        wchar_t guid[128];
        DWORD size = sizeof(guid);
        DWORD type = 0;
        if (RegQueryValueExW(key, L"MachineGuid", 0, &type, (LPBYTE)guid, &size) == ERROR_SUCCESS &&
            type == REG_SZ)
        {
            crypto::sha256_update(ctx, guid, size);
        }
        RegCloseKey(key);
    }

    void feed_volume_serial(crypto::sha256_ctx* ctx)
    {
        wchar_t sysdir[MAX_PATH];
        if (!GetSystemDirectoryW(sysdir, MAX_PATH)) return;

        wchar_t root[4] = { sysdir[0], L':', L'\\', 0 };
        DWORD serial = 0;
        if (GetVolumeInformationW(root, 0, 0, &serial, 0, 0, 0, 0))
            crypto::sha256_update(ctx, &serial, sizeof(serial));
    }

    void feed_computer_name(crypto::sha256_ctx* ctx)
    {
        wchar_t name[256];
        DWORD size = 256;
        if (GetComputerNameW(name, &size))
            crypto::sha256_update(ctx, name, size * (DWORD)sizeof(wchar_t));
    }

    void feed_cpu(crypto::sha256_ctx* ctx)
    {
        int regs[4];

        __cpuid(regs, 0);
        crypto::sha256_update(ctx, regs, sizeof(regs));

        __cpuid(regs, 1);
        // EAX holds stepping/model/family; ECX/EDX are the feature masks. EBX
        // carries the APIC id, which changes per core, so it is excluded.
        crypto::sha256_update(ctx, &regs[0], sizeof(int));
        crypto::sha256_update(ctx, &regs[2], sizeof(int) * 2);

        // Processor brand string.
        for (unsigned int leaf = 0x80000002; leaf <= 0x80000004; leaf++)
        {
            __cpuid(regs, (int)leaf);
            crypto::sha256_update(ctx, regs, sizeof(regs));
        }
    }

    void feed_smbios(crypto::sha256_ctx* ctx)
    {
        // 'RSMB' - raw SMBIOS table. Contains the board/system UUID.
        UINT size = GetSystemFirmwareTable('RSMB', 0, 0, 0);
        if (!size || size > (4u << 20)) return;

        unsigned char* buf = (unsigned char*)memalloc((int)size);
        if (!buf) return;

        if (GetSystemFirmwareTable('RSMB', 0, buf, size) == size)
            crypto::sha256_update(ctx, buf, size);

        memfree(buf);
    }
}

void hwid::get(unsigned char out[32])
{
    if (!g_ready)
    {
        crypto::sha256_ctx ctx;
        crypto::sha256_init(&ctx);

        const char* domain = "IMDiscord/hwid/v1";
        crypto::sha256_update(&ctx, domain, (unsigned int)ccslenf(domain));

        feed_registry_machine_guid(&ctx);
        feed_volume_serial(&ctx);
        feed_computer_name(&ctx);
        feed_cpu(&ctx);
        feed_smbios(&ctx);

        crypto::sha256_final(&ctx, g_hwid);
        g_ready = true;
    }
    ccpy(out, g_hwid, 32);
}

void hwid::get_hex(char* out, int cap)
{
    if (cap < 2) { if (cap > 0) out[0] = 0; return; }

    unsigned char id[32];
    get(id);

    const char* hex = "0123456789abcdef";
    int n = (cap - 1) / 2;
    if (n > 16) n = 16;

    for (int i = 0; i < n; i++)
    {
        out[i * 2 + 0] = hex[(id[i] >> 4) & 0xF];
        out[i * 2 + 1] = hex[id[i] & 0xF];
    }
    out[n * 2] = 0;
}
