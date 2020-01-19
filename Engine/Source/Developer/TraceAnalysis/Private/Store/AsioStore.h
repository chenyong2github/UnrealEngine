// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Asio/Asio.h"

#if TRACE_WITH_ASIO

#include "AsioFile.h"
#include "AsioObject.h"
#include "Containers/Array.h"
#include "Containers/UnrealString.h"
#include "Utils.h"

namespace Trace
{

class FAsioReadable;
class FAsioWriteable;

////////////////////////////////////////////////////////////////////////////////
enum class EStoreVersion
{
	Value = 0x0100, // 0xMMmm MM=major, mm=minor
};

////////////////////////////////////////////////////////////////////////////////
class FAsioStore
	: public FAsioObject
{
public:
	class FTrace
	{
	public:
		const TCHAR*	GetName() const;
		uint32			GetId() const;
		uint64			GetSize() const;
		uint64			GetTimestamp() const;

	private:
		friend			FAsioStore;
		FString			Name;
		UPTRINT			Handle;
		uint32			Id;
	};

	struct FNewTrace
	{
		uint32			Id;
		FAsioWriteable* Writeable;
	};

						FAsioStore(asio::io_context& IoContext, const TCHAR* InStoreDir);
						~FAsioStore();
	uint32				GetTraceCount() const;
	const FTrace*		GetTraceInfo(uint32 Index) const;
	int32				ReadTrace(uint32 Id);
	FNewTrace			CreateTrace();
	FAsioReadable*		OpenTrace(uint32 Id);

private:
	FTrace*				GetTrace(uint32 Id);
	FTrace*				AddTrace(const TCHAR* Path);
	FString				StoreDir;
	TArray<FTrace*>		Traces;
#if 0
	int32				LastTraceId = -1;
#endif // 0
};

} // namespace Trace

#endif // TRACE_WITH_ASIO
