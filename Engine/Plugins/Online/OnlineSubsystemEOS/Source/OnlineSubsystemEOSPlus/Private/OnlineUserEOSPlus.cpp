// Copyright Epic Games, Inc. All Rights Reserved.

#include "OnlineUserEOSPlus.h"
#include "OnlineSubsystem.h"
#include "OnlineSubsystemEOSPlus.h"
#include "EOSSettings.h"

FOnlineUserEOSPlus::FOnlineUserEOSPlus(FOnlineSubsystemEOSPlus* InSubsystem)
	: EOSPlus(InSubsystem)
{
	BaseUserInterface = EOSPlus->BaseOSS->GetUserInterface();
	check(BaseUserInterface.IsValid());
	EOSUserInterface = EOSPlus->EosOSS->GetUserInterface();
	check(EOSUserInterface.IsValid());

	IntermediateOnQueryUserInfoCompleteDelegateHandle = FOnQueryUserInfoCompleteDelegate::CreateRaw(this, &FOnlineUserEOSPlus::IntermediateOnQueryUserInfoComplete);
	FinalOnQueryUserInfoCompleteDelegateHandle = FOnQueryUserInfoCompleteDelegate::CreateRaw(this, &FOnlineUserEOSPlus::FinalOnQueryUserInfoComplete);
}

FOnlineUserEOSPlus::~FOnlineUserEOSPlus()
{
	for (int32 LocalUserNum = 0; LocalUserNum < MAX_LOCAL_PLAYERS; LocalUserNum++)
	{
		BaseUserInterface->ClearOnQueryUserInfoCompleteDelegates(LocalUserNum, this);
		EOSUserInterface->ClearOnQueryUserInfoCompleteDelegates(LocalUserNum, this);
	}
}


bool FOnlineUserEOSPlus::QueryUserInfo(int32 LocalUserNum, const TArray<TSharedRef<const FUniqueNetId>>& UserIds)
{
	if (GetDefault<UEOSSettings>()->bUseEAS || GetDefault<UEOSSettings>()->bUseEOSConnect)
	{
		// Register the intermediate delegate with the base oss
		BaseUserInterface->AddOnQueryUserInfoCompleteDelegate_Handle(LocalUserNum, IntermediateOnQueryUserInfoCompleteDelegateHandle);
	}
	else
	{
		// Register the final with the base oss
		BaseUserInterface->AddOnQueryUserInfoCompleteDelegate_Handle(LocalUserNum, FinalOnQueryUserInfoCompleteDelegateHandle);
	}

	return BaseUserInterface->QueryUserInfo(LocalUserNum, UserIds);
}

void FOnlineUserEOSPlus::IntermediateOnQueryUserInfoComplete(int32 LocalUserNum, bool bWasSuccessful, const TArray<TSharedRef<const FUniqueNetId>>& UserIds, const FString& ErrorStr)
{
	BaseUserInterface->ClearOnQueryUserInfoCompleteDelegates(LocalUserNum, this);

	if (!bWasSuccessful)
	{
		// Skip EOS and notify
		TriggerOnQueryUserInfoCompleteDelegates(LocalUserNum, bWasSuccessful, UserIds, ErrorStr);
		return;
	}
	// Query the EOS OSS now
	EOSUserInterface->AddOnQueryUserInfoCompleteDelegate_Handle(LocalUserNum, FinalOnQueryUserInfoCompleteDelegateHandle);
	EOSUserInterface->QueryUserInfo(LocalUserNum, UserIds);
}

void FOnlineUserEOSPlus::FinalOnQueryUserInfoComplete(int32 LocalUserNum, bool bWasSuccessful, const TArray<TSharedRef<const FUniqueNetId>>& UserIds, const FString& ErrorStr)
{
	BaseUserInterface->ClearOnQueryUserInfoCompleteDelegates(LocalUserNum, this);
	EOSUserInterface->ClearOnQueryUserInfoCompleteDelegates(LocalUserNum, this);

	// Notify anyone listening
	TriggerOnQueryUserInfoCompleteDelegates(LocalUserNum, bWasSuccessful, UserIds, ErrorStr);
}

bool FOnlineUserEOSPlus::GetAllUserInfo(int32 LocalUserNum, TArray<TSharedRef<FOnlineUser>>& OutUsers)
{
	OutUsers.Reset();

	TArray<TSharedRef<FOnlineUser>> OutLocalUsers;
	bool bWasSuccessful = BaseUserInterface->GetAllUserInfo(LocalUserNum, OutLocalUsers);
	OutUsers += OutLocalUsers;

	if (GetDefault<UEOSSettings>()->bUseEAS || GetDefault<UEOSSettings>()->bUseEOSConnect)
	{
		OutLocalUsers.Reset();
		bWasSuccessful = BaseUserInterface->GetAllUserInfo(LocalUserNum, OutLocalUsers);
		OutUsers += OutLocalUsers;
	}

	return bWasSuccessful;
}

TSharedPtr<FOnlineUser> FOnlineUserEOSPlus::GetUserInfo(int32 LocalUserNum, const FUniqueNetId& UserId)
{
	// @todo joeg - should we build a single user here or just use the platform or merge the info as part of the identity interface process?
	return BaseUserInterface->GetUserInfo(LocalUserNum, UserId);
}

bool FOnlineUserEOSPlus::QueryUserIdMapping(const FUniqueNetId& UserId, const FString& DisplayNameOrEmail, const FOnQueryUserMappingComplete& Delegate)
{
	BaseUserInterface->QueryUserIdMapping(UserId, DisplayNameOrEmail,
		FOnQueryUserMappingComplete::CreateLambda([this, IntermediateComplete = FOnQueryUserMappingComplete(Delegate)](bool bWasSuccessful, const FUniqueNetId& UserId, const FString& DisplayNameOrEmail, const FUniqueNetId& FoundUserId, const FString& Error)
		{
			if (bWasSuccessful || (!GetDefault<UEOSSettings>()->bUseEAS && !GetDefault<UEOSSettings>()->bUseEOSConnect))
			{
				IntermediateComplete.ExecuteIfBound(bWasSuccessful, UserId, DisplayNameOrEmail, FoundUserId, Error);
				return;
			}
			EOSUserInterface->QueryUserIdMapping(UserId, DisplayNameOrEmail,
				FOnQueryUserMappingComplete::CreateLambda([this, OnComplete = FOnQueryUserMappingComplete(IntermediateComplete)](bool bWasSuccessful, const FUniqueNetId& UserId, const FString& DisplayNameOrEmail, const FUniqueNetId& FoundUserId, const FString& Error)
			{
				OnComplete.ExecuteIfBound(bWasSuccessful, UserId, DisplayNameOrEmail, FoundUserId, Error);
			}));
		}));
	return true;
}

bool FOnlineUserEOSPlus::QueryExternalIdMappings(const FUniqueNetId& UserId, const FExternalIdQueryOptions& QueryOptions, const TArray<FString>& ExternalIds, const FOnQueryExternalIdMappingsComplete& Delegate)
{
	BaseUserInterface->QueryExternalIdMappings(UserId, QueryOptions, ExternalIds,
		FOnQueryExternalIdMappingsComplete::CreateLambda([this, IntermediateComplete = FOnQueryExternalIdMappingsComplete(Delegate)](bool bWasSuccessful, const FUniqueNetId& UserId, const FExternalIdQueryOptions& QueryOptions, const TArray<FString>& ExternalIds, const FString& Error)
	{
		if (bWasSuccessful || (!GetDefault<UEOSSettings>()->bUseEAS && !GetDefault<UEOSSettings>()->bUseEOSConnect))
		{
			IntermediateComplete.ExecuteIfBound(bWasSuccessful, UserId, QueryOptions, ExternalIds, Error);
			return;
		}
		EOSUserInterface->QueryExternalIdMappings(UserId, QueryOptions, ExternalIds,
			FOnQueryExternalIdMappingsComplete::CreateLambda([this, OnComplete = FOnQueryExternalIdMappingsComplete(IntermediateComplete)](bool bWasSuccessful, const FUniqueNetId& UserId, const FExternalIdQueryOptions& QueryOptions, const TArray<FString>& ExternalIds, const FString& Error)
		{
			OnComplete.ExecuteIfBound(bWasSuccessful, UserId, QueryOptions, ExternalIds, Error);
		}));
	}));
	return true;
}

void FOnlineUserEOSPlus::GetExternalIdMappings(const FExternalIdQueryOptions& QueryOptions, const TArray<FString>& ExternalIds, TArray<TSharedPtr<const FUniqueNetId>>& OutIds)
{
	OutIds.Reset();

	TArray<TSharedPtr<const FUniqueNetId>> OutLocalIds;
	BaseUserInterface->GetExternalIdMappings(QueryOptions, ExternalIds, OutLocalIds);
	OutIds += OutLocalIds;

	if (GetDefault<UEOSSettings>()->bUseEAS || GetDefault<UEOSSettings>()->bUseEOSConnect)
	{
		OutLocalIds.Reset();
		BaseUserInterface->GetExternalIdMappings(QueryOptions, ExternalIds, OutLocalIds);
		OutIds += OutLocalIds;
	}
}

TSharedPtr<const FUniqueNetId> FOnlineUserEOSPlus::GetExternalIdMapping(const FExternalIdQueryOptions& QueryOptions, const FString& ExternalId)
{
	// @todo joeg - should we build a single user here or just use the platform or merge the info as part of the identity interface process?
	return BaseUserInterface->GetExternalIdMapping(QueryOptions, ExternalId);
}
