#include "te-helper.h"
#include <Windows.h>

namespace te_sdk_helper
{
    static uintptr_t GetModuleBase(const wchar_t* moduleName)
    {
        return reinterpret_cast<uintptr_t>(GetModuleHandleW(moduleName));
    }

    static uint32_t ReadDWORD(uintptr_t address)
    {
        DWORD oldProtect;
        VirtualProtect(reinterpret_cast<void*>(address), 4, PAGE_EXECUTE_READWRITE, &oldProtect);
        uint32_t value = *reinterpret_cast<uint32_t*>(address);
        VirtualProtect(reinterpret_cast<void*>(address), 4, oldProtect, &oldProtect);
        return value;
    }

    SAMPVersion GetSAMPVersion()
    {
        uintptr_t sampBase = GetModuleBase(L"samp.dll");
        if (!sampBase)
            return SAMPVersion::Unknown;

        uint32_t val128 = ReadDWORD(sampBase + 0x128);
        switch (val128)
        {
        case 0x5542F47A: return SAMPVersion::R1;
        case 0x59C30C94: return SAMPVersion::R2;
        case 0x5A6A3130: return SAMPVersion::DL;
        }

        uint32_t val120 = ReadDWORD(sampBase + 0x120);
        switch (val120)
        {
        case 0x5C0B4243: return SAMPVersion::R3;
        case 0x5DD606CD: return SAMPVersion::R4;
        case 0x6094ACAB: return SAMPVersion::R4v2;
        case 0x6372C39E: return SAMPVersion::R5;
        }

        return SAMPVersion::Unknown;
    }

    void* GetSAMPInfo()
    {
        uintptr_t sampBase = GetModuleBase(L"samp.dll");
        if (!sampBase)
            return nullptr;

        SAMPVersion ver = GetSAMPVersion();

        uintptr_t offset = 0;
        switch (ver)
        {
        case SAMPVersion::R1:    offset = 0x21A0F8; break;
        case SAMPVersion::R2:    offset = 0x21A100; break;
        case SAMPVersion::DL:    offset = 0x2ACA24; break;
        case SAMPVersion::R3:    offset = 0x26E8DC; break;
        case SAMPVersion::R4:    offset = 0x26EA0C; break;
        case SAMPVersion::R4v2:  offset = 0x26EA0C; break;
        case SAMPVersion::R5:    offset = 0x26EB94; break;
        default: return nullptr;
        }

        return reinterpret_cast<void*>(sampBase + offset);
    }

    void* GetRakNetInterface()
    {
        void* sampInfo = GetSAMPInfo();
        if (!sampInfo)
            return nullptr;

        SAMPVersion ver = GetSAMPVersion();
        uintptr_t offset = 0;

        switch (ver)
        {
        case SAMPVersion::R1:    offset = 969; break;
        case SAMPVersion::R2:    offset = 24; break;
        case SAMPVersion::R3:    offset = 44; break;
        case SAMPVersion::R4:    offset = 44; break;
        case SAMPVersion::R4v2:  offset = 0; break;
        case SAMPVersion::R5:    offset = 0; break;
        case SAMPVersion::DL:    offset = 44; break;
        default: return nullptr;
        }

        return *reinterpret_cast<void**>(reinterpret_cast<uintptr_t>(sampInfo) + offset);
    }
}

