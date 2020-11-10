// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/CoreOnline.h"
#include "Interfaces/OnlineFriendsInterface.h"

class FOnlineSubsystemEOSPlus;

/**
 * Interface that handles friends from both OSSes
 */
class FOnlineFriendsEOSPlus :
	public IOnlineFriends
{
public:
	FOnlineFriendsEOSPlus() = delete;
	virtual ~FOnlineFriendsEOSPlus();

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

PACKAGE_SCOPE:
	FOnlineFriendsEOSPlus(FOnlineSubsystemEOSPlus* InSubsystem);

	// Delegates to rebroadcast things back out
	void OnFriendsChanged();
	void OnOutgoingInviteSent();
	void OnInviteReceived(const FUniqueNetId& UserId, const FUniqueNetId& FriendId);
	void OnInviteAccepted(const FUniqueNetId& UserId, const FUniqueNetId& FriendId);
	void OnInviteRejected(const FUniqueNetId& UserId, const FUniqueNetId& FriendId);
	void OnInviteAborted(const FUniqueNetId& UserId, const FUniqueNetId& FriendId);
	void OnFriendRemoved(const FUniqueNetId& UserId, const FUniqueNetId& FriendId);

private:
	/** Reference to the owning EOS plus subsystem */
	FOnlineSubsystemEOSPlus* EOSPlus;
	/** Since we're going to bind to delegates, we need to hold onto these */
	IOnlineFriendsPtr BaseFriendsInterface;
	IOnlineFriendsPtr EOSFriendsInterface;
};

typedef TSharedPtr<FOnlineFriendsEOSPlus, ESPMode::ThreadSafe> FOnlineFriendsEOSPlusPtr;
