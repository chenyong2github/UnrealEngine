// Copyright Epic Games, Inc. All Rights Reserved.

#include "OnlineStatsEOSPlus.h"
#include "OnlineSubsystem.h"
#include "OnlineSubsystemEOSPlus.h"
#include "EOSSettings.h"

void FOnlineStatsEOSPlus::QueryStats(const FUniqueNetIdRef LocalUserId, const FUniqueNetIdRef StatsUser, const FOnlineStatsQueryUserStatsComplete& Delegate)
{
	IOnlineStatsPtr Stats = EOSPlus->BaseOSS->GetStatsInterface();
	if (Stats.IsValid())
	{
		Stats->QueryStats(LocalUserId, StatsUser, Delegate);
	}
}

void FOnlineStatsEOSPlus::QueryStats(const FUniqueNetIdRef LocalUserId, const TArray<FUniqueNetIdRef>& StatUsers, const TArray<FString>& StatNames, const FOnlineStatsQueryUsersStatsComplete& Delegate)
{
	IOnlineStatsPtr Stats = EOSPlus->BaseOSS->GetStatsInterface();
	if (Stats.IsValid())
	{
		Stats->QueryStats(LocalUserId, StatUsers, StatNames, Delegate);
	}
}

TSharedPtr<const FOnlineStatsUserStats> FOnlineStatsEOSPlus::GetStats(const FUniqueNetIdRef StatsUserId) const
{
	IOnlineStatsPtr Stats = EOSPlus->BaseOSS->GetStatsInterface();
	if (Stats.IsValid())
	{
		return Stats->GetStats(StatsUserId);
	}
	return nullptr;
}

FOnlineStatsUpdateStatsComplete IgnoredStatsComplete;

void FOnlineStatsEOSPlus::UpdateStats(const FUniqueNetIdRef LocalUserId, const TArray<FOnlineStatsUserUpdatedStats>& UpdatedUserStats, const FOnlineStatsUpdateStatsComplete& Delegate)
{
	// This one is the one that will fire the delegate upon completion
	IOnlineStatsPtr Stats = EOSPlus->BaseOSS->GetStatsInterface();
	if (Stats.IsValid())
	{
		Stats->UpdateStats(LocalUserId, UpdatedUserStats, Delegate);
	}
	if (GetDefault<UEOSSettings>()->bMirrorStatsToEOS)
	{
		// Also write the data to EOS
		IOnlineStatsPtr EOSStats = EOSPlus->EosOSS->GetStatsInterface();
		if (EOSStats.IsValid())
		{
			EOSStats->UpdateStats(LocalUserId, UpdatedUserStats, IgnoredStatsComplete);
		}
	}
}

#if !UE_BUILD_SHIPPING
void FOnlineStatsEOSPlus::ResetStats(const FUniqueNetIdRef StatsUserId)
{
	// Only need to forward to the base since EOS doesn't support this
	IOnlineStatsPtr Stats = EOSPlus->BaseOSS->GetStatsInterface();
	if (Stats.IsValid())
	{
		Stats->ResetStats(StatsUserId);
	}
}
#endif
