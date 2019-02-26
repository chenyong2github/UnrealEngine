// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)

namespace GranularNetworkMemoryTrackingPrivate
{
	struct ENGINE_API FHelper
	{
		FHelper(FArchive& InAr, FString&& InScopeName);

		void BeginWork();

		void EndWork(const FString& WorkName);

		void LogCustomWork(const FString& WorkName, const uint64 Bytes) const;

		const bool IsEnabled() const
		{
			return bShouldTrack;
		}

	private:

		uint64 PreWorkPos = 0;

		const FArchive& Ar;
		const FString ScopeName;
		const bool bShouldTrack;
	};
}

#define GRANULAR_NETWORK_MEMORY_TRACKING_INIT(Archive, ScopeName) GranularNetworkMemoryTrackingPrivate::FHelper GranularNetworkMemoryHelper(Archive, ScopeName);
#define GRANULAR_NETWORK_MEMORY_TRACKING_TRACK(Id, Work) \
	{ \
		GranularNetworkMemoryHelper.BeginWork(); \
		Work; \
		GranularNetworkMemoryHelper.EndWork(Id); \
	}
#define GRANULAR_NETWORK_MEMORY_TRACKING_CUSTOM_WORK(Id, Value) GranularNetworkMemoryHelper.LogCustomWork(Id, Value);

#else

#define GRANULAR_NETWORK_MEMORY_TRACKING_INIT(Archive, ScopeName) 
#define GRANULAR_NETWORK_MEMORY_TRACKING_TRACK(Id, Work) { Work; }
#define GRANULAR_NETWORK_MEMORY_TRACKING_CUSTOM_WORK(Id, Work) 

#endif
