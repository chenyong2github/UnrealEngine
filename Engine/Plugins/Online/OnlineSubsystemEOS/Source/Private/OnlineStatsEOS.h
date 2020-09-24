// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/CoreOnline.h"
#include "Interfaces/OnlineStatsInterface.h"
#include "OnlineSubsystemEOSPackage.h"
#include "OnlineSubsystemEOSTypes.h"

class FOnlineSubsystemEOS;

#if WITH_EOS_SDK
#include "eos_stats_types.h"

/**
 * Interface for interacting with EOS stats
 */
class FOnlineStatsEOS :
	public IOnlineStats
{
public:
	FOnlineStatsEOS() = delete;
	virtual ~FOnlineStatsEOS() = default;

// IOnlineStats Interface
	virtual void QueryStats(const TSharedRef<const FUniqueNetId> LocalUserId, const TSharedRef<const FUniqueNetId> StatsUser, const FOnlineStatsQueryUserStatsComplete& Delegate) override;
	virtual void QueryStats(const TSharedRef<const FUniqueNetId> LocalUserId, const TArray<TSharedRef<const FUniqueNetId>>& StatUsers, const TArray<FString>& StatNames, const FOnlineStatsQueryUsersStatsComplete& Delegate) override;
	virtual TSharedPtr<const FOnlineStatsUserStats> GetStats(const TSharedRef<const FUniqueNetId> StatsUserId) const override;
	virtual void UpdateStats(const TSharedRef<const FUniqueNetId> LocalUserId, const TArray<FOnlineStatsUserUpdatedStats>& UpdatedUserStats, const FOnlineStatsUpdateStatsComplete& Delegate) override;
#if !UE_BUILD_SHIPPING
	virtual void ResetStats(const TSharedRef<const FUniqueNetId> StatsUserId) override;
#endif
// ~IOnlineStats Interface

PACKAGE_SCOPE:
	FOnlineStatsEOS(FOnlineSubsystemEOS* InSubsystem)
		: EOSSubsystem(InSubsystem)
	{
	}

private:
	void WriteStats(EOS_ProductUserId UserId, const FOnlineStatsUserUpdatedStats& PlayerStats);

	/** Reference to the main EOS subsystem */
	FOnlineSubsystemEOS* EOSSubsystem;
	/** Cached list of stats for users as they arrive */
	TUniqueNetIdMap<TSharedRef<FOnlineStatsUserStats>> StatsCache;
};

typedef TSharedPtr<FOnlineStatsEOS, ESPMode::ThreadSafe> FOnlineStatsEOSPtr;

#endif
