// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DerivedDataSharedStringFwd.h"
#include "Features/IModularFeature.h"
#include "UObject/NameTypes.h"

#define UE_API DERIVEDDATACACHE_API

class FQueuedThreadPool;

namespace UE::DerivedData
{

/** Provides the thread pool to use when reserving memory, and matches build functions using their type name. */
class IBuildSchedulerThreadPoolProvider : public IModularFeature
{
public:
	static inline const FLazyName FeatureName{"BuildSchedulerThreadPoolProvider"};

	virtual ~IBuildSchedulerThreadPoolProvider() = default;

	/** Returns the type name that this provider corresponds to. */
	virtual const FUtf8SharedString& GetTypeName() const = 0;

	/** Returns the thread pool to use when reserving memory for the build. */
	virtual FQueuedThreadPool* GetThreadPool() const = 0;
};

} // UE::DerivedData

#undef UE_API
