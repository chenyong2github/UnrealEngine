// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"

class FSocket;
class FInternetAddr;

namespace Trace
{

////////////////////////////////////////////////////////////////////////////////
class TRACEANALYSIS_API FControlClient
{
public:
                ~FControlClient();
    bool        Connect(const TCHAR* Host, uint16 Port=1985);
	bool        Connect(const FInternetAddr& Address);
    void        Disconnect();
    bool        IsConnected() const;
    void        SendConnect(const TCHAR* Path);
    void        SendToggleEvent(const TCHAR* Logger, bool bState=true);
    void        Send(const TCHAR* Command);

private:
    void        FormatAndSend(const TCHAR* Format, ...);
    void        Send(const uint8* Data, int Length);
    FSocket*    Socket = nullptr;
};

} // namespace Trace
