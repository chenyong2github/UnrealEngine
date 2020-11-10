// Copyright Epic Games, Inc. All Rights Reserved.

#include "OnlinePresenceEOSPlus.h"
#include "OnlineSubsystem.h"
#include "OnlineSubsystemEOSPlus.h"
#include "EOSSettings.h"

FOnlinePresenceEOSPlus::FOnlinePresenceEOSPlus(FOnlineSubsystemEOSPlus* InSubsystem)
	: EOSPlus(InSubsystem)
{
	BasePresenceInterface = EOSPlus->BaseOSS->GetPresenceInterface();
	check(BasePresenceInterface.IsValid());
	EOSPresenceInterface = EOSPlus->EosOSS->GetPresenceInterface();
	check(EOSPresenceInterface.IsValid());

	// Only rebroadcast the platform notifications
	BasePresenceInterface->AddOnPresenceReceivedDelegate_Handle(FOnPresenceReceivedDelegate::CreateRaw(this, &FOnlinePresenceEOSPlus::OnPresenceReceived));
	BasePresenceInterface->AddOnPresenceArrayUpdatedDelegate_Handle(FOnPresenceArrayUpdatedDelegate::CreateRaw(this, &FOnlinePresenceEOSPlus::OnPresenceArrayUpdated));
}

FOnlinePresenceEOSPlus::~FOnlinePresenceEOSPlus()
{
	BasePresenceInterface->ClearOnPresenceReceivedDelegates(this);
	BasePresenceInterface->ClearOnPresenceArrayUpdatedDelegates(this);
}

void FOnlinePresenceEOSPlus::OnPresenceReceived(const FUniqueNetId& UserId, const TSharedRef<FOnlineUserPresence>& Presence)
{
	TriggerOnPresenceReceivedDelegates(UserId, Presence);
}

void FOnlinePresenceEOSPlus::OnPresenceArrayUpdated(const FUniqueNetId& UserId, const TArray<TSharedRef<FOnlineUserPresence>>& NewPresenceArray)
{
	TriggerOnPresenceArrayUpdatedDelegates(UserId, NewPresenceArray);
}

void FOnlinePresenceEOSPlus::SetPresence(const FUniqueNetId& User, const FOnlineUserPresenceStatus& Status, const FOnPresenceTaskCompleteDelegate& Delegate)
{
	BasePresenceInterface->SetPresence(User, Status,
		FOnPresenceTaskCompleteDelegate::CreateLambda([this, StatusCopy = FOnlineUserPresenceStatus(Status), IntermediateComplete = FOnPresenceTaskCompleteDelegate(Delegate)](const FUniqueNetId& UserId, const bool bWasSuccessful)
	{
		// Skip setting EAS presence if not mirrored or if we errored at the platform level
		if (!GetDefault<UEOSSettings>()->bMirrorPresenceToEAS || !bWasSuccessful)
		{
			IntermediateComplete.ExecuteIfBound(UserId, bWasSuccessful);
			return;
		}
		// Set the EAS version too
		EOSPresenceInterface->SetPresence(UserId, StatusCopy,
			FOnPresenceTaskCompleteDelegate::CreateLambda([this, OnComplete = FOnPresenceTaskCompleteDelegate(IntermediateComplete)](const FUniqueNetId& UserId, const bool bWasSuccessful)
		{
			// The platform one is the one that matters so if we get here we succeeded earlier
			OnComplete.ExecuteIfBound(UserId, true);
		}));
	}));
}

void FOnlinePresenceEOSPlus::QueryPresence(const FUniqueNetId& User, const FOnPresenceTaskCompleteDelegate& Delegate)
{
	BasePresenceInterface->QueryPresence(User, Delegate);
}

EOnlineCachedResult::Type FOnlinePresenceEOSPlus::GetCachedPresence(const FUniqueNetId& User, TSharedPtr<FOnlineUserPresence>& OutPresence)
{
	return BasePresenceInterface->GetCachedPresence(User, OutPresence);
}

EOnlineCachedResult::Type FOnlinePresenceEOSPlus::GetCachedPresenceForApp(const FUniqueNetId& LocalUserId, const FUniqueNetId& User, const FString& AppId, TSharedPtr<FOnlineUserPresence>& OutPresence)
{
	return BasePresenceInterface->GetCachedPresenceForApp(LocalUserId, User, AppId, OutPresence);
}
