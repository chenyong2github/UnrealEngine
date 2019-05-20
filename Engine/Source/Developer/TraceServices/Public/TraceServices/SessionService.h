// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Templates/SharedPointer.h"
#include "Trace/Store.h"
#include "Trace/DataStream.h"

namespace Trace
{

class ISessionService
{
public:
	virtual ~ISessionService() = default;
	virtual bool StartRecorderServer() = 0;
	virtual bool IsRecorderServerRunning() const = 0;
	virtual void StopRecorderServer() = 0;
	virtual void GetAvailableSessions(TArray<Trace::FSessionHandle>& OutSessions) = 0;
	virtual bool GetSessionInfo(Trace::FSessionHandle SessionHandle, Trace::FSessionInfo& OutSessionInfo) = 0;
	virtual Trace::IInDataStream* OpenSessionStream(Trace::FSessionHandle SessionHandle) = 0;
	virtual Trace::IInDataStream* OpenSessionFromFile(const TCHAR* FilePath) = 0;
};

}
