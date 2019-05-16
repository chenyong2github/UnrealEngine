// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Containers/UnrealString.h"
#include "Templates/SharedPointer.h"

namespace Trace
{

class IInDataStream;
class IOutDataStream;
typedef uint64 FSessionHandle;

struct FSessionInfo
{
	const TCHAR* Name;
	bool IsLive;
};

class IStore
{
public:
	virtual ~IStore() = default;
	virtual void GetAvailableSessions(TArray<FSessionHandle>& OutSessions) = 0;
	virtual bool GetSessionInfo(FSessionHandle Handle, FSessionInfo& OutInfo) = 0;
	virtual IOutDataStream* CreateNewSession() = 0;
	virtual IInDataStream* OpenSessionStream(FSessionHandle Handle) = 0;
};

TRACEANALYSIS_API TSharedPtr<IStore> Store_Create(const TCHAR* StoreDir);

}
