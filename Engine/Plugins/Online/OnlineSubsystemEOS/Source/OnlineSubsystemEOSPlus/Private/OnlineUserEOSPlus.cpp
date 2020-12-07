// Copyright Epic Games, Inc. All Rights Reserved.

#include "OnlineUserEOSPlus.h"
#include "OnlineSubsystem.h"
#include "OnlineSubsystemEOSPlus.h"
#include "EOSSettings.h"

inline FString BuildEOSPlusStringId(TSharedPtr<const FUniqueNetId> InBaseUniqueNetId, TSharedPtr<const FUniqueNetId> InEOSUniqueNetId)
{
	FString StrId = InBaseUniqueNetId.IsValid() ? InBaseUniqueNetId->ToString() : TEXT("");
	StrId += TEXT("_+_");
	StrId += InEOSUniqueNetId.IsValid() ? InEOSUniqueNetId->ToString() : TEXT(""), TEXT("EOSPlus");
	return StrId;
}

FUniqueNetIdEOSPlus::FUniqueNetIdEOSPlus(TSharedPtr<const FUniqueNetId> InBaseUniqueNetId, TSharedPtr<const FUniqueNetId> InEOSUniqueNetId)
	: FUniqueNetIdString(BuildEOSPlusStringId(InBaseUniqueNetId, InEOSUniqueNetId))
	, BaseUniqueNetId(InBaseUniqueNetId)
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

FOnlineUserEOSPlus::FOnlineUserEOSPlus(FOnlineSubsystemEOSPlus* InSubsystem)
	: EOSPlus(InSubsystem)
{
	BaseUserInterface = EOSPlus->BaseOSS->GetUserInterface();
	BaseIdentityInterface = EOSPlus->BaseOSS->GetIdentityInterface();
	check(BaseUserInterface.IsValid() && BaseIdentityInterface.IsValid());
	EOSUserInterface = EOSPlus->EosOSS->GetUserInterface();
	EOSIdentityInterface = EOSPlus->EosOSS->GetIdentityInterface();
	check(EOSUserInterface.IsValid() && EOSIdentityInterface.IsValid());
	BaseFriendsInterface = EOSPlus->BaseOSS->GetFriendsInterface();
	check(BaseFriendsInterface.IsValid());
	EOSFriendsInterface = EOSPlus->EosOSS->GetFriendsInterface();
	check(EOSFriendsInterface.IsValid());
	BasePresenceInterface = EOSPlus->BaseOSS->GetPresenceInterface();
	check(BasePresenceInterface.IsValid());
	EOSPresenceInterface = EOSPlus->EosOSS->GetPresenceInterface();
	check(EOSPresenceInterface.IsValid());

	BaseFriendsInterface->AddOnInviteReceivedDelegate_Handle(FOnInviteReceivedDelegate::CreateRaw(this, &FOnlineUserEOSPlus::OnInviteReceived));
	BaseFriendsInterface->AddOnInviteAcceptedDelegate_Handle(FOnInviteAcceptedDelegate::CreateRaw(this, &FOnlineUserEOSPlus::OnInviteAccepted));
	BaseFriendsInterface->AddOnInviteRejectedDelegate_Handle(FOnInviteRejectedDelegate::CreateRaw(this, &FOnlineUserEOSPlus::OnInviteRejected));
	BaseFriendsInterface->AddOnInviteAbortedDelegate_Handle(FOnInviteAbortedDelegate::CreateRaw(this, &FOnlineUserEOSPlus::OnInviteAborted));
	BaseFriendsInterface->AddOnFriendRemovedDelegate_Handle(FOnFriendRemovedDelegate::CreateRaw(this, &FOnlineUserEOSPlus::OnFriendRemoved));
	EOSFriendsInterface->AddOnInviteReceivedDelegate_Handle(FOnInviteReceivedDelegate::CreateRaw(this, &FOnlineUserEOSPlus::OnInviteReceived));
	EOSFriendsInterface->AddOnInviteAcceptedDelegate_Handle(FOnInviteAcceptedDelegate::CreateRaw(this, &FOnlineUserEOSPlus::OnInviteAccepted));
	EOSFriendsInterface->AddOnInviteRejectedDelegate_Handle(FOnInviteRejectedDelegate::CreateRaw(this, &FOnlineUserEOSPlus::OnInviteRejected));
	EOSFriendsInterface->AddOnInviteAbortedDelegate_Handle(FOnInviteAbortedDelegate::CreateRaw(this, &FOnlineUserEOSPlus::OnInviteAborted));
	EOSFriendsInterface->AddOnFriendRemovedDelegate_Handle(FOnFriendRemovedDelegate::CreateRaw(this, &FOnlineUserEOSPlus::OnFriendRemoved));
	// Only rebroadcast the platform notifications
	BasePresenceInterface->AddOnPresenceReceivedDelegate_Handle(FOnPresenceReceivedDelegate::CreateRaw(this, &FOnlineUserEOSPlus::OnPresenceReceived));
	BasePresenceInterface->AddOnPresenceArrayUpdatedDelegate_Handle(FOnPresenceArrayUpdatedDelegate::CreateRaw(this, &FOnlineUserEOSPlus::OnPresenceArrayUpdated));

	IntermediateOnQueryUserInfoCompleteDelegateHandle = FOnQueryUserInfoCompleteDelegate::CreateRaw(this, &FOnlineUserEOSPlus::IntermediateOnQueryUserInfoComplete);
	FinalOnQueryUserInfoCompleteDelegateHandle = FOnQueryUserInfoCompleteDelegate::CreateRaw(this, &FOnlineUserEOSPlus::FinalOnQueryUserInfoComplete);

	BaseIdentityInterface->AddOnLoginChangedDelegate_Handle(FOnLoginChangedDelegate::CreateRaw(this, &FOnlineUserEOSPlus::OnLoginChanged));
	BaseIdentityInterface->AddOnControllerPairingChangedDelegate_Handle(FOnControllerPairingChangedDelegate::CreateRaw(this, &FOnlineUserEOSPlus::OnControllerPairingChanged));
	for (int32 LocalUserNum = 0; LocalUserNum < MAX_LOCAL_PLAYERS; LocalUserNum++)
	{
		BaseIdentityInterface->AddOnLoginStatusChangedDelegate_Handle(LocalUserNum, FOnLoginStatusChangedDelegate::CreateRaw(this, &FOnlineUserEOSPlus::OnLoginStatusChanged));
		BaseIdentityInterface->AddOnLoginCompleteDelegate_Handle(LocalUserNum, FOnLoginCompleteDelegate::CreateRaw(this, &FOnlineUserEOSPlus::OnLoginComplete));
		BaseIdentityInterface->AddOnLogoutCompleteDelegate_Handle(LocalUserNum, FOnLogoutCompleteDelegate::CreateRaw(this, &FOnlineUserEOSPlus::OnLogoutComplete));

		BaseFriendsInterface->AddOnFriendsChangeDelegate_Handle(LocalUserNum, FOnFriendsChangeDelegate::CreateRaw(this, &FOnlineUserEOSPlus::OnFriendsChanged));
		EOSFriendsInterface->AddOnFriendsChangeDelegate_Handle(LocalUserNum, FOnFriendsChangeDelegate::CreateRaw(this, &FOnlineUserEOSPlus::OnFriendsChanged));
		BaseFriendsInterface->AddOnOutgoingInviteSentDelegate_Handle(LocalUserNum, FOnOutgoingInviteSentDelegate::CreateRaw(this, &FOnlineUserEOSPlus::OnOutgoingInviteSent));
		EOSFriendsInterface->AddOnOutgoingInviteSentDelegate_Handle(LocalUserNum, FOnOutgoingInviteSentDelegate::CreateRaw(this, &FOnlineUserEOSPlus::OnOutgoingInviteSent));
	}
}

FOnlineUserEOSPlus::~FOnlineUserEOSPlus()
{
	BaseIdentityInterface->ClearOnLoginChangedDelegates(this);
	BaseIdentityInterface->ClearOnControllerPairingChangedDelegates(this);
	BaseFriendsInterface->ClearOnInviteReceivedDelegates(this);
	BaseFriendsInterface->ClearOnInviteAcceptedDelegates(this);
	BaseFriendsInterface->ClearOnInviteRejectedDelegates(this);
	BaseFriendsInterface->ClearOnInviteAbortedDelegates(this);
	BaseFriendsInterface->ClearOnFriendRemovedDelegates(this);
	EOSFriendsInterface->ClearOnInviteReceivedDelegates(this);
	EOSFriendsInterface->ClearOnInviteAcceptedDelegates(this);
	EOSFriendsInterface->ClearOnInviteRejectedDelegates(this);
	EOSFriendsInterface->ClearOnInviteAbortedDelegates(this);
	EOSFriendsInterface->ClearOnFriendRemovedDelegates(this);
	BasePresenceInterface->ClearOnPresenceReceivedDelegates(this);
	BasePresenceInterface->ClearOnPresenceArrayUpdatedDelegates(this);

	for (int32 LocalUserNum = 0; LocalUserNum < MAX_LOCAL_PLAYERS; LocalUserNum++)
	{
		BaseUserInterface->ClearOnQueryUserInfoCompleteDelegates(LocalUserNum, this);
		EOSUserInterface->ClearOnQueryUserInfoCompleteDelegates(LocalUserNum, this);
		BaseIdentityInterface->ClearOnLoginStatusChangedDelegates(LocalUserNum, this);
		BaseIdentityInterface->ClearOnLoginCompleteDelegates(LocalUserNum, this);
		BaseIdentityInterface->ClearOnLogoutCompleteDelegates(LocalUserNum, this);

		BaseFriendsInterface->ClearOnFriendsChangeDelegates(LocalUserNum, this);
		EOSFriendsInterface->ClearOnFriendsChangeDelegates(LocalUserNum, this);
		BaseFriendsInterface->ClearOnOutgoingInviteSentDelegates(LocalUserNum, this);
		EOSFriendsInterface->ClearOnOutgoingInviteSentDelegates(LocalUserNum, this);
	}
}

TSharedPtr<FUniqueNetIdEOSPlus> FOnlineUserEOSPlus::GetNetIdPlus(const FString& SourceId)
{
	if (BaseNetIdToNetIdPlus.Contains(SourceId))
	{
		return BaseNetIdToNetIdPlus[SourceId];
	}
	if (EOSNetIdToNetIdPlus.Contains(SourceId))
	{
		return EOSNetIdToNetIdPlus[SourceId];
	}
	return nullptr;
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
	if (NetIdPlusToUserMap.Contains(UserId.ToString()))
	{
		TSharedRef<FOnlineUserPlus> UserPlus = NetIdPlusToUserMap[UserId.ToString()];
		// Handle the user info from EOS coming later
		if (!UserPlus->IsEOSItemValid())
		{
			UserPlus->SetEOSItem(EOSUserInterface->GetUserInfo(LocalUserNum, UserId));
		}
		return UserPlus;
	}
	// Build a user
	TSharedPtr<FOnlineUser> BaseUser = BaseUserInterface->GetUserInfo(LocalUserNum, UserId);
	if (!BaseUser.IsValid())
	{
		return nullptr;
	}
	TSharedPtr<FOnlineUser> EOSUser = EOSUserInterface->GetUserInfo(LocalUserNum, UserId);
	TSharedRef<FOnlineUserPlus> UserPlus = MakeShared<FOnlineUserPlus>(BaseUser, EOSUser);
	NetIdPlusToUserMap.Add(UserId.ToString(), UserPlus);
	return UserPlus;
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

void FOnlineUserEOSPlus::OnLoginChanged(int32 LocalUserNum)
{
	bool bForward = GetDefault<UEOSSettings>()->bUseEAS || GetDefault<UEOSSettings>()->bUseEOSConnect;

	ELoginStatus::Type LoginStatus = BaseIdentityInterface->GetLoginStatus(LocalUserNum);
	if (LoginStatus == ELoginStatus::LoggedIn || LoginStatus == ELoginStatus::UsingLocalProfile)
	{
		// When the platform logs in we need to conditionally log into EAS/EOS
		if (bForward)
		{
			EOSIdentityInterface->AutoLogin(LocalUserNum);
		}
		else
		{
			AddPlayer(LocalUserNum);
			TriggerOnLoginChangedDelegates(LocalUserNum);
		}
	}
	else if (LoginStatus == ELoginStatus::NotLoggedIn)
	{
		// Log out of EAS/EOS if configured
		if (bForward)
		{
			Logout(LocalUserNum);
		}
		TriggerOnLoginChangedDelegates(LocalUserNum);
	}
}

void FOnlineUserEOSPlus::OnEOSLoginChanged(int32 LocalUserNum)
{
	if (!GetDefault<UEOSSettings>()->bUseEAS && !GetDefault<UEOSSettings>()->bUseEOSConnect)
	{
		return;
	}

	ELoginStatus::Type LoginStatus = EOSIdentityInterface->GetLoginStatus(LocalUserNum);
	if (LoginStatus == ELoginStatus::LoggedIn)
	{
		AddPlayer(LocalUserNum);
		TriggerOnLoginChangedDelegates(LocalUserNum);
	}
	else if (LoginStatus == ELoginStatus::NotLoggedIn)
	{
		// @todo joeg - should we force a logout of the platform? Things will be broken either way...
		//		Logout(LocalUserNum);
	}
}

void FOnlineUserEOSPlus::OnLoginStatusChanged(int32 LocalUserNum, ELoginStatus::Type OldStatus, ELoginStatus::Type NewStatus, const FUniqueNetId& NewId)
{
	if (NewStatus == ELoginStatus::UsingLocalProfile)
	{
		if (!GetDefault<UEOSSettings>()->bUseEAS && !GetDefault<UEOSSettings>()->bUseEOSConnect)
		{
			EOSIdentityInterface->Logout(LocalUserNum);
		}
		TriggerOnLoginStatusChangedDelegates(LocalUserNum, OldStatus, NewStatus, NewId);
	}
}

void FOnlineUserEOSPlus::OnControllerPairingChanged(int32 LocalUserNum, FControllerPairingChangedUserInfo PreviousUser, FControllerPairingChangedUserInfo NewUser)
{
	// @todo joeg - probably needs special handling here, though I think it should be covered by login change
	TriggerOnControllerPairingChangedDelegates(LocalUserNum, PreviousUser, NewUser);
}

void FOnlineUserEOSPlus::OnLoginComplete(int32 LocalUserNum, bool bWasSuccessful, const FUniqueNetId& UserId, const FString& Error)
{
	TSharedPtr<FUniqueNetIdEOSPlus> NetIdPlus = GetNetIdPlus(UserId.ToString());
	if (!NetIdPlus.IsValid())
	{
		return;
	}
	TriggerOnLoginCompleteDelegates(LocalUserNum, bWasSuccessful, *NetIdPlus, Error);
}

void FOnlineUserEOSPlus::OnLogoutComplete(int32 LocalUserNum, bool bWasSuccessful)
{
	TriggerOnLogoutCompleteDelegates(LocalUserNum, bWasSuccessful);
}

void FOnlineUserEOSPlus::AddPlayer(int32 LocalUserNum)
{
	if (LocalUserNumToNetIdPlus.Contains(LocalUserNum))
	{
		RemovePlayer(LocalUserNum);
	}

	TSharedPtr<const FUniqueNetId> BaseNetId = BaseIdentityInterface->GetUniquePlayerId(LocalUserNum);
	TSharedPtr<const FUniqueNetId> EOSNetId = EOSIdentityInterface->GetUniquePlayerId(LocalUserNum);
	TSharedPtr<FUniqueNetIdEOSPlus> PlusNetId = MakeShared<FUniqueNetIdEOSPlus>(BaseNetId, EOSNetId);

	BaseNetIdToNetIdPlus.Add(BaseNetId->ToString(), PlusNetId);
	EOSNetIdToNetIdPlus.Add(EOSNetId->ToString(), PlusNetId);
	NetIdPlusToBaseNetId.Add(PlusNetId->ToString(), BaseNetId);
	NetIdPlusToEOSNetId.Add(PlusNetId->ToString(), EOSNetId);
	LocalUserNumToNetIdPlus.Add(LocalUserNum, PlusNetId);

	// Add the local account
	TSharedPtr<FUserOnlineAccount> BaseAccount = BaseIdentityInterface->GetUserAccount(*BaseNetId);
	TSharedPtr<FUserOnlineAccount> EOSAccount = BaseIdentityInterface->GetUserAccount(*EOSNetId);
	TSharedRef<FOnlineUserAccountPlus> PlusAccount = MakeShared<FOnlineUserAccountPlus>(BaseAccount, EOSAccount);
	NetIdPlusToUserAccountMap.Add(PlusNetId->ToString(), PlusAccount);
}

void FOnlineUserEOSPlus::RemovePlayer(int32 LocalUserNum)
{
	if (!LocalUserNumToNetIdPlus.Contains(LocalUserNum))
	{
		// We don't know about this user
		return;
	}

	TSharedPtr<const FUniqueNetId> BaseNetId = BaseIdentityInterface->GetUniquePlayerId(LocalUserNum);
	TSharedPtr<const FUniqueNetId> EOSNetId = EOSIdentityInterface->GetUniquePlayerId(LocalUserNum);
	TSharedPtr<FUniqueNetIdEOSPlus> PlusNetId = LocalUserNumToNetIdPlus[LocalUserNum];

	// Remove the user account first
	TSharedPtr<FOnlineUserAccountPlus> PlusAccount = NetIdPlusToUserAccountMap[PlusNetId->ToString()];
	NetIdPlusToUserAccountMap.Remove(PlusNetId->ToString());

	// Clean up the net id caches
	BaseNetIdToNetIdPlus.Remove(BaseNetId->ToString());
	NetIdPlusToBaseNetId.Remove(PlusNetId->ToString());
	EOSNetIdToNetIdPlus.Remove(EOSNetId->ToString());
	NetIdPlusToEOSNetId.Remove(PlusNetId->ToString());
	LocalUserNumToNetIdPlus.Remove(LocalUserNum);
}

bool FOnlineUserEOSPlus::Logout(int32 LocalUserNum)
{
	// Clean up the cached data for this user
	RemovePlayer(LocalUserNum);

	EOSIdentityInterface->Logout(LocalUserNum);
	return BaseIdentityInterface->Logout(LocalUserNum);
}

bool FOnlineUserEOSPlus::AutoLogin(int32 LocalUserNum)
{
	return BaseIdentityInterface->AutoLogin(LocalUserNum);
}

TSharedPtr<FUserOnlineAccount> FOnlineUserEOSPlus::GetUserAccount(const FUniqueNetId& UserId) const
{
	if (NetIdPlusToUserAccountMap.Contains(UserId.ToString()))
	{
		return NetIdPlusToUserAccountMap[UserId.ToString()];
	}
	return nullptr;
}

TArray<TSharedPtr<FUserOnlineAccount>> FOnlineUserEOSPlus::GetAllUserAccounts() const
{
	TArray<TSharedPtr<FUserOnlineAccount>> Result;

	for (TMap<FString, TSharedRef<FOnlineUserAccountPlus>>::TConstIterator It(NetIdPlusToUserAccountMap); It; ++It)
	{
		Result.Add(It.Value());
	}
	return Result;
}

TSharedPtr<const FUniqueNetId> FOnlineUserEOSPlus::GetUniquePlayerId(int32 LocalUserNum) const
{
	if (LocalUserNumToNetIdPlus.Contains(LocalUserNum))
	{
		return LocalUserNumToNetIdPlus[LocalUserNum];
	}
	return nullptr;
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
	if (BaseNetIdToNetIdPlus.Contains(UniqueNetId.ToString()))
	{
		return BaseIdentityInterface->GetPlatformUserIdFromUniqueNetId(UniqueNetId);
	}
	if (NetIdPlusToBaseNetId.Contains(UniqueNetId.ToString()))
	{
		return BaseIdentityInterface->GetPlatformUserIdFromUniqueNetId(*NetIdPlusToBaseNetId[UniqueNetId.ToString()]);
	}
	return FPlatformUserId();
}

void FOnlineUserEOSPlus::OnFriendsChanged()
{
	for (int32 LocalUserNum = 0; LocalUserNum < MAX_LOCAL_PLAYERS; LocalUserNum++)
	{
		TriggerOnFriendsChangeDelegates(LocalUserNum);
	}
}

void FOnlineUserEOSPlus::OnOutgoingInviteSent()
{
	for (int32 LocalUserNum = 0; LocalUserNum < MAX_LOCAL_PLAYERS; LocalUserNum++)
	{
		TriggerOnOutgoingInviteSentDelegates(LocalUserNum);
	}
}

void FOnlineUserEOSPlus::OnInviteReceived(const FUniqueNetId& UserId, const FUniqueNetId& FriendId)
{
	TSharedPtr<FUniqueNetIdEOSPlus> NetIdPlus = GetNetIdPlus(UserId.ToString());
	if (!NetIdPlus.IsValid())
	{
		return;
	}
	TSharedPtr<FUniqueNetIdEOSPlus> FriendNetIdPlus = GetNetIdPlus(FriendId.ToString());
	if (!FriendNetIdPlus.IsValid())
	{
		return;
	}
	TriggerOnInviteReceivedDelegates(*NetIdPlus, *FriendNetIdPlus);
}

void FOnlineUserEOSPlus::OnInviteAccepted(const FUniqueNetId& UserId, const FUniqueNetId& FriendId)
{
	TSharedPtr<FUniqueNetIdEOSPlus> NetIdPlus = GetNetIdPlus(UserId.ToString());
	if (!NetIdPlus.IsValid())
	{
		return;
	}
	TSharedPtr<FUniqueNetIdEOSPlus> FriendNetIdPlus = GetNetIdPlus(FriendId.ToString());
	if (!FriendNetIdPlus.IsValid())
	{
		return;
	}
	TriggerOnInviteAcceptedDelegates(UserId, FriendId);
}

void FOnlineUserEOSPlus::OnInviteRejected(const FUniqueNetId& UserId, const FUniqueNetId& FriendId)
{
	TSharedPtr<FUniqueNetIdEOSPlus> NetIdPlus = GetNetIdPlus(UserId.ToString());
	if (!NetIdPlus.IsValid())
	{
		return;
	}
	TSharedPtr<FUniqueNetIdEOSPlus> FriendNetIdPlus = GetNetIdPlus(FriendId.ToString());
	if (!FriendNetIdPlus.IsValid())
	{
		return;
	}
	TriggerOnInviteRejectedDelegates(*NetIdPlus, *FriendNetIdPlus);
}

void FOnlineUserEOSPlus::OnInviteAborted(const FUniqueNetId& UserId, const FUniqueNetId& FriendId)
{
	TSharedPtr<FUniqueNetIdEOSPlus> NetIdPlus = GetNetIdPlus(UserId.ToString());
	if (!NetIdPlus.IsValid())
	{
		return;
	}
	TSharedPtr<FUniqueNetIdEOSPlus> FriendNetIdPlus = GetNetIdPlus(FriendId.ToString());
	if (!FriendNetIdPlus.IsValid())
	{
		return;
	}
	TriggerOnInviteAbortedDelegates(*NetIdPlus, *FriendNetIdPlus);
}

void FOnlineUserEOSPlus::OnFriendRemoved(const FUniqueNetId& UserId, const FUniqueNetId& FriendId)
{
	TSharedPtr<FUniqueNetIdEOSPlus> NetIdPlus = GetNetIdPlus(UserId.ToString());
	if (!NetIdPlus.IsValid())
	{
		return;
	}
	TSharedPtr<FUniqueNetIdEOSPlus> FriendNetIdPlus = GetNetIdPlus(FriendId.ToString());
	if (!FriendNetIdPlus.IsValid())
	{
		return;
	}
	TriggerOnFriendRemovedDelegates(*NetIdPlus, *FriendNetIdPlus);
}

bool FOnlineUserEOSPlus::ReadFriendsList(int32 LocalUserNum, const FString& ListName, const FOnReadFriendsListComplete& Delegate)
{
	return BaseFriendsInterface->ReadFriendsList(LocalUserNum, ListName,
		FOnReadFriendsListComplete::CreateLambda([this, IntermediateComplete = FOnReadFriendsListComplete(Delegate)](int32 LocalUserNum, bool bWasSuccessful, const FString& ListName, const FString& ErrorStr)
	{
		// Skip reading EAS if not in use and if we errored at the platform level
		if (!GetDefault<UEOSSettings>()->bUseEAS || !bWasSuccessful)
		{
			IntermediateComplete.ExecuteIfBound(LocalUserNum, bWasSuccessful, ListName, ErrorStr);
			return;
		}
		// Read the EAS version too
		EOSFriendsInterface->ReadFriendsList(LocalUserNum, ListName,
			FOnReadFriendsListComplete::CreateLambda([this, OnComplete = FOnReadFriendsListComplete(IntermediateComplete)](int32 LocalUserNum, bool bWasSuccessful, const FString& ListName, const FString& ErrorStr)
		{
			OnComplete.ExecuteIfBound(LocalUserNum, bWasSuccessful, ListName, ErrorStr);
		}));
	}));
}

bool FOnlineUserEOSPlus::DeleteFriendsList(int32 LocalUserNum, const FString& ListName, const FOnDeleteFriendsListComplete& Delegate)
{
	return BaseFriendsInterface->DeleteFriendsList(LocalUserNum, ListName, Delegate);
}

bool FOnlineUserEOSPlus::SendInvite(int32 LocalUserNum, const FUniqueNetId& FriendId, const FString& ListName, const FOnSendInviteComplete& Delegate)
{
	if (!NetIdPlusToBaseNetId.Contains(FriendId.ToString()))
	{
		return false;
	}
	return BaseFriendsInterface->SendInvite(LocalUserNum, *NetIdPlusToBaseNetId[FriendId.ToString()], ListName, Delegate);
}

bool FOnlineUserEOSPlus::AcceptInvite(int32 LocalUserNum, const FUniqueNetId& FriendId, const FString& ListName, const FOnAcceptInviteComplete& Delegate)
{
	if (!NetIdPlusToBaseNetId.Contains(FriendId.ToString()))
	{
		return false;
	}
	return BaseFriendsInterface->AcceptInvite(LocalUserNum, *NetIdPlusToBaseNetId[FriendId.ToString()], ListName, Delegate);
}

bool FOnlineUserEOSPlus::RejectInvite(int32 LocalUserNum, const FUniqueNetId& FriendId, const FString& ListName)
{
	if (!NetIdPlusToBaseNetId.Contains(FriendId.ToString()))
	{
		return false;
	}
	return BaseFriendsInterface->RejectInvite(LocalUserNum, *NetIdPlusToBaseNetId[FriendId.ToString()], ListName);
}

bool FOnlineUserEOSPlus::DeleteFriend(int32 LocalUserNum, const FUniqueNetId& FriendId, const FString& ListName)
{
	if (!NetIdPlusToBaseNetId.Contains(FriendId.ToString()))
	{
		return false;
	}
	return BaseFriendsInterface->DeleteFriend(LocalUserNum, *NetIdPlusToBaseNetId[FriendId.ToString()], ListName);
}

TSharedRef<FOnlineFriendPlus> FOnlineUserEOSPlus::AddFriend(TSharedRef<FOnlineFriend> Friend)
{
	TSharedPtr<FUniqueNetIdEOSPlus> NetIdPlus = nullptr;
	TSharedRef<const FUniqueNetId> NetId = Friend->GetUserId();
	if (NetId->GetType() == TEXT("EOS"))
	{
		// Grab or make a NetIdPlus
		if (EOSNetIdToNetIdPlus.Contains(NetId->ToString()))
		{
			NetIdPlus = EOSNetIdToNetIdPlus[NetId->ToString()];
		}
		else
		{
			NetIdPlus = MakeShared<FUniqueNetIdEOSPlus>(nullptr, NetId);
			EOSNetIdToNetIdPlus.Add(NetId->ToString(), NetIdPlus);
		}
		// Build a new friend plus and map them in
		TSharedRef<FOnlineFriendPlus> FriendPlus = MakeShared<FOnlineFriendPlus>(nullptr, Friend);
		NetIdPlusToFriendMap.Add(NetIdPlus->ToString(), FriendPlus);
		return FriendPlus;
	}
	// Grab or make a NetIdPlus
	if (BaseNetIdToNetIdPlus.Contains(NetId->ToString()))
	{
		NetIdPlus = BaseNetIdToNetIdPlus[NetId->ToString()];
	}
	else
	{
		NetIdPlus = MakeShared<FUniqueNetIdEOSPlus>(NetId, nullptr);
		BaseNetIdToNetIdPlus.Add(NetId->ToString(), NetIdPlus);
	}
	// Build a new friend plus and map them in
	TSharedRef<FOnlineFriendPlus> FriendPlus = MakeShared<FOnlineFriendPlus>(Friend, nullptr);
	NetIdPlusToFriendMap.Add(NetIdPlus->ToString(), FriendPlus);
	return FriendPlus;
}

TSharedRef<FOnlineFriendPlus> FOnlineUserEOSPlus::GetFriend(TSharedRef<FOnlineFriend> Friend)
{
	TSharedRef<const FUniqueNetId> NetId = Friend->GetUserId();
	if (NetIdPlusToFriendMap.Contains(NetId->ToString()))
	{
		return NetIdPlusToFriendMap[NetId->ToString()];
	}
	return AddFriend(Friend);
}

bool FOnlineUserEOSPlus::GetFriendsList(int32 LocalUserNum, const FString& ListName, TArray<TSharedRef<FOnlineFriend>>& OutFriends)
{
	OutFriends.Reset();

	TArray<TSharedRef<FOnlineFriend>> Friends;
	bool bWasSuccessful = BaseFriendsInterface->GetFriendsList(LocalUserNum, ListName, Friends);
	// Build the list of base friends
	for (TSharedRef<FOnlineFriend> Friend : Friends)
	{
		OutFriends.Add(GetFriend(Friend));
	}

	if (GetDefault<UEOSSettings>()->bUseEAS)
	{
		Friends.Reset();
		bWasSuccessful |= EOSFriendsInterface->GetFriendsList(LocalUserNum, ListName, Friends);
		// Build the list of EOS friends
		for (TSharedRef<FOnlineFriend> Friend : Friends)
		{
			OutFriends.Add(GetFriend(Friend));
		}
	}
	return bWasSuccessful;
}

TSharedPtr<FOnlineFriend> FOnlineUserEOSPlus::GetFriend(int32 LocalUserNum, const FUniqueNetId& FriendId, const FString& ListName)
{
	if (!NetIdPlusToFriendMap.Num())
	{
		TArray<TSharedRef<FOnlineFriend>> Friends;
		GetFriendsList(LocalUserNum, ListName, Friends);
	}

	if (FriendId.GetType() == TEXT("EOS"))
	{
		TSharedPtr<FOnlineFriend> Friend = EOSFriendsInterface->GetFriend(LocalUserNum, *NetIdPlusToBaseNetId[FriendId.ToString()], ListName);
		return Friend.IsValid() ? GetFriend(Friend.ToSharedRef()) : Friend;
	}
	TSharedPtr<FOnlineFriend> Friend = EOSFriendsInterface->GetFriend(LocalUserNum, *NetIdPlusToBaseNetId[FriendId.ToString()], ListName);
	return Friend.IsValid() ? GetFriend(Friend.ToSharedRef()) : Friend;
}

bool FOnlineUserEOSPlus::IsFriend(int32 LocalUserNum, const FUniqueNetId& FriendId, const FString& ListName)
{
	bool bIsFriend = false;
	if (NetIdPlusToBaseNetId.Contains(FriendId.ToString()))
	{
		bIsFriend = BaseFriendsInterface->IsFriend(LocalUserNum, *NetIdPlusToBaseNetId[FriendId.ToString()], ListName);
	}
	if (!bIsFriend && GetDefault<UEOSSettings>()->bUseEAS && NetIdPlusToEOSNetId.Contains(FriendId.ToString()))
	{
		bIsFriend = EOSFriendsInterface->IsFriend(LocalUserNum, *NetIdPlusToEOSNetId[FriendId.ToString()], ListName);
	}
	return bIsFriend;
}

bool FOnlineUserEOSPlus::QueryRecentPlayers(const FUniqueNetId& UserId, const FString& Namespace)
{
	if (!NetIdPlusToBaseNetId.Contains(UserId.ToString()))
	{
		return false;
	}
	return BaseFriendsInterface->QueryRecentPlayers(*NetIdPlusToBaseNetId[UserId.ToString()], Namespace);
}

TSharedRef<FOnlineRecentPlayer> FOnlineUserEOSPlus::AddRecentPlayer(TSharedRef<FOnlineRecentPlayer> Player)
{
	TSharedPtr<FUniqueNetIdEOSPlus> NetIdPlus = nullptr;
	TSharedRef<const FUniqueNetId> NetId = Player->GetUserId();
	if (NetId->GetType() == TEXT("EOS"))
	{
		// Grab or make a NetIdPlus
		if (EOSNetIdToNetIdPlus.Contains(NetId->ToString()))
		{
			NetIdPlus = EOSNetIdToNetIdPlus[NetId->ToString()];
		}
		else
		{
			NetIdPlus = MakeShared<FUniqueNetIdEOSPlus>(nullptr, NetId);
			EOSNetIdToNetIdPlus.Add(NetId->ToString(), NetIdPlus);
		}
		// Build a new recent player plus and map them in
		TSharedRef<FOnlineRecentPlayerPlus> PlayerPlus = MakeShared<FOnlineRecentPlayerPlus>(nullptr, Player);
		NetIdPlusToRecentPlayerMap.Add(NetIdPlus->ToString(), PlayerPlus);
		return PlayerPlus;
	}
	// Grab or make a NetIdPlus
	if (BaseNetIdToNetIdPlus.Contains(NetId->ToString()))
	{
		NetIdPlus = BaseNetIdToNetIdPlus[NetId->ToString()];
	}
	else
	{
		NetIdPlus = MakeShared<FUniqueNetIdEOSPlus>(NetId, nullptr);
		BaseNetIdToNetIdPlus.Add(NetId->ToString(), NetIdPlus);
	}
	// Build a new recent player plus and map them in
	TSharedRef<FOnlineRecentPlayerPlus> PlayerPlus = MakeShared<FOnlineRecentPlayerPlus>(Player, nullptr);
	NetIdPlusToRecentPlayerMap.Add(NetIdPlus->ToString(), PlayerPlus);
	return PlayerPlus;
}

TSharedRef<FOnlineRecentPlayer> FOnlineUserEOSPlus::GetRecentPlayer(TSharedRef<FOnlineRecentPlayer> Player)
{
	TSharedRef<const FUniqueNetId> NetId = Player->GetUserId();
	if (NetIdPlusToRecentPlayerMap.Contains(NetId->ToString()))
	{
		return NetIdPlusToRecentPlayerMap[NetId->ToString()];
	}
	return AddRecentPlayer(Player);
}

bool FOnlineUserEOSPlus::GetRecentPlayers(const FUniqueNetId& UserId, const FString& Namespace, TArray<TSharedRef<FOnlineRecentPlayer>>& OutRecentPlayers)
{
	OutRecentPlayers.Reset();

	if (!NetIdPlusToBaseNetId.Contains(UserId.ToString()))
	{
		return false;
	}

	TArray<TSharedRef<FOnlineRecentPlayer>> Players;
	bool bWasSuccessful = BaseFriendsInterface->GetRecentPlayers(*NetIdPlusToBaseNetId[UserId.ToString()], Namespace, Players);
	for (TSharedRef<FOnlineRecentPlayer> Player : Players)
	{
		OutRecentPlayers.Add(GetRecentPlayer(Player));
	}

	return bWasSuccessful;
}

bool FOnlineUserEOSPlus::BlockPlayer(int32 LocalUserNum, const FUniqueNetId& PlayerId)
{
	if (!NetIdPlusToBaseNetId.Contains(PlayerId.ToString()))
	{
		return false;
	}
	return BaseFriendsInterface->BlockPlayer(LocalUserNum, *NetIdPlusToBaseNetId[PlayerId.ToString()]);
}

bool FOnlineUserEOSPlus::UnblockPlayer(int32 LocalUserNum, const FUniqueNetId& PlayerId)
{
	if (!NetIdPlusToBaseNetId.Contains(PlayerId.ToString()))
	{
		return false;
	}
	return BaseFriendsInterface->UnblockPlayer(LocalUserNum, *NetIdPlusToBaseNetId[PlayerId.ToString()]);
}

bool FOnlineUserEOSPlus::QueryBlockedPlayers(const FUniqueNetId& UserId)
{
	if (!NetIdPlusToBaseNetId.Contains(UserId.ToString()))
	{
		return false;
	}
	return BaseFriendsInterface->QueryBlockedPlayers(*NetIdPlusToBaseNetId[UserId.ToString()]);
}

TSharedRef<FOnlineBlockedPlayer> FOnlineUserEOSPlus::AddBlockedPlayer(TSharedRef<FOnlineBlockedPlayer> Player)
{
	TSharedPtr<FUniqueNetIdEOSPlus> NetIdPlus = nullptr;
	TSharedRef<const FUniqueNetId> NetId = Player->GetUserId();
	if (NetId->GetType() == TEXT("EOS"))
	{
		if (EOSNetIdToNetIdPlus.Contains(NetId->ToString()))
		{
			NetIdPlus = EOSNetIdToNetIdPlus[NetId->ToString()];
		}
		else
		{
			NetIdPlus = MakeShared<FUniqueNetIdEOSPlus>(nullptr, NetId);
			EOSNetIdToNetIdPlus.Add(NetId->ToString(), NetIdPlus);
		}
		TSharedRef<FOnlineBlockedPlayerPlus> PlayerPlus = MakeShared<FOnlineBlockedPlayerPlus>(nullptr, Player);
		NetIdPlusToBlockedPlayerMap.Add(NetIdPlus->ToString(), PlayerPlus);
		return PlayerPlus;
	}
	if (BaseNetIdToNetIdPlus.Contains(NetId->ToString()))
	{
		NetIdPlus = BaseNetIdToNetIdPlus[NetId->ToString()];
	}
	else
	{
		NetIdPlus = MakeShared<FUniqueNetIdEOSPlus>(NetId, nullptr);
		BaseNetIdToNetIdPlus.Add(NetId->ToString(), NetIdPlus);
	}
	TSharedRef<FOnlineBlockedPlayerPlus> PlayerPlus = MakeShared<FOnlineBlockedPlayerPlus>(Player, nullptr);
	NetIdPlusToBlockedPlayerMap.Add(NetIdPlus->ToString(), PlayerPlus);
	return PlayerPlus;
}

TSharedRef<FOnlineBlockedPlayer> FOnlineUserEOSPlus::GetBlockedPlayer(TSharedRef<FOnlineBlockedPlayer> Player)
{
	TSharedRef<const FUniqueNetId> NetId = Player->GetUserId();
	if (NetIdPlusToBlockedPlayerMap.Contains(NetId->ToString()))
	{
		return NetIdPlusToBlockedPlayerMap[NetId->ToString()];
	}
	return AddBlockedPlayer(Player);
}

bool FOnlineUserEOSPlus::GetBlockedPlayers(const FUniqueNetId& UserId, TArray< TSharedRef<FOnlineBlockedPlayer> >& OutBlockedPlayers)
{
	OutBlockedPlayers.Reset();

	if (!NetIdPlusToBaseNetId.Contains(UserId.ToString()))
	{
		return false;
	}

	TArray<TSharedRef<FOnlineBlockedPlayer>> Players;
	bool bWasSuccessful = BaseFriendsInterface->GetBlockedPlayers(*NetIdPlusToBaseNetId[UserId.ToString()], Players);
	for (TSharedRef<FOnlineBlockedPlayer> Player : Players)
	{
		OutBlockedPlayers.Add(GetBlockedPlayer(Player));
	}
	return bWasSuccessful;
}

void FOnlineUserEOSPlus::DumpBlockedPlayers() const
{
	BaseFriendsInterface->DumpBlockedPlayers();
	EOSFriendsInterface->DumpBlockedPlayers();
}

void FOnlineUserEOSPlus::SetFriendAlias(int32 LocalUserNum, const FUniqueNetId& FriendId, const FString& ListName, const FString& Alias, const FOnSetFriendAliasComplete& Delegate)
{
	if (NetIdPlusToBaseNetId.Contains(FriendId.ToString()))
	{
		BaseFriendsInterface->SetFriendAlias(LocalUserNum, *NetIdPlusToBaseNetId[FriendId.ToString()], ListName, Alias, Delegate);
		return;
	}
	if (NetIdPlusToEOSNetId.Contains(FriendId.ToString()))
	{
		EOSFriendsInterface->SetFriendAlias(LocalUserNum, *NetIdPlusToEOSNetId[FriendId.ToString()], ListName, Alias, Delegate);
		return;
	}
	Delegate.ExecuteIfBound(LocalUserNum, FriendId, ListName, FOnlineError(false));
}

void FOnlineUserEOSPlus::DeleteFriendAlias(int32 LocalUserNum, const FUniqueNetId& FriendId, const FString& ListName, const FOnDeleteFriendAliasComplete& Delegate)
{
	if (NetIdPlusToBaseNetId.Contains(FriendId.ToString()))
	{
		BaseFriendsInterface->DeleteFriendAlias(LocalUserNum, *NetIdPlusToBaseNetId[FriendId.ToString()], ListName, Delegate);
		return;
	}
	if (NetIdPlusToEOSNetId.Contains(FriendId.ToString()))
	{
		EOSFriendsInterface->DeleteFriendAlias(LocalUserNum, *NetIdPlusToEOSNetId[FriendId.ToString()], ListName, Delegate);
		return;
	}
	Delegate.ExecuteIfBound(LocalUserNum, FriendId, ListName, FOnlineError(false));
}

void FOnlineUserEOSPlus::DumpRecentPlayers() const
{
	BaseFriendsInterface->DumpRecentPlayers();
	EOSFriendsInterface->DumpRecentPlayers();
}

void FOnlineUserEOSPlus::OnPresenceReceived(const FUniqueNetId& UserId, const TSharedRef<FOnlineUserPresence>& Presence)
{
	if (!BaseNetIdToNetIdPlus.Contains(UserId.ToString()))
	{
		return;
	}
	TriggerOnPresenceReceivedDelegates(*BaseNetIdToNetIdPlus[UserId.ToString()], Presence);
}

void FOnlineUserEOSPlus::OnPresenceArrayUpdated(const FUniqueNetId& UserId, const TArray<TSharedRef<FOnlineUserPresence>>& NewPresenceArray)
{
	if (!BaseNetIdToNetIdPlus.Contains(UserId.ToString()))
	{
		return;
	}
	TriggerOnPresenceArrayUpdatedDelegates(*BaseNetIdToNetIdPlus[UserId.ToString()], NewPresenceArray);
}

void FOnlineUserEOSPlus::SetPresence(const FUniqueNetId& User, const FOnlineUserPresenceStatus& Status, const FOnPresenceTaskCompleteDelegate& Delegate)
{
	if (!NetIdPlusToBaseNetId.Contains(User.ToString()))
	{
		Delegate.ExecuteIfBound(User, false);
		return;
	}
	BasePresenceInterface->SetPresence(*NetIdPlusToBaseNetId[User.ToString()], Status,
		FOnPresenceTaskCompleteDelegate::CreateLambda([this, StatusCopy = FOnlineUserPresenceStatus(Status), IntermediateComplete = FOnPresenceTaskCompleteDelegate(Delegate)](const FUniqueNetId& UserId, const bool bWasSuccessful)
	{
		// Skip setting EAS presence if not mirrored or if we errored at the platform level or the EOS user isn't found
		if (!GetDefault<UEOSSettings>()->bMirrorPresenceToEAS || !bWasSuccessful || !NetIdPlusToEOSNetId.Contains(UserId.ToString()))
		{
			IntermediateComplete.ExecuteIfBound(UserId, bWasSuccessful);
			return;
		}
		// Set the EAS version too
		EOSPresenceInterface->SetPresence(*NetIdPlusToEOSNetId[UserId.ToString()], StatusCopy,
			FOnPresenceTaskCompleteDelegate::CreateLambda([this, OnComplete = FOnPresenceTaskCompleteDelegate(IntermediateComplete)](const FUniqueNetId& UserId, const bool bWasSuccessful)
		{
			// The platform one is the one that matters so if we get here we succeeded earlier
			OnComplete.ExecuteIfBound(UserId, true);
		}));
	}));
}

void FOnlineUserEOSPlus::QueryPresence(const FUniqueNetId& User, const FOnPresenceTaskCompleteDelegate& Delegate)
{
	if (!NetIdPlusToBaseNetId.Contains(User.ToString()))
	{
		Delegate.ExecuteIfBound(User, false);
		return;
	}
	BasePresenceInterface->QueryPresence(*NetIdPlusToBaseNetId[User.ToString()], Delegate);
}

EOnlineCachedResult::Type FOnlineUserEOSPlus::GetCachedPresence(const FUniqueNetId& User, TSharedPtr<FOnlineUserPresence>& OutPresence)
{
	if (!NetIdPlusToBaseNetId.Contains(User.ToString()))
	{
		return EOnlineCachedResult::NotFound;
	}
	return BasePresenceInterface->GetCachedPresence(*NetIdPlusToBaseNetId[User.ToString()], OutPresence);
}

EOnlineCachedResult::Type FOnlineUserEOSPlus::GetCachedPresenceForApp(const FUniqueNetId& LocalUserId, const FUniqueNetId& User, const FString& AppId, TSharedPtr<FOnlineUserPresence>& OutPresence)
{
	if (!NetIdPlusToBaseNetId.Contains(LocalUserId.ToString()) || !NetIdPlusToBaseNetId.Contains(User.ToString()))
	{
		return EOnlineCachedResult::NotFound;
	}
	return BasePresenceInterface->GetCachedPresenceForApp(*NetIdPlusToBaseNetId[LocalUserId.ToString()], *NetIdPlusToBaseNetId[User.ToString()], AppId, OutPresence);
}
