// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Containers/UnrealString.h"
#include "Templates/SharedPointer.h"

namespace Trace
{

class IInDataStream;
class IOutDataStream;
typedef uint64 FStoreSessionHandle;

struct FStoreSessionInfo
{
	FStoreSessionHandle Handle;
	const TCHAR* Uri;
	const TCHAR* Name;
	bool bIsLive;
};

class IStore
{
public:
	virtual ~IStore() = default;
	virtual void GetAvailableSessions(TArray<FStoreSessionInfo>& OutSessions) const = 0;
	virtual TTuple<FStoreSessionHandle, IOutDataStream*> CreateNewSession() = 0;
	virtual IInDataStream* OpenSessionStream(FStoreSessionHandle Handle) = 0;
};

TRACEANALYSIS_API TSharedPtr<IStore> Store_Create(const TCHAR* StoreDir);

}
