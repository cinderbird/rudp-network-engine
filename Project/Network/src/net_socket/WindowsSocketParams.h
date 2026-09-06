#pragma once
#include "pch.h"

struct WindowsSocketParams
{
    int af = 0;
    int type = 0;
    int protocol = 0;
    LPWSAPROTOCOL_INFOW lpProtocolInfo = NULL;
    GROUP g = 0;
    DWORD dwFlags = 0;

    WindowsSocketParams() : af(0), type(0), protocol(0), lpProtocolInfo(NULL), g(0), dwFlags(0) {}
};

