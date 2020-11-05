// Copyright Epic Games, Inc. All Rights Reserved.

#include "OnlineSubsystemEOSPlus.h"

#include "Misc/ConfigCacheIni.h"

bool FOnlineSubsystemEOSPlus::Exec(UWorld* InWorld, const TCHAR* Cmd, FOutputDevice& Ar)
{
	return false;
}

FString FOnlineSubsystemEOSPlus::GetAppId() const
{
	return BaseOSS != nullptr ? BaseOSS->GetAppId() : TEXT("");
}

FText FOnlineSubsystemEOSPlus::GetOnlineServiceName() const
{
	return NSLOCTEXT("OnlineSubsystemEOSPlus", "OnlineServiceName", "EOS_Plus");
}

bool FOnlineSubsystemEOSPlus::Init()
{
#if PLATFORM_DESKTOP
	FString BaseOSSName;
	GConfig->GetString(TEXT("[OnlineSubsystemEOSPlus]"), TEXT("BaseOSSName"), BaseOSSName, GEngineIni);
	BaseOSS = BaseOSSName.IsEmpty() ? IOnlineSubsystem::GetByPlatform() : IOnlineSubsystem::Get(FName(*BaseOSSName));
#else
	BaseOSS = IOnlineSubsystem::GetByPlatform();
#endif
	if (BaseOSS != nullptr)
	{
		UE_LOG_ONLINE(Error, TEXT("FOnlineSubsystemEOSPlus::Init() failed to get the platform OSS"));
		return false;
	}
	EosOSS = IOnlineSubsystem::Get(EOS_SUBSYSTEM);
	if (EosOSS != nullptr)
	{
		UE_LOG_ONLINE(Error, TEXT("FOnlineSubsystemEOSPlus::Init() failed to get the EOS OSS"));
		return false;
	}

	StatsInterfacePtr = MakeShareable(new FOnlineStatsEOSPlus(this));
	AchievementsInterfacePtr = MakeShareable(new FOnlineAchievementsEOSPlus(this));
	FriendsInterfacePtr = MakeShareable(new FOnlineFriendsEOSPlus(this));

	return true;
}

bool FOnlineSubsystemEOSPlus::Shutdown()
{
	BaseOSS = nullptr;
	EosOSS = nullptr;

#define DESTRUCT_INTERFACE(Interface) \
	if (Interface.IsValid()) \
	{ \
		ensure(Interface.IsUnique()); \
		Interface = nullptr; \
	}

	DESTRUCT_INTERFACE(StatsInterfacePtr);
	DESTRUCT_INTERFACE(AchievementsInterfacePtr);

#undef DESTRUCT_INTERFACE

	return true;
}

IOnlineSessionPtr FOnlineSubsystemEOSPlus::GetSessionInterface() const
{
	return BaseOSS != nullptr ? BaseOSS->GetSessionInterface() : nullptr;
}

IOnlineFriendsPtr FOnlineSubsystemEOSPlus::GetFriendsInterface() const
{
	return FriendsInterfacePtr;
}

IOnlineGroupsPtr FOnlineSubsystemEOSPlus::GetGroupsInterface() const
{
	return BaseOSS != nullptr ? BaseOSS->GetGroupsInterface() : nullptr;
}

IOnlinePartyPtr FOnlineSubsystemEOSPlus::GetPartyInterface() const
{
	return BaseOSS != nullptr ? BaseOSS->GetPartyInterface() : nullptr;
}

IOnlineSharedCloudPtr FOnlineSubsystemEOSPlus::GetSharedCloudInterface() const
{
	return BaseOSS != nullptr ? BaseOSS->GetSharedCloudInterface() : nullptr;
}

IOnlineUserCloudPtr FOnlineSubsystemEOSPlus::GetUserCloudInterface() const
{
	return BaseOSS != nullptr ? BaseOSS->GetUserCloudInterface() : nullptr;
}

IOnlineEntitlementsPtr FOnlineSubsystemEOSPlus::GetEntitlementsInterface() const
{
	return BaseOSS != nullptr ? BaseOSS->GetEntitlementsInterface() : nullptr;
}

IOnlineLeaderboardsPtr FOnlineSubsystemEOSPlus::GetLeaderboardsInterface() const
{
	return BaseOSS != nullptr ? BaseOSS->GetLeaderboardsInterface() : nullptr;
}

IOnlineVoicePtr FOnlineSubsystemEOSPlus::GetVoiceInterface() const
{
	return BaseOSS != nullptr ? BaseOSS->GetVoiceInterface() : nullptr;
}

IOnlineExternalUIPtr FOnlineSubsystemEOSPlus::GetExternalUIInterface() const
{
	return BaseOSS != nullptr ? BaseOSS->GetExternalUIInterface() : nullptr;
}

IOnlineTimePtr FOnlineSubsystemEOSPlus::GetTimeInterface() const
{
	return BaseOSS != nullptr ? BaseOSS->GetTimeInterface() : nullptr;
}

IOnlineIdentityPtr FOnlineSubsystemEOSPlus::GetIdentityInterface() const
{
	return BaseOSS != nullptr ? BaseOSS->GetIdentityInterface() : nullptr;
}

IOnlineTitleFilePtr FOnlineSubsystemEOSPlus::GetTitleFileInterface() const
{
	return BaseOSS != nullptr ? BaseOSS->GetTitleFileInterface() : nullptr;
}

IOnlineStoreV2Ptr FOnlineSubsystemEOSPlus::GetStoreV2Interface() const
{
	return BaseOSS != nullptr ? BaseOSS->GetStoreV2Interface() : nullptr;
}

IOnlinePurchasePtr FOnlineSubsystemEOSPlus::GetPurchaseInterface() const
{
	return BaseOSS != nullptr ? BaseOSS->GetPurchaseInterface() : nullptr;
}

IOnlineEventsPtr FOnlineSubsystemEOSPlus::GetEventsInterface() const
{
	return BaseOSS != nullptr ? BaseOSS->GetEventsInterface() : nullptr;
}

IOnlineAchievementsPtr FOnlineSubsystemEOSPlus::GetAchievementsInterface() const
{
	return AchievementsInterfacePtr;
}

IOnlineSharingPtr FOnlineSubsystemEOSPlus::GetSharingInterface() const
{
	return BaseOSS != nullptr ? BaseOSS->GetSharingInterface() : nullptr;
}

IOnlineUserPtr FOnlineSubsystemEOSPlus::GetUserInterface() const
{
	return BaseOSS != nullptr ? BaseOSS->GetUserInterface() : nullptr;
}

IOnlineMessagePtr FOnlineSubsystemEOSPlus::GetMessageInterface() const
{
	return BaseOSS != nullptr ? BaseOSS->GetMessageInterface() : nullptr;
}

IOnlinePresencePtr FOnlineSubsystemEOSPlus::GetPresenceInterface() const
{
	return BaseOSS != nullptr ? BaseOSS->GetPresenceInterface() : nullptr;
}

IOnlineChatPtr FOnlineSubsystemEOSPlus::GetChatInterface() const
{
	return BaseOSS != nullptr ? BaseOSS->GetChatInterface() : nullptr;
}

IOnlineStatsPtr FOnlineSubsystemEOSPlus::GetStatsInterface() const
{
	return StatsInterfacePtr;
}

IOnlineTurnBasedPtr FOnlineSubsystemEOSPlus::GetTurnBasedInterface() const
{
	return BaseOSS != nullptr ? BaseOSS->GetTurnBasedInterface() : nullptr;
}

IOnlineTournamentPtr FOnlineSubsystemEOSPlus::GetTournamentInterface() const
{
	return BaseOSS != nullptr ? BaseOSS->GetTournamentInterface() : nullptr;
}
