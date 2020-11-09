// Copyright Epic Games, Inc. All Rights Reserved.

#include "OnlineAchievementsEOSPlus.h"
#include "OnlineSubsystem.h"
#include "OnlineSubsystemEOSPlus.h"
#include "EOSSettings.h"

FOnAchievementsWrittenDelegate Ignored;

FOnlineAchievementsEOSPlus::FOnlineAchievementsEOSPlus(FOnlineSubsystemEOSPlus* InSubsystem)
	: EOSPlus(InSubsystem)
{
	BaseAchievementsInterface = EOSPlus->BaseOSS->GetAchievementsInterface();
	check(BaseAchievementsInterface.IsValid());
	EosAchievementsInterface = EOSPlus->EosOSS->GetAchievementsInterface();
	check(EosAchievementsInterface.IsValid());

	BaseAchievementsInterface->AddOnAchievementUnlockedDelegate_Handle(FOnAchievementUnlockedDelegate::CreateRaw(this, &FOnlineAchievementsEOSPlus::OnAchievementUnlocked));
}

FOnlineAchievementsEOSPlus::~FOnlineAchievementsEOSPlus()
{
	BaseAchievementsInterface->ClearOnAchievementUnlockedDelegates(this);
}

void FOnlineAchievementsEOSPlus::OnAchievementUnlocked(const FUniqueNetId& PlayerId, const FString& AchievementId)
{
	TriggerOnAchievementUnlockedDelegates(PlayerId, AchievementId);
}

void FOnlineAchievementsEOSPlus::WriteAchievements(const FUniqueNetId& PlayerId, FOnlineAchievementsWriteRef& WriteObject, const FOnAchievementsWrittenDelegate& Delegate)
{
	BaseAchievementsInterface->WriteAchievements(PlayerId, WriteObject, Delegate);
	if (GetDefault<UEOSSettings>()->bMirrorAchievementsToEOS)
	{
		// Mirror the achievement data to EOS
		EosAchievementsInterface->WriteAchievements(PlayerId, WriteObject, Ignored);
	}
}

void FOnlineAchievementsEOSPlus::QueryAchievements(const FUniqueNetId& PlayerId, const FOnQueryAchievementsCompleteDelegate& Delegate)
{
	BaseAchievementsInterface->QueryAchievements(PlayerId, Delegate);
}

void FOnlineAchievementsEOSPlus::QueryAchievementDescriptions(const FUniqueNetId& PlayerId, const FOnQueryAchievementsCompleteDelegate& Delegate)
{
	BaseAchievementsInterface->QueryAchievements(PlayerId, Delegate);
}

EOnlineCachedResult::Type FOnlineAchievementsEOSPlus::GetCachedAchievement(const FUniqueNetId& PlayerId, const FString& AchievementId, FOnlineAchievement& OutAchievement)
{
	return BaseAchievementsInterface->GetCachedAchievement(PlayerId, AchievementId, OutAchievement);
}

EOnlineCachedResult::Type FOnlineAchievementsEOSPlus::GetCachedAchievements(const FUniqueNetId& PlayerId, TArray<FOnlineAchievement>& OutAchievements)
{
	return BaseAchievementsInterface->GetCachedAchievements(PlayerId, OutAchievements);
}

EOnlineCachedResult::Type FOnlineAchievementsEOSPlus::GetCachedAchievementDescription(const FString& AchievementId, FOnlineAchievementDesc& OutAchievementDesc)
{
	return BaseAchievementsInterface->GetCachedAchievementDescription(AchievementId, OutAchievementDesc);
}

#if !UE_BUILD_SHIPPING
bool FOnlineAchievementsEOSPlus::ResetAchievements(const FUniqueNetId& PlayerId)
{
	return BaseAchievementsInterface->ResetAchievements(PlayerId);
}
#endif
