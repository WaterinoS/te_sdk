#include "te-sdk.h"

namespace te::sdk::helper::samp
{
    bool AddChatMessage(const char* text, uint32_t color)
    {
        if (!text)
        {
            te::sdk::helper::logging::Log("[SendChatMessage] Invalid text parameter");
            return false;
        }

        HMODULE sampModule = GetModuleHandleA("samp.dll");
        if (!sampModule)
        {
            te::sdk::helper::logging::Log("[SendChatMessage] samp.dll not found");
            return false;
        }

        te::sdk::helper::SAMPVersion sampVersion = te::sdk::helper::GetSAMPVersion();

        uintptr_t sampBase = reinterpret_cast<uintptr_t>(sampModule);
        uintptr_t chatInfoOffset = 0;
        uintptr_t addToChatWndOffset = 0;

        // Map SDK enum to offsets
        switch (sampVersion)
        {
        case te::sdk::helper::SAMPVersion::R1:
            chatInfoOffset = 0x21A0E4;
            addToChatWndOffset = 0x64010;
            break;
        case te::sdk::helper::SAMPVersion::R2:
            chatInfoOffset = 0x21A0EC;
            addToChatWndOffset = 0x640E0;
            break;
        case te::sdk::helper::SAMPVersion::DL:
            chatInfoOffset = 0x2ACA10;
            addToChatWndOffset = 0x67650;
            break;
        case te::sdk::helper::SAMPVersion::R3:
            chatInfoOffset = 0x26E8C8;
            addToChatWndOffset = 0x67460;
            break;
        case te::sdk::helper::SAMPVersion::R4:
            chatInfoOffset = 0x26E9F8;
            addToChatWndOffset = 0x67BA0;
            break;
        case te::sdk::helper::SAMPVersion::R4v2:
            chatInfoOffset = 0x26E9F8;
            addToChatWndOffset = 0x67BE0;
            break;
        case te::sdk::helper::SAMPVersion::R5:
            chatInfoOffset = 0x26EB80;
            addToChatWndOffset = 0x67BE0;
            break;
        case te::sdk::helper::SAMPVersion::Unknown:
        default:
            te::sdk::helper::logging::Log("[SendChatMessage] Unknown or unsupported SAMP version");
            return false;
        }

        try
        {
            // Get chat info pointer
            uintptr_t* pChatInfo = reinterpret_cast<uintptr_t*>(sampBase + chatInfoOffset);
            if (IsBadReadPtr(pChatInfo, sizeof(uintptr_t)) || !*pChatInfo)
            {
                te::sdk::helper::logging::Log("[SendChatMessage] Invalid chat info pointer for version %s",
                    te::sdk::helper::TranslateSAMPVersion(sampVersion).c_str());
                return false;
            }

            uintptr_t chatInfo = *pChatInfo;
            uintptr_t addToChatWndFunc = sampBase + addToChatWndOffset;

            // Function signature: AddToChatWnd(void* this, int type, const char* text, const char* prefix, DWORD textColor, DWORD prefixColor)
            using AddToChatWndFunc = void(__thiscall*)(void*, int, const char*, const char*, uint32_t, uint32_t);
            auto addToChatWnd = reinterpret_cast<AddToChatWndFunc>(addToChatWndFunc);

            addToChatWnd(reinterpret_cast<void*>(chatInfo), 8, text, "", color, color);
            return true;
        }
        catch (const std::exception& e)
        {
            te::sdk::helper::logging::Log("[SendChatMessage] Exception occurred: %s", e.what());
            return false;
        }
        catch (...)
        {
            te::sdk::helper::logging::Log("[SendChatMessage] Unknown exception occurred");
            return false;
        }
    }
}