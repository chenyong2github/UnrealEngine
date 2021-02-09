// Copyright Epic Games, Inc. All Rights Reserved.

#include "OnlineUserEOSPlus.h"
#include "OnlineSubsystem.h"
#include "OnlineSubsystemEOSPlus.h"
#include "EOSSettings.h"

// temp
#define EOS_ID_BYTE_SIZE 32

inline FString BuildEOSPlusStringId(TSharedPtr<const FUniqueNetId> InBaseUniqueNetId, TSharedPtr<const FUniqueNetId> InEOSUniqueNetId)
{
	FString StrId = InBaseUniqueNetId.IsValid() ? InBaseUniqueNetId->ToString() : TEXT("");
	StrId += TEXT("_+_");
	StrId += InEOSUniqueNetId.IsValid() ? InEOSUniqueNetId->ToString() : TEXT("");
	return StrId;
}

FUniqueNetIdEOSPlus::FUniqueNetIdEOSPlus(TSharedPtr<const FUniqueNetId> InBaseUniqueNetId, TSharedPtr<const FUniqueNetId> InEOSUniqueNetId)
	: FUniqueNetIdString(BuildEOSPlusStringId(InBaseUniqueNetId, InEOSUniqueNetId))
	, BaseUniqueNetId(InBaseUniqueNetId)
	, EOSUniqueNetId(InEOSUniqueNetId)
{
	int32 TotalBytes = GetSize();
	RawBytes.Empty(TotalBytes);
	RawBytes.AddZeroed(TotalBytes);

	if (EOSUniqueNetId.IsValid())
	{
		int32 EOSSize = EOSUniqueNetId->GetSize();
		FMemory::Memcpy(RawBytes.GetData(), EOSUniqueNetId->GetBytes(), EOSSize);
	}

	if (BaseUniqueNetId.IsValid())
	{
		int32 BaseSize = BaseUniqueNetId->GetSize();
		// Always copy above the EOS ID
		FMemory::Memcpy(RawBytes.GetData() + EOS_ID_BYTE_SIZE, BaseUniqueNetId->GetBytes(), BaseSize);
	}
}

const uint8* FUniqueNetIdEOSPlus::GetBytes() const
{
	return RawBytes.GetData();
}

int32 FUniqueNetIdEOSPlus::GetSize() const
{
	// Always account for EOS ID
	int32 Size = EOS_ID_BYTE_SIZE;
	if (BaseUniqueNetId.IsValid())
	{
		Size += BaseUniqueNetId->GetSize();
	}
	return Size;
}

bool FUniqueNetIdEOSPlus::IsValid() const
{
	return BaseUniqueNetId.IsValid() || EOSUniqueNetId.IsValid();
}

FOnlineUserEOSPlus::FOnlineUserEOSPlus(FOnlineSubsystemEOSPlus* InSubsystem)
	: EOSPlus(InSubsystem)
{
	BaseIdentityInterface = EOSPlus->BaseOSS->GetIdentityInterface();
	check(BaseIdentityInterface.IsValid());
	EOSIdentityInterface = EOSPlus->EosOSS->GetIdentityInterface();
	check(EOSIdentityInterface.IsValid());
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

	BaseIdentityInterface->AddOnLoginChangedDelegate_Handle(FOnLoginChangedDelegate::CreateRaw(this, &FOnlineUserEOSPlus::OnLoginChanged));
	BaseIdentityInterface->AddOnControllerPairingChangedDelegate_Handle(FOnControllerPairingChangedDelegate::CreateRaw(this, &FOnlineUserEOSPlus::OnControllerPairingChanged));
	for (int32 LocalUserNum = 0; LocalUserNum < MAX_LOCAL_PLAYERS; LocalUserNum++)
	{
		BaseIdentityInterface->AddOnLoginStatusChangedDelegate_Handle(LocalUserNum, FOnLoginStatusChangedDelegate::CreateRaw(this, &FOnlineUserEOSPlus::OnLoginStatusChanged));
		EOSIdentityInterface->AddOnLoginCompleteDelegate_Handle(LocalUserNum, FOnLoginCompleteDelegate::CreateRaw(this, &FOnlineUserEOSPlus::OnLoginComplete));
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
	if (NetIdPlusToNetIdPlus.Contains(SourceId))
	{
		return NetIdPlusToNetIdPlus[SourceId];
	}
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

TSharedPtr<const FUniqueNetId> FOnlineUserEOSPlus::GetBaseNetId(const FString& SourceId)
{
	if (NetIdPlusToBaseNetId.Contains(SourceId))
	{
		return NetIdPlusToBaseNetId[SourceId];
	}
	return nullptr;
}

TSharedPtr<const FUniqueNetId> FOnlineUserEOSPlus::GetEOSNetId(const FString& SourceId)
{
	if (NetIdPlusToEOSNetId.Contains(SourceId))
	{
		return NetIdPlusToEOSNetId[SourceId];
	}
	return nullptr;
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
	if (!bWasSuccessful)
	{
		return;
	}

	AddPlayer(LocalUserNum);
	TSharedPtr<FUniqueNetIdEOSPlus> NetIdPlus = LocalUserNumToNetIdPlus[LocalUserNum];
	check(NetIdPlus.IsValid());

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
	NetIdPlusToNetIdPlus.Add(PlusNetId->ToString(), PlusNetId);
	LocalUserNumToNetIdPlus.Add(LocalUserNum, PlusNetId);

	// Add the local account
	TSharedPtr<FUserOnlineAccount> BaseAccount = BaseIdentityInterface->GetUserAccount(*BaseNetId);
	TSharedPtr<FUserOnlineAccount> EOSAccount = EOSIdentityInterface->GetUserAccount(*EOSNetId);
	TSharedRef<FOnlineUserAccountPlus> PlusAccount = MakeShared<FOnlineUserAccountPlus>(BaseAccount, EOSAccount);
	NetIdPlusToUserAccountMap.Add(PlusNetId->ToString(), PlusAccount);
}

TSharedPtr<FUniqueNetIdEOSPlus> FOnlineUserEOSPlus::AddRemotePlayer(TSharedPtr<const FUniqueNetId> BaseNetId, TSharedPtr<const FUniqueNetId> EOSNetId)
{
	TSharedPtr<FUniqueNetIdEOSPlus> PlusNetId = MakeShared<FUniqueNetIdEOSPlus>(BaseNetId, EOSNetId);

	BaseNetIdToNetIdPlus.Add(BaseNetId->ToString(), PlusNetId);
	EOSNetIdToNetIdPlus.Add(EOSNetId->ToString(), PlusNetId);
	NetIdPlusToBaseNetId.Add(PlusNetId->ToString(), BaseNetId);
	NetIdPlusToEOSNetId.Add(PlusNetId->ToString(), EOSNetId);
	NetIdPlusToNetIdPlus.Add(PlusNetId->ToString(), PlusNetId);

	return PlusNetId;
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
	if (Size < 32)
	{
		UE_LOG_ONLINE(Error, TEXT("Invalid size (%d) passed to FOnlineUserEOSPlus::CreateUniquePlayerId()"), Size);
		return nullptr;
	}
	// We know that the last 32 bytes are the EOS ids, so the rest is the platform id
	int32 PlatformIdSize = Size - 32;
	if (PlatformIdSize < 0)
	{
		UE_LOG_ONLINE(Error, TEXT("Invalid size (%d) passed to FOnlineUserEOSPlus::CreateUniquePlayerId()"), Size);
		return nullptr;
	}

	// First 32 bytes are always the EOS/EAS ids so we can have the pure EOS OSS handle them too
	TSharedPtr<const FUniqueNetId> EOSNetId = EOSIdentityInterface->CreateUniquePlayerId(Bytes, 32);
//@todo joeg handle the case of differing platforms
	TSharedPtr<const FUniqueNetId> BaseNetId = BaseIdentityInterface->CreateUniquePlayerId(Bytes + 32, PlatformIdSize);
	
	return AddRemotePlayer(BaseNetId, EOSNetId);
}

TSharedPtr<const FUniqueNetId> FOnlineUserEOSPlus::CreateUniquePlayerId(const FString& Str)
{
	// Split <id>_+_<id2> into two strings
	int32 FoundAt = Str.Find(TEXT("_+_"));
	if (FoundAt == -1)
	{
		UE_LOG_ONLINE(Error, TEXT("Couldn't parse string (%s) passed to FOnlineUserEOSPlus::CreateUniquePlayerId()"), *Str);
		return nullptr;
	}

	TSharedPtr<const FUniqueNetId> BaseNetId = BaseIdentityInterface->CreateUniquePlayerId(Str.Left(FoundAt));
	TSharedPtr<const FUniqueNetId> EOSNetId = EOSIdentityInterface->CreateUniquePlayerId(Str.Right(Str.Len() - FoundAt - 3));

	return AddRemotePlayer(BaseNetId, EOSNetId);
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

void FOnlineUserEOSPlus::GetLinkedAccountAuthToken(int32 LocalUserNum, const FOnGetLinkedAccountAuthTokenCompleteDelegate& Delegate) const
{
	// Pass through to the platform layer
	BaseIdentityInterface->GetLinkedAccountAuthToken(LocalUserNum, Delegate);
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
	if (Friend.IsValid())
	{
		return GetFriend(Friend.ToSharedRef());
	}
	return nullptr;
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
	TSharedPtr<FUniqueNetIdEOSPlus> NetIdPlus = GetNetIdPlus(User.ToString());
	if (!NetIdPlus.IsValid())
	{
		UE_LOG_ONLINE(Error, TEXT("Failed to find user (%s) in net id plus to base net id map"), *User.ToString());
		Delegate.ExecuteIfBound(User, false);
		return;
	}
	BasePresenceInterface->SetPresence(*NetIdPlusToBaseNetId[User.ToString()], Status,
		FOnPresenceTaskCompleteDelegate::CreateLambda([this, NetIdPlus, StatusCopy = FOnlineUserPresenceStatus(Status), IntermediateComplete = FOnPresenceTaskCompleteDelegate(Delegate)](const FUniqueNetId& UserId, const bool bWasSuccessful)
	{
		// Skip setting EAS presence if not mirrored or if we errored at the platform level or the EOS user isn't found
		if (!bWasSuccessful || !NetIdPlus->GetEOSNetId().IsValid() || !GetDefault<UEOSSettings>()->bMirrorPresenceToEAS)
		{
			IntermediateComplete.ExecuteIfBound(UserId, bWasSuccessful);
			return;
		}
		// Set the EAS version too
		EOSPresenceInterface->SetPresence(*NetIdPlus->GetEOSNetId(), StatusCopy,
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
