#include "pch.h"
#include "customcrt_windows.h"
#include <winternl.h>

struct LDR_DATA_TABLE_ENTRY_T {
    LIST_ENTRY InLoadOrderLinks;
    LIST_ENTRY InMemoryOrderLinks;
    LIST_ENTRY InInitializationOrderLinks;
    void* DllBase;
    void* EntryPoint;
    union {
        unsigned long SizeOfImage;
        const char* _dummy;
    };
    UNICODE_STRING FullDllName;
    UNICODE_STRING BaseDllName;
};

void* get_base_module(const wchar_t* module)
{
    PEB* pPeb = (PEB*)__readgsqword(0x60);

    PEB_LDR_DATA* pLdrData = pPeb->Ldr;
    LIST_ENTRY* pListHead = &pLdrData->InMemoryOrderModuleList;
    LIST_ENTRY* pListEntry = pListHead->Flink;

    while (pListEntry != pListHead)
    {
        LDR_DATA_TABLE_ENTRY_T* pEntry = (LDR_DATA_TABLE_ENTRY_T*)CONTAINING_RECORD(pListEntry, LDR_DATA_TABLE_ENTRY, InMemoryOrderLinks);
        if (pEntry->BaseDllName.Buffer && ccwcmpif(pEntry->BaseDllName.Buffer, module) == 0)
        {
            return pEntry->DllBase;
        }

        pListEntry = pListEntry->Flink;
    }

    return nullptr;
}

void* get_import(void* module, const char* import)
{
    char* pModuleBase = (char*)module;

    IMAGE_DOS_HEADER* pDosHeader = (IMAGE_DOS_HEADER*)pModuleBase;
    IMAGE_NT_HEADERS* pNtHeaders = (IMAGE_NT_HEADERS*)(pModuleBase + pDosHeader->e_lfanew);

    IMAGE_DATA_DIRECTORY* pExportDataDir = &pNtHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];

    DWORD exportStart = pExportDataDir->VirtualAddress;
    DWORD exportEnd = exportStart + pExportDataDir->Size;

    IMAGE_EXPORT_DIRECTORY* pExportDir = (IMAGE_EXPORT_DIRECTORY*)(pModuleBase + exportStart);

    DWORD* pNames = (DWORD*)(pModuleBase + pExportDir->AddressOfNames);
    DWORD* pFunctions = (DWORD*)(pModuleBase + pExportDir->AddressOfFunctions);
    WORD* pOrdinals = (WORD*)(pModuleBase + pExportDir->AddressOfNameOrdinals);

    for (DWORD i = 0; i < pExportDir->NumberOfNames; ++i)
    {
        char* pFuncName = pModuleBase + pNames[i];
        if (ccscmpf(pFuncName, import) == 0)
        {
            WORD ordinal = pOrdinals[i];
            DWORD functionRva = pFunctions[ordinal];

            if (functionRva >= exportStart && functionRva < exportEnd)
            {
                char* forwarderStr = pModuleBase + functionRva;

                char dllName[64] = { 0 };
                char funcName[128] = { 0 };

                int dotIdx = -1;
                for (int j = 0; forwarderStr[j]; j++) {
                    if (forwarderStr[j] == '.') {
                        dotIdx = j;
                        break;
                    }
                }

                if (dotIdx != -1) {
                    ccpy(dllName, forwarderStr, dotIdx);
                    ccpy(dllName + dotIdx, xor_a(".dll"), 5);

                    int funcLen = 0;
                    while (forwarderStr[dotIdx + 1 + funcLen]) {
                        funcName[funcLen] = forwarderStr[dotIdx + 1 + funcLen];
                        funcLen++;
                    }
                    funcName[funcLen] = '\0';

                    void* targetDll = (void*)LoadLibraryA(dllName);
                    if (targetDll) {
                        return get_import(targetDll, funcName);
                    }
                }
                return nullptr;
            }

            return (void*)(pModuleBase + functionRva);
        }
    }

    return nullptr;
}