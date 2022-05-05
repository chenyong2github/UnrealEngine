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

	//...
};

TRACESERVICES_API FName GetMetadataProviderName();
TRACESERVICES_API const IMetadataProvider* ReadMetadataProvider(const IAnalysisSession& Session);

} // namespace TraceServices
