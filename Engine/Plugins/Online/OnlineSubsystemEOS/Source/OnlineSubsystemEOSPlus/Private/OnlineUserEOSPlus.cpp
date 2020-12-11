// Copyright Epic Games, Inc. All Rights Reserved.

#include "OnlineUserEOSPlus.h"
#include "OnlineSubsystem.h"
#include "OnlineSubsystemEOSPlus.h"
#include "EOSSettings.h"


FUniqueNetIdEOSPlus::FUniqueNetIdEOSPlus(TSharedPtr<const FUniqueNetId> InBaseUniqueNetId, TSharedPtr<const FUniqueNetId> InEOSUniqueNetId)
	: BaseUniqueNetId(InBaseUniqueNetId)
	, EOSUniqueNetId(InEOSUniqueNetId)
{
	int32 TotalBytes = GetSize();
	int32 Offset = 0;
	RawBytes.Empty(TotalBytes);
	if (BaseUniqueNetId.IsValid())
	{
		int32 BaseSize = BaseUniqueNetId->GetSize();
		FMemory::Memcpy(RawBytes.GetData(), BaseUniqueNetId->GetBytes(), BaseSize);
		Offset = BaseSize;
	}
	if (EOSUniqueNetId.IsValid())
	{
		int32 EOSSize = EOSUniqueNetId->GetSize();
		FMemory::Memcpy(RawBytes.GetData() + Offset, EOSUniqueNetId->GetBytes(), EOSSize);
	}
}

FName FUniqueNetIdEOSPlus::GetType() const
{
	return TEXT("EOSPlus");
}

const uint8* FUniqueNetIdEOSPlus::GetBytes() const
{
	return RawBytes.GetData();
}

int32 FUniqueNetIdEOSPlus::GetSize() const
{
	int32 Size = 0;
	if (BaseUniqueNetId.IsValid())
	{
		Size += BaseUniqueNetId->GetSize();
	}
	if (EOSUniqueNetId.IsValid())
	{
		Size += EOSUniqueNetId->GetSize();
	}
	return Size;
}

bool FUniqueNetIdEOSPlus::IsValid() const
{
	return BaseUniqueNetId.IsValid();
}

FString FUniqueNetIdEOSPlus::ToString() const
{
	FString Local;
	if (BaseUniqueNetId.IsValid())
	{
		Local += BaseUniqueNetId->ToString();
	}
	if (EOSUniqueNetId.IsValid())
	{
		Local += EOSUniqueNetId->ToString();
	}
	return Local;
}

FString FUniqueNetIdEOSPlus::ToDebugString() const
{
	FString Local;
	if (BaseUniqueNetId.IsValid())
	{
		Local += BaseUniqueNetId->ToDebugString();
	}
	if (EOSUniqueNetId.IsValid())
	{
		Local += EOSUniqueNetId->ToDebugString();
	}
	return Local;
}

FOnlineUserEOSPlus::FOnlineUserEOSPlus(FOnlineSubsystemEOSPlus* InSubsystem)
	: EOSPlus(InSubsystem)
{
	BaseUserInterface = EOSPlus->BaseOSS->GetUserInterface();
	BaseIdentityInterface = EOSPlus->BaseOSS->GetIdentityInterface();
	check(BaseUserInterface.IsValid() && BaseIdentityInterface.IsValid());
	EOSUserInterface = EOSPlus->EosOSS->GetUserInterface();
	EOSIdentityInterface = EOSPlus->EosOSS->GetIdentityInterface();
	check(EOSUserInterface.IsValid() && EOSIdentityInterface.IsValid());

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
		EOSUserInterface->GetExternalIdMappings(QueryOptions, ExternalIds, OutLocalIds);
		OutIds += OutLocalIds;
	}
}

TSharedPtr<const FUniqueNetId> FOnlineUserEOSPlus::GetExternalIdMapping(const FExternalIdQueryOptions& QueryOptions, const FString& ExternalId)
{
	TSharedPtr<const FUniqueNetId> MappedId = BaseUserInterface->GetExternalIdMapping(QueryOptions, ExternalId);

	if (!MappedId.IsValid() && (GetDefault<UEOSSettings>()->bUseEAS || GetDefault<UEOSSettings>()->bUseEOSConnect))
	{
		return EOSUserInterface->GetExternalIdMapping(QueryOptions, ExternalId);
	}

	return MappedId;
}

bool FOnlineUserEOSPlus::Login(int32 LocalUserNum, const FOnlineAccountCredentials& AccountCredentials)
{
	return BaseIdentityInterface->Login(LocalUserNum, AccountCredentials);
}

bool FOnlineUserEOSPlus::Logout(int32 LocalUserNum)
{
	EOSIdentityInterface->Logout(LocalUserNum);
	return BaseIdentityInterface->Logout(LocalUserNum);
}

bool FOnlineUserEOSPlus::AutoLogin(int32 LocalUserNum)
{
	return BaseIdentityInterface->AutoLogin(LocalUserNum);
}

TSharedPtr<FUserOnlineAccount> FOnlineUserEOSPlus::GetUserAccount(const FUniqueNetId& UserId) const
{
	//@todo joeg - decide whether we want to wrap both accounts into one or not
	return BaseIdentityInterface->GetUserAccount(UserId);
}

TArray<TSharedPtr<FUserOnlineAccount>> FOnlineUserEOSPlus::GetAllUserAccounts() const
{
	//@todo joeg - decide whether we want to wrap both accounts into one or not
	return BaseIdentityInterface->GetAllUserAccounts();
}

TSharedPtr<const FUniqueNetId> FOnlineUserEOSPlus::GetUniquePlayerId(int32 LocalUserNum) const
{
	return BaseIdentityInterface->GetUniquePlayerId(LocalUserNum);
}

TSharedPtr<const FUniqueNetId> FOnlineUserEOSPlus::CreateUniquePlayerId(uint8* Bytes, int32 Size)
{
	return BaseIdentityInterface->CreateUniquePlayerId(Bytes, Size);
}

TSharedPtr<const FUniqueNetId> FOnlineUserEOSPlus::CreateUniquePlayerId(const FString& Str)
{
	return BaseIdentityInterface->CreateUniquePlayerId(Str);
}

ELoginStatus::Type FOnlineUserEOSPlus::GetLoginStatus(int32 LocalUserNum) const
{
	return BaseIdentityInterface->GetLoginStatus(LocalUserNum);
}

ELoginStatus::Type FOnlineUserEOSPlus::GetLoginStatus(const FUniqueNetId& UserId) const
{
	return BaseIdentityInterface->GetLoginStatus(UserId);
}

FString FOnlineUserEOSPlus::GetPlayerNickname(int32 LocalUserNum) const
{
	return BaseIdentityInterface->GetPlayerNickname(LocalUserNum);
}

FString FOnlineUserEOSPlus::GetPlayerNickname(const FUniqueNetId& UserId) const
{
	// Do we wrap this and map or pass through or aggregate and pass through?
	return BaseIdentityInterface->GetPlayerNickname(UserId);
}

FString FOnlineUserEOSPlus::GetAuthToken(int32 LocalUserNum) const
{
	return BaseIdentityInterface->GetAuthToken(LocalUserNum);
}

void FOnlineUserEOSPlus::GetUserPrivilege(const FUniqueNetId& UserId, EUserPrivileges::Type Privilege, const FOnGetUserPrivilegeCompleteDelegate& Delegate)
{
	BaseIdentityInterface->GetUserPrivilege(UserId, Privilege, Delegate);
}

FString FOnlineUserEOSPlus::GetAuthType() const
{
	return BaseIdentityInterface->GetAuthType();
}

void FOnlineUserEOSPlus::RevokeAuthToken(const FUniqueNetId& LocalUserId, const FOnRevokeAuthTokenCompleteDelegate& Delegate)
{
	BaseIdentityInterface->RevokeAuthToken(LocalUserId, Delegate);
}

FPlatformUserId FOnlineUserEOSPlus::GetPlatformUserIdFromUniqueNetId(const FUniqueNetId& UniqueNetId) const
{
	return BaseIdentityInterface->GetPlatformUserIdFromUniqueNetId(UniqueNetId);
}
