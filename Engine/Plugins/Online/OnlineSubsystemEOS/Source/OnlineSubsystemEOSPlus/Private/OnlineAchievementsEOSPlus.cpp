// Copyright Epic Games, Inc. All Rights Reserved.

#include "OnlineAchievementsEOSPlus.h"
#include "OnlineSubsystem.h"
#include "OnlineSubsystemEOSPlus.h"
#include "EOSSettings.h"

FOnAchievementsWrittenDelegate Ignored;

void FOnlineAchievementsEOSPlus::WriteAchievements(const FUniqueNetId& PlayerId, FOnlineAchievementsWriteRef& WriteObject, const FOnAchievementsWrittenDelegate& Delegate)
{
	IOnlineAchievementsPtr Achievements = EOSPlus->BaseOSS->GetAchievementsInterface();
	if (Achievements.IsValid())
	{
		Achievements->WriteAchievements(PlayerId, WriteObject, Delegate);
	}
	if (GetDefault<UEOSSettings>()->bMirrorAchievementsToEOS)
	{
		// Mirror the achievement data to EOS
		IOnlineAchievementsPtr EOSAchievements = EOSPlus->EosOSS->GetAchievementsInterface();
		if (EOSAchievements.IsValid())
		{
			EOSAchievements->WriteAchievements(PlayerId, WriteObject, Ignored);
		}
	}
}

void FOnlineAchievementsEOSPlus::QueryAchievements(const FUniqueNetId& PlayerId, const FOnQueryAchievementsCompleteDelegate& Delegate)
{
	IOnlineAchievementsPtr Achievements = EOSPlus->BaseOSS->GetAchievementsInterface();
	if (Achievements.IsValid())
	{
		Achievements->QueryAchievements(PlayerId, Delegate);
	}
}

void FOnlineAchievementsEOSPlus::QueryAchievementDescriptions(const FUniqueNetId& PlayerId, const FOnQueryAchievementsCompleteDelegate& Delegate)
{
	IOnlineAchievementsPtr Achievements = EOSPlus->BaseOSS->GetAchievementsInterface();
	if (Achievements.IsValid())
	{
		Achievements->QueryAchievements(PlayerId, Delegate);
	}
}

EOnlineCachedResult::Type FOnlineAchievementsEOSPlus::GetCachedAchievement(const FUniqueNetId& PlayerId, const FString& AchievementId, FOnlineAchievement& OutAchievement)
{
	IOnlineAchievementsPtr Achievements = EOSPlus->BaseOSS->GetAchievementsInterface();
	if (Achievements.IsValid())
	{
		return Achievements->GetCachedAchievement(PlayerId, AchievementId, OutAchievement);
	}
	return EOnlineCachedResult::NotFound;
}

EOnlineCachedResult::Type FOnlineAchievementsEOSPlus::GetCachedAchievements(const FUniqueNetId& PlayerId, TArray<FOnlineAchievement>& OutAchievements)
{
	IOnlineAchievementsPtr Achievements = EOSPlus->BaseOSS->GetAchievementsInterface();
	if (Achievements.IsValid())
	{
		return Achievements->GetCachedAchievements(PlayerId, OutAchievements);
	}
	return EOnlineCachedResult::NotFound;
}

EOnlineCachedResult::Type FOnlineAchievementsEOSPlus::GetCachedAchievementDescription(const FString& AchievementId, FOnlineAchievementDesc& OutAchievementDesc)
{
	IOnlineAchievementsPtr Achievements = EOSPlus->BaseOSS->GetAchievementsInterface();
	if (Achievements.IsValid())
	{
		return Achievements->GetCachedAchievementDescription(AchievementId, OutAchievementDesc);
	}
	return EOnlineCachedResult::NotFound;
}

#if !UE_BUILD_SHIPPING
bool FOnlineAchievementsEOSPlus::ResetAchievements(const FUniqueNetId& PlayerId)
{
	IOnlineAchievementsPtr Achievements = EOSPlus->BaseOSS->GetAchievementsInterface();
	if (Achievements.IsValid())
	{
		return Achievements->ResetAchievements(PlayerId);
	}
	return false;
}
#endif
