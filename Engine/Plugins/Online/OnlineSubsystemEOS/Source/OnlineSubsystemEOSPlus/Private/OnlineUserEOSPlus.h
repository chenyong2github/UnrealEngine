// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/CoreOnline.h"
#include "OnlineSubsystem.h"
#include "Interfaces/OnlineIdentityInterface.h"
#include "Interfaces/OnlineUserInterface.h"
#include "Interfaces/OnlineFriendsInterface.h"
#include "Interfaces/OnlinePresenceInterface.h"

class FOnlineSubsystemEOSPlus;

/**
 * Unique net id wrapper for a EOS account ids. The underlying string is a combination
 * of both account ids concatenated. "<EOS_EpicAccountId>|<EOS_ProductAccountId>"
 */
class FUniqueNetIdEOSPlus :
	public FUniqueNetIdString
{
public:
	FUniqueNetIdEOSPlus()
	{
	}

	explicit FUniqueNetIdEOSPlus(TSharedPtr<const FUniqueNetId> InBaseUniqueNetId, TSharedPtr<const FUniqueNetId> InEOSUniqueNetId);

// FUniqueNetId interface
	virtual const uint8* GetBytes() const override;
	virtual int32 GetSize() const override;
	virtual bool IsValid() const override;
// ~FUniqueNetId interface

private:
	TSharedPtr<const FUniqueNetId> BaseUniqueNetId;
	TSharedPtr<const FUniqueNetId> EOSUniqueNetId;
	TArray<uint8> RawBytes;
};

template<class AggregateUserType>
class TOnlineAggregateUser :
	public AggregateUserType
{
public:
	TOnlineAggregateUser(TSharedPtr<AggregateUserType> InBase, TSharedPtr<AggregateUserType> InEOS)
		: BaseItem(InBase)
		, EOSItem(InEOS)
	{
		check(InBase.IsValid() || EOSItem.IsValid());
	}

	virtual ~TOnlineAggregateUser()
	{
	}

// FOnlineUser interface
	virtual TSharedRef<const FUniqueNetId> GetUserId() const override
	{
		return MakeShared<FUniqueNetIdEOSPlus>(BaseItem->GetUserId(), EOSItem->GetUserId());
	}

	virtual FString GetRealName() const override
	{
		return GetBaseOrEOSItem()->GetRealName();
	}

	virtual FString GetDisplayName(const FString& Platform = FString()) const override
	{
		if (Platform == TEXT("EOS"))
		{
			if (IsEOSItemValid())
			{
				return EOSItem->GetDisplayName();
			}
			return FString();
		}
		if (IsBaseItemValid())
		{
			return BaseItem->GetDisplayName(Platform);
		}
		return FString();
	}

	virtual bool GetUserAttribute(const FString& AttrName, FString& OutAttrValue) const override
	{
		if (IsBaseItemValid() && BaseItem->GetUserAttribute(AttrName, OutAttrValue))
		{
			return true;
		}
		return IsEOSItemValid() && EOSItem->GetUserAttribute(AttrName, OutAttrValue);
	}

	virtual bool SetUserLocalAttribute(const FString& AttrName, const FString& AttrValue) override
	{
		if (IsBaseItemValid() && BaseItem->SetUserLocalAttribute(AttrName, AttrValue))
		{
			return true;
		}
		return IsEOSItemValid() && EOSItem->SetUserLocalAttribute(AttrName, AttrValue);
	}
// ~FOnlineUser interface

	bool IsBaseItemValid() const
	{
		return BaseItem.IsValid();
	}

	bool IsEOSItemValid() const
	{
		return EOSItem.IsValid();
	}

	void SetEOSItem(TSharedPtr<AggregateUserType> InEOSItem)
	{
		EOSItem = InEOSItem;
	}

	TSharedPtr<AggregateUserType> GetBaseOrEOSItem()
	{
		return BaseItem.IsValid() ? BaseItem : EOSItem;
	}

	TSharedPtr<AggregateUserType> GetBaseOrEOSItem() const
	{
		return BaseItem.IsValid() ? BaseItem : EOSItem;
	}

protected:
	TSharedPtr<AggregateUserType> BaseItem;
	TSharedPtr<AggregateUserType> EOSItem;
};

typedef TOnlineAggregateUser<FOnlineUser> FOnlineUserPlus;
typedef TOnlineAggregateUser<FOnlineBlockedPlayer> FOnlineBlockedPlayerPlus;

class FOnlineUserAccountPlus :
	public TOnlineAggregateUser<FUserOnlineAccount>
{
public:
	FOnlineUserAccountPlus(TSharedPtr<FUserOnlineAccount> InBase, TSharedPtr<FUserOnlineAccount> InEOS)
		: TOnlineAggregateUser<FUserOnlineAccount>(InBase, InEOS)
	{
	}

	virtual ~FOnlineUserAccountPlus()
	{
	}

// FUserOnlineAccount interface
	virtual FString GetAccessToken() const override
	{
		return BaseItem->GetAccessToken();
	}

	virtual bool GetAuthAttribute(const FString& AttrName, FString& OutAttrValue) const override
	{
		if (IsBaseItemValid() && BaseItem->GetAuthAttribute(AttrName, OutAttrValue))
		{
			return true;
		}
		return IsEOSItemValid() && EOSItem->GetAuthAttribute(AttrName, OutAttrValue);
	}

	virtual bool SetUserAttribute(const FString& AttrName, const FString& AttrValue) override
	{
		if (IsBaseItemValid() && BaseItem->SetUserAttribute(AttrName, AttrValue))
		{
			return true;
		}
		return IsEOSItemValid() && EOSItem->SetUserAttribute(AttrName, AttrValue);
	}
// ~FUserOnlineAccount interface
};

class FOnlineFriendPlus :
	public TOnlineAggregateUser<FOnlineFriend>
{
public:
	FOnlineFriendPlus(TSharedPtr<FOnlineFriend> InBase, TSharedPtr<FOnlineFriend> InEOS)
		: TOnlineAggregateUser<FOnlineFriend>(InBase, InEOS)
	{
	}

	virtual ~FOnlineFriendPlus()
	{
	}

// FOnlineFriend interface
	virtual EInviteStatus::Type GetInviteStatus() const
	{
		EInviteStatus::Type Status = EInviteStatus::Unknown;
		if (IsBaseItemValid())
		{
			Status = BaseItem->GetInviteStatus();
		}
		if (Status == EInviteStatus::Unknown && IsEOSItemValid())
		{
			Status = EOSItem->GetInviteStatus();
		}
		return Status;
	}

	virtual const class FOnlineUserPresence& GetPresence() const
	{
		if (IsBaseItemValid())
		{
			return BaseItem->GetPresence();
		}
		if (IsEOSItemValid())
		{
			return EOSItem->GetPresence();
		}
		return EmptyPresence;
	}
// ~FOnlineFriend interface

	FOnlineUserPresence EmptyPresence;
};

class FOnlineRecentPlayerPlus :
	public TOnlineAggregateUser<FOnlineRecentPlayer>
{
public:
	FOnlineRecentPlayerPlus(TSharedPtr<FOnlineRecentPlayer> InBase, TSharedPtr<FOnlineRecentPlayer> InEOS)
		: TOnlineAggregateUser<FOnlineRecentPlayer>(InBase, InEOS)
	{
	}

	virtual ~FOnlineRecentPlayerPlus()
	{
	}

// FOnlineRecentPlayer interface
	virtual FDateTime GetLastSeen() const
	{
		if (IsBaseItemValid())
		{
			return BaseItem->GetLastSeen();
		}
		return FDateTime(0);
	}
// ~FOnlineRecentPlayer interface
};

/**
 * Interface for combining platform users with EOS/EAS users
 */
class FOnlineUserEOSPlus :
	public IOnlineIdentity,
	public IOnlineUser,
	public IOnlineFriends,
	public IOnlinePresence
{
public:
	FOnlineUserEOSPlus() = delete;
	virtual ~FOnlineUserEOSPlus();

// IOnlineUser Interface
	virtual bool QueryUserInfo(int32 LocalUserNum, const TArray<TSharedRef<const FUniqueNetId>>& UserIds) override;
	virtual bool GetAllUserInfo(int32 LocalUserNum, TArray<TSharedRef<FOnlineUser>>& OutUsers) override;
	virtual TSharedPtr<FOnlineUser> GetUserInfo(int32 LocalUserNum, const FUniqueNetId& UserId) override;
	virtual bool QueryUserIdMapping(const FUniqueNetId& UserId, const FString& DisplayNameOrEmail, const FOnQueryUserMappingComplete& Delegate = FOnQueryUserMappingComplete()) override;
	virtual bool QueryExternalIdMappings(const FUniqueNetId& UserId, const FExternalIdQueryOptions& QueryOptions, const TArray<FString>& ExternalIds, const FOnQueryExternalIdMappingsComplete& Delegate = FOnQueryExternalIdMappingsComplete()) override;
	virtual void GetExternalIdMappings(const FExternalIdQueryOptions& QueryOptions, const TArray<FString>& ExternalIds, TArray<TSharedPtr<const FUniqueNetId>>& OutIds) override;
	virtual TSharedPtr<const FUniqueNetId> GetExternalIdMapping(const FExternalIdQueryOptions& QueryOptions, const FString& ExternalId) override;
// ~IOnlineUser Interface

// IOnlineIdentity Interface
	virtual bool Login(int32 LocalUserNum, const FOnlineAccountCredentials& AccountCredentials) override;
	virtual bool Logout(int32 LocalUserNum) override;
	virtual bool AutoLogin(int32 LocalUserNum) override;
	virtual TSharedPtr<FUserOnlineAccount> GetUserAccount(const FUniqueNetId& UserId) const override;
	virtual TArray<TSharedPtr<FUserOnlineAccount>> GetAllUserAccounts() const override;
	virtual TSharedPtr<const FUniqueNetId> GetUniquePlayerId(int32 LocalUserNum) const override;
	virtual TSharedPtr<const FUniqueNetId> CreateUniquePlayerId(uint8* Bytes, int32 Size) override;
	virtual TSharedPtr<const FUniqueNetId> CreateUniquePlayerId(const FString& Str) override;
	virtual ELoginStatus::Type GetLoginStatus(int32 LocalUserNum) const override;
	virtual ELoginStatus::Type GetLoginStatus(const FUniqueNetId& UserId) const override;
	virtual FString GetPlayerNickname(int32 LocalUserNum) const override;
	virtual FString GetPlayerNickname(const FUniqueNetId& UserId) const override;
	virtual FString GetAuthToken(int32 LocalUserNum) const override;
	virtual void GetUserPrivilege(const FUniqueNetId& UserId, EUserPrivileges::Type Privilege, const FOnGetUserPrivilegeCompleteDelegate& Delegate) override;
	virtual FString GetAuthType() const override;
	virtual void RevokeAuthToken(const FUniqueNetId& LocalUserId, const FOnRevokeAuthTokenCompleteDelegate& Delegate) override;
	virtual FPlatformUserId GetPlatformUserIdFromUniqueNetId(const FUniqueNetId& UniqueNetId) const override;
// ~IOnlineIdentity Interface

// IOnlineFriends Interface
	virtual bool ReadFriendsList(int32 LocalUserNum, const FString& ListName, const FOnReadFriendsListComplete& Delegate = FOnReadFriendsListComplete()) override;
	virtual bool DeleteFriendsList(int32 LocalUserNum, const FString& ListName, const FOnDeleteFriendsListComplete& Delegate = FOnDeleteFriendsListComplete()) override;
	virtual bool SendInvite(int32 LocalUserNum, const FUniqueNetId& FriendId, const FString& ListName, const FOnSendInviteComplete& Delegate = FOnSendInviteComplete()) override;
	virtual bool AcceptInvite(int32 LocalUserNum, const FUniqueNetId& FriendId, const FString& ListName, const FOnAcceptInviteComplete& Delegate = FOnAcceptInviteComplete()) override;
	virtual bool RejectInvite(int32 LocalUserNum, const FUniqueNetId& FriendId, const FString& ListName) override;
	virtual bool DeleteFriend(int32 LocalUserNum, const FUniqueNetId& FriendId, const FString& ListName) override;
	virtual bool GetFriendsList(int32 LocalUserNum, const FString& ListName, TArray< TSharedRef<FOnlineFriend> >& OutFriends) override;
	virtual TSharedPtr<FOnlineFriend> GetFriend(int32 LocalUserNum, const FUniqueNetId& FriendId, const FString& ListName) override;
	virtual bool IsFriend(int32 LocalUserNum, const FUniqueNetId& FriendId, const FString& ListName) override;
	virtual bool QueryRecentPlayers(const FUniqueNetId& UserId, const FString& Namespace) override;
	virtual bool GetRecentPlayers(const FUniqueNetId& UserId, const FString& Namespace, TArray< TSharedRef<FOnlineRecentPlayer> >& OutRecentPlayers) override;
	virtual bool BlockPlayer(int32 LocalUserNum, const FUniqueNetId& PlayerId) override;
	virtual bool UnblockPlayer(int32 LocalUserNum, const FUniqueNetId& PlayerId) override;
	virtual bool QueryBlockedPlayers(const FUniqueNetId& UserId) override;
	virtual bool GetBlockedPlayers(const FUniqueNetId& UserId, TArray< TSharedRef<FOnlineBlockedPlayer> >& OutBlockedPlayers) override;
	virtual void DumpBlockedPlayers() const override;
	virtual void SetFriendAlias(int32 LocalUserNum, const FUniqueNetId& FriendId, const FString& ListName, const FString& Alias, const FOnSetFriendAliasComplete& Delegate = FOnSetFriendAliasComplete()) override;
	virtual void DeleteFriendAlias(int32 LocalUserNum, const FUniqueNetId& FriendId, const FString& ListName, const FOnDeleteFriendAliasComplete& Delegate = FOnDeleteFriendAliasComplete()) override;
	virtual void DumpRecentPlayers() const override;
// ~IOnlineFriends Interface

// IOnlinePresence Interface
	virtual void SetPresence(const FUniqueNetId& User, const FOnlineUserPresenceStatus& Status, const FOnPresenceTaskCompleteDelegate& Delegate = FOnPresenceTaskCompleteDelegate()) override;
	virtual void QueryPresence(const FUniqueNetId& User, const FOnPresenceTaskCompleteDelegate& Delegate = FOnPresenceTaskCompleteDelegate()) override;
	virtual EOnlineCachedResult::Type GetCachedPresence(const FUniqueNetId& User, TSharedPtr<FOnlineUserPresence>& OutPresence) override;
	virtual EOnlineCachedResult::Type GetCachedPresenceForApp(const FUniqueNetId& LocalUserId, const FUniqueNetId& User, const FString& AppId, TSharedPtr<FOnlineUserPresence>& OutPresence) override;
// ~IOnlinePresence Interface

PACKAGE_SCOPE:
	FOnlineUserEOSPlus(FOnlineSubsystemEOSPlus* InSubsystem);

	// Delegates to rebroadcast things back out
	void OnLoginChanged(int32 LocalUserNum);
	void OnEOSLoginChanged(int32 LocalUserNum);
	void OnLoginStatusChanged(int32 LocalUserNum, ELoginStatus::Type OldStatus, ELoginStatus::Type NewStatus, const FUniqueNetId& NewId);
	void OnControllerPairingChanged(int32 LocalUserNum, FControllerPairingChangedUserInfo PreviousUser, FControllerPairingChangedUserInfo NewUser);
	void OnLoginComplete(int32 LocalUserNum, bool bWasSuccessful, const FUniqueNetId& UserId, const FString& Error);
	void OnLogoutComplete(int32 LocalUserNum, bool bWasSuccessful);
	void OnPresenceReceived(const FUniqueNetId& UserId, const TSharedRef<FOnlineUserPresence>& Presence);
	void OnPresenceArrayUpdated(const FUniqueNetId& UserId, const TArray<TSharedRef<FOnlineUserPresence>>& NewPresenceArray);
	// Friends delegates to rebroadcast things back out
	void OnFriendsChanged();
	void OnOutgoingInviteSent();
	void OnInviteReceived(const FUniqueNetId& UserId, const FUniqueNetId& FriendId);
	void OnInviteAccepted(const FUniqueNetId& UserId, const FUniqueNetId& FriendId);
	void OnInviteRejected(const FUniqueNetId& UserId, const FUniqueNetId& FriendId);
	void OnInviteAborted(const FUniqueNetId& UserId, const FUniqueNetId& FriendId);
	void OnFriendRemoved(const FUniqueNetId& UserId, const FUniqueNetId& FriendId);

private:
	void AddPlayer(int32 LocalUserNum);
	void RemovePlayer(int32 LocalUserNum);
	TSharedRef<FOnlineFriendPlus> AddFriend(TSharedRef<FOnlineFriend> Friend);
	TSharedRef<FOnlineFriendPlus> GetFriend(TSharedRef<FOnlineFriend> Friend);
	TSharedRef<FOnlineRecentPlayer> AddRecentPlayer(TSharedRef<FOnlineRecentPlayer> Player);
	TSharedRef<FOnlineRecentPlayer> GetRecentPlayer(TSharedRef<FOnlineRecentPlayer> Player);
	TSharedRef<FOnlineBlockedPlayer> AddBlockedPlayer(TSharedRef<FOnlineBlockedPlayer> Player);
	TSharedRef<FOnlineBlockedPlayer> GetBlockedPlayer(TSharedRef<FOnlineBlockedPlayer> Player);
	TSharedPtr<FUniqueNetIdEOSPlus> GetNetIdPlus(const FString& SourceId);

	/** Reference to the owning EOS plus subsystem */
	FOnlineSubsystemEOSPlus* EOSPlus;

	IOnlineUserPtr BaseUserInterface;
	IOnlineIdentityPtr BaseIdentityInterface;
	IOnlineUserPtr EOSUserInterface;
	IOnlineIdentityPtr EOSIdentityInterface;
	IOnlineFriendsPtr BaseFriendsInterface;
	IOnlineFriendsPtr EOSFriendsInterface;
	IOnlinePresencePtr BasePresenceInterface;
	IOnlinePresencePtr EOSPresenceInterface;

	FOnQueryUserInfoCompleteDelegate IntermediateOnQueryUserInfoCompleteDelegateHandle;
	FOnQueryUserInfoCompleteDelegate FinalOnQueryUserInfoCompleteDelegateHandle;

	/** Maps of net ids */
	TMap<FString, TSharedPtr<FUniqueNetIdEOSPlus>> BaseNetIdToNetIdPlus;
	TMap<FString, TSharedPtr<FUniqueNetIdEOSPlus>> EOSNetIdToNetIdPlus;
	TMap<FString, TSharedPtr<const FUniqueNetId>> NetIdPlusToBaseNetId;
	TMap<FString, TSharedPtr<const FUniqueNetId>> NetIdPlusToEOSNetId;
	TMap<int32, TSharedPtr<FUniqueNetIdEOSPlus>> LocalUserNumToNetIdPlus;

	/** Online user variants maps */
	TMap<FString, TSharedRef<FOnlineUserAccountPlus>> NetIdPlusToUserAccountMap;
	TMap<FString, TSharedRef<FOnlineUserPlus>> NetIdPlusToUserMap;
	TMap<FString, TSharedRef<FOnlineFriendPlus>> NetIdPlusToFriendMap;
	TMap<FString, TSharedRef<FOnlineRecentPlayerPlus>> NetIdPlusToRecentPlayerMap;
	TMap<FString, TSharedRef<FOnlineBlockedPlayerPlus>> NetIdPlusToBlockedPlayerMap;

	void IntermediateOnQueryUserInfoComplete(int32 LocalUserNum, bool bWasSuccessful, const TArray<TSharedRef<const FUniqueNetId>>& UserIds, const FString& ErrorStr);
	void FinalOnQueryUserInfoComplete(int32 LocalUserNum, bool bWasSuccessful, const TArray<TSharedRef<const FUniqueNetId>>& UserIds, const FString& ErrorStr);
};

typedef TSharedPtr<FOnlineUserEOSPlus, ESPMode::ThreadSafe> FOnlineUserEOSPlusPtr;
