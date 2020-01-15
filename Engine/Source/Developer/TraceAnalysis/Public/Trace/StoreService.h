// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/Platform.h"

namespace Trace
{

////////////////////////////////////////////////////////////////////////////////
class TRACEANALYSIS_API FStoreService
{
public:
	struct FDesc
	{
		const TCHAR*		StoreDir;
		int32				RecorderPort = 0; // 0:auto-assign, -1:off
		int32				ThreadCount  = 0; // <=0:logical CPU count
	};

							~FStoreService();
	static FStoreService*	Create(const FDesc& Desc);
	uint32					GetPort() const;

private:
							FStoreService() = default;
	struct					FImpl;
	FImpl*					Impl;

private:
							FStoreService(const FStoreService&) = delete;
							FStoreService(const FStoreService&&) = delete;
	void					operator = (const FStoreService&) = delete;
	void					operator = (const FStoreService&&) = delete;
};

} // namespace Trace
