// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AnalysisSession.h"
#include "CoreTypes.h"
#include "Templates/UniquePtr.h"
#include "ProfilingDebugging/MemoryTrace.h"

#include <new>

namespace TraceServices
{

////////////////////////////////////////////////////////////////////////////////////////////////////

class IMetadataProvider : public IProvider
{
public:
	struct TRACESERVICES_API FEditScopeLock
	{
		FEditScopeLock(const IMetadataProvider& InMetadataProvider)
			: MetadataProvider(InMetadataProvider)
		{
			MetadataProvider.BeginEdit();
		}

		~FEditScopeLock()
		{
			MetadataProvider.EndEdit();
		}

		const IMetadataProvider& MetadataProvider;
	};

	struct TRACESERVICES_API FReadScopeLock
	{
		FReadScopeLock(const IMetadataProvider& InMetadataProvider)
			: MetadataProvider(InMetadataProvider)
		{
			MetadataProvider.BeginRead();
		}

		~FReadScopeLock()
		{
			MetadataProvider.EndRead();
		}

		const IMetadataProvider& MetadataProvider;
	};

public:
	virtual ~IMetadataProvider() = default;

	virtual void BeginEdit() const = 0;
	virtual void EndEdit() const = 0;
	virtual void BeginRead() const = 0;
	virtual void EndRead() const = 0;

	virtual uint16 GetRegisteredMetadataType(const TCHAR* Name) const = 0;
	virtual const TCHAR* GetRegisteredMetadataName(uint16 Type) const = 0;

	virtual uint32 GetMetadataStackSize(uint32 InThreadId, uint32 InMetadataId) const = 0;
	virtual bool GetMetadata(uint32 InThreadId, uint32 InMetadataId, uint32 InStackDepth, uint16& OutType, const void*& OutData, uint32& OutSize) const = 0;
	virtual void EnumerateMetadata(uint32 InThreadId, uint32 InMetadataId, TFunctionRef<bool(uint32 StackDepth, uint16 Type, const void* Data, uint32 Size)> Callback) const = 0;
};

TRACESERVICES_API FName GetMetadataProviderName();
TRACESERVICES_API const IMetadataProvider* ReadMetadataProvider(const IAnalysisSession& Session);

} // namespace TraceServices
