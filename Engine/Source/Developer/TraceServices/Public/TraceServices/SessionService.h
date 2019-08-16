// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Templates/SharedPointer.h"
#include "Trace/Store.h"
#include "Trace/DataStream.h"

namespace Trace
{

typedef uint64 FSessionHandle;

struct FSessionInfo
{
	const TCHAR* Uri;
	const TCHAR* Name;
	bool bIsLive;
};

class ISessionService
{
public:
	virtual ~ISessionService() = default;
	virtual bool StartRecorderServer() = 0;
	virtual bool IsRecorderServerRunning() const = 0;
	virtual void StopRecorderServer() = 0;
	virtual const TCHAR* GetLocalSessionDirectory() const = 0;
	virtual void GetAvailableSessions(TArray<FSessionHandle>& OutSessions) const = 0;
	virtual void GetLiveSessions(TArray<FSessionHandle>& OutSessions) const = 0;
	virtual bool GetSessionInfo(FSessionHandle SessionHandle, FSessionInfo& OutSessionInfo) const = 0;
	virtual Trace::IInDataStream* OpenSessionStream(FSessionHandle SessionHandle) = 0;
	virtual Trace::IInDataStream* OpenSessionFromFile(const TCHAR* FilePath) = 0;
	virtual void SetModuleEnabled(Trace::FSessionHandle SessionHandle, const FName& ModuleName, bool bState) = 0;
	virtual bool IsModuleEnabled(Trace::FSessionHandle SessionHandle, const FName& ModuleName) const = 0;
	virtual bool ConnectSession(const TCHAR* ControlClientAddress) = 0;
};

}
