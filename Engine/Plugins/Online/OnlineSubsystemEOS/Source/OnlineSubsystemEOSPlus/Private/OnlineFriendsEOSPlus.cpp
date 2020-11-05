// Copyright Epic Games, Inc. All Rights Reserved.

#include "OnlineFriendsEOSPlus.h"
#include "OnlineSubsystem.h"
#include "OnlineSubsystemEOSPlus.h"
#include "EOSSettings.h"

FOnlineFriendsEOSPlus::FOnlineFriendsEOSPlus(FOnlineSubsystemEOSPlus* InSubsystem)
	: EOSPlus(InSubsystem)
{
	BaseFriendsInterface = EOSPlus->BaseOSS->GetFriendsInterface();
	check(BaseFriendsInterface.IsValid());
	EOSFriendsInterface = EOSPlus->EosOSS->GetFriendsInterface();
	check(EOSFriendsInterface.IsValid());

	// Register delegates we'll want to foward out
	for (int32 LocalUserNum = 0; LocalUserNum < MAX_LOCAL_PLAYERS; LocalUserNum++)
	{
		BaseFriendsInterface->AddOnFriendsChangeDelegate_Handle(LocalUserNum, FOnFriendsChangeDelegate::CreateRaw(this, &FOnlineFriendsEOSPlus::OnFriendsChanged));
		EOSFriendsInterface->AddOnFriendsChangeDelegate_Handle(LocalUserNum, FOnFriendsChangeDelegate::CreateRaw(this, &FOnlineFriendsEOSPlus::OnFriendsChanged));

		BaseFriendsInterface->AddOnOutgoingInviteSentDelegate_Handle(LocalUserNum, FOnOutgoingInviteSentDelegate::CreateRaw(this, &FOnlineFriendsEOSPlus::OnOutgoingInviteSent));
		EOSFriendsInterface->AddOnOutgoingInviteSentDelegate_Handle(LocalUserNum, FOnOutgoingInviteSentDelegate::CreateRaw(this, &FOnlineFriendsEOSPlus::OnOutgoingInviteSent));
	}
	BaseFriendsInterface->AddOnInviteReceivedDelegate_Handle(FOnInviteReceivedDelegate::CreateRaw(this, &FOnlineFriendsEOSPlus::OnInviteReceived));
	BaseFriendsInterface->AddOnInviteAcceptedDelegate_Handle(FOnInviteAcceptedDelegate::CreateRaw(this, &FOnlineFriendsEOSPlus::OnInviteAccepted));
	BaseFriendsInterface->AddOnInviteRejectedDelegate_Handle(FOnInviteRejectedDelegate::CreateRaw(this, &FOnlineFriendsEOSPlus::OnInviteRejected));
	BaseFriendsInterface->AddOnInviteAbortedDelegate_Handle(FOnInviteAbortedDelegate::CreateRaw(this, &FOnlineFriendsEOSPlus::OnInviteAborted));
	BaseFriendsInterface->AddOnFriendRemovedDelegate_Handle(FOnFriendRemovedDelegate::CreateRaw(this, &FOnlineFriendsEOSPlus::OnFriendRemoved));
	EOSFriendsInterface->AddOnInviteReceivedDelegate_Handle(FOnInviteReceivedDelegate::CreateRaw(this, &FOnlineFriendsEOSPlus::OnInviteReceived));
	EOSFriendsInterface->AddOnInviteAcceptedDelegate_Handle(FOnInviteAcceptedDelegate::CreateRaw(this, &FOnlineFriendsEOSPlus::OnInviteAccepted));
	EOSFriendsInterface->AddOnInviteRejectedDelegate_Handle(FOnInviteRejectedDelegate::CreateRaw(this, &FOnlineFriendsEOSPlus::OnInviteRejected));
	EOSFriendsInterface->AddOnInviteAbortedDelegate_Handle(FOnInviteAbortedDelegate::CreateRaw(this, &FOnlineFriendsEOSPlus::OnInviteAborted));
	EOSFriendsInterface->AddOnFriendRemovedDelegate_Handle(FOnFriendRemovedDelegate::CreateRaw(this, &FOnlineFriendsEOSPlus::OnFriendRemoved));
}

FOnlineFriendsEOSPlus::~FOnlineFriendsEOSPlus()
{
	for (int32 LocalUserNum = 0; LocalUserNum < MAX_LOCAL_PLAYERS; LocalUserNum++)
	{
		BaseFriendsInterface->ClearOnFriendsChangeDelegates(LocalUserNum, this);
		EOSFriendsInterface->ClearOnFriendsChangeDelegates(LocalUserNum, this);

		BaseFriendsInterface->ClearOnOutgoingInviteSentDelegates(LocalUserNum, this);
		EOSFriendsInterface->ClearOnOutgoingInviteSentDelegates(LocalUserNum, this);
	}
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
}

void FOnlineFriendsEOSPlus::OnFriendsChanged()
{
	for (int32 LocalUserNum = 0; LocalUserNum < MAX_LOCAL_PLAYERS; LocalUserNum++)
	{
		TriggerOnFriendsChangeDelegates(LocalUserNum);
	}
}

void FOnlineFriendsEOSPlus::OnOutgoingInviteSent()
{
	for (int32 LocalUserNum = 0; LocalUserNum < MAX_LOCAL_PLAYERS; LocalUserNum++)
	{
		TriggerOnOutgoingInviteSentDelegates(LocalUserNum);
	}
}

void FOnlineFriendsEOSPlus::OnInviteReceived(const FUniqueNetId& UserId, const FUniqueNetId& FriendId)
{
	TriggerOnInviteReceivedDelegates(UserId, FriendId);
}

void FOnlineFriendsEOSPlus::OnInviteAccepted(const FUniqueNetId& UserId, const FUniqueNetId& FriendId)
{
	TriggerOnInviteAcceptedDelegates(UserId, FriendId);
}

void FOnlineFriendsEOSPlus::OnInviteRejected(const FUniqueNetId& UserId, const FUniqueNetId& FriendId)
{
	TriggerOnInviteRejectedDelegates(UserId, FriendId);
}

void FOnlineFriendsEOSPlus::OnInviteAborted(const FUniqueNetId& UserId, const FUniqueNetId& FriendId)
{
	TriggerOnInviteAbortedDelegates(UserId, FriendId);
}

void FOnlineFriendsEOSPlus::OnFriendRemoved(const FUniqueNetId& UserId, const FUniqueNetId& FriendId)
{
	TriggerOnFriendRemovedDelegates(UserId, FriendId);
}

bool FOnlineFriendsEOSPlus::ReadFriendsList(int32 LocalUserNum, const FString& ListName, const FOnReadFriendsListComplete& Delegate)
{
	BaseFriendsInterface->ReadFriendsList(LocalUserNum, ListName,
		FOnReadFriendsListComplete::CreateLambda([this, IntermediateComplete = FOnReadFriendsListComplete(Delegate)](int32 LocalUserNum, bool bWasSuccessful, const FString& ListName, const FString& ErrorStr)
	{
		// Skip reading EAS if not in use and if we errored at the platform level
		if (!GetDefault<UEOSSettings>()->bUseEAS || !bWasSuccessful)
		{
			IntermediateComplete.ExecuteIfBound(LocalUserNum, bWasSuccessful, ListName, ErrorStr);
			return;
		}
		// Read the EAS version too
		BaseFriendsInterface->ReadFriendsList(LocalUserNum, ListName,
			FOnReadFriendsListComplete::CreateLambda([this, OnComplete = FOnReadFriendsListComplete(IntermediateComplete)](int32 LocalUserNum, bool bWasSuccessful, const FString& ListName, const FString& ErrorStr)
		{
			OnComplete.ExecuteIfBound(LocalUserNum, bWasSuccessful, ListName, ErrorStr);
		}));
	}));
	return false;
}

bool FOnlineFriendsEOSPlus::DeleteFriendsList(int32 LocalUserNum, const FString& ListName, const FOnDeleteFriendsListComplete& Delegate)
{
	return BaseFriendsInterface->DeleteFriendsList(LocalUserNum, ListName, Delegate);
}

bool FOnlineFriendsEOSPlus::SendInvite(int32 LocalUserNum, const FUniqueNetId& FriendId, const FString& ListName, const FOnSendInviteComplete& Delegate)
{
	return BaseFriendsInterface->SendInvite(LocalUserNum, FriendId, ListName, Delegate);
}

bool FOnlineFriendsEOSPlus::AcceptInvite(int32 LocalUserNum, const FUniqueNetId& FriendId, const FString& ListName, const FOnAcceptInviteComplete& Delegate)
{
	return BaseFriendsInterface->AcceptInvite(LocalUserNum, FriendId, ListName, Delegate);
}

bool FOnlineFriendsEOSPlus::RejectInvite(int32 LocalUserNum, const FUniqueNetId& FriendId, const FString& ListName)
{
	return BaseFriendsInterface->RejectInvite(LocalUserNum, FriendId, ListName);
}

bool FOnlineFriendsEOSPlus::DeleteFriend(int32 LocalUserNum, const FUniqueNetId& FriendId, const FString& ListName)
{
	return BaseFriendsInterface->DeleteFriend(LocalUserNum, FriendId, ListName);
}

bool FOnlineFriendsEOSPlus::GetFriendsList(int32 LocalUserNum, const FString& ListName, TArray< TSharedRef<FOnlineFriend> >& OutFriends)
{
	OutFriends.Reset();

	TArray<TSharedRef<FOnlineFriend>> Friends;
	bool bWasSuccessful = BaseFriendsInterface->GetFriendsList(LocalUserNum, ListName, Friends);
	OutFriends += Friends;
	if (GetDefault<UEOSSettings>()->bUseEAS)
	{
		Friends.Reset();
		bWasSuccessful |= EOSFriendsInterface->GetFriendsList(LocalUserNum, ListName, Friends);
// @todo joeg - filter out duplicate friends
		OutFriends += Friends;
	}
	return bWasSuccessful;
}

TSharedPtr<FOnlineFriend> FOnlineFriendsEOSPlus::GetFriend(int32 LocalUserNum, const FUniqueNetId& FriendId, const FString& ListName)
{
	TSharedPtr<FOnlineFriend> OutFriend = BaseFriendsInterface->GetFriend(LocalUserNum, FriendId, ListName);
	if (!OutFriend.IsValid() && GetDefault<UEOSSettings>()->bUseEAS)
	{
		OutFriend = EOSFriendsInterface->GetFriend(LocalUserNum, FriendId, ListName);
	}
	return OutFriend;
}

bool FOnlineFriendsEOSPlus::IsFriend(int32 LocalUserNum, const FUniqueNetId& FriendId, const FString& ListName)
{
	bool bIsFriend = BaseFriendsInterface->IsFriend(LocalUserNum, FriendId, ListName);
	if (!bIsFriend && GetDefault<UEOSSettings>()->bUseEAS)
	{
		bIsFriend = EOSFriendsInterface->IsFriend(LocalUserNum, FriendId, ListName);
	}
	return bIsFriend;
}

bool FOnlineFriendsEOSPlus::QueryRecentPlayers(const FUniqueNetId& UserId, const FString& Namespace)
{
	return BaseFriendsInterface->QueryRecentPlayers(UserId, Namespace);
}

bool FOnlineFriendsEOSPlus::GetRecentPlayers(const FUniqueNetId& UserId, const FString& Namespace, TArray< TSharedRef<FOnlineRecentPlayer> >& OutRecentPlayers)
{
	return BaseFriendsInterface->GetRecentPlayers(UserId, Namespace, OutRecentPlayers);
}

bool FOnlineFriendsEOSPlus::BlockPlayer(int32 LocalUserNum, const FUniqueNetId& PlayerId)
{
	return BaseFriendsInterface->BlockPlayer(LocalUserNum, PlayerId);
}

bool FOnlineFriendsEOSPlus::UnblockPlayer(int32 LocalUserNum, const FUniqueNetId& PlayerId)
{
	return BaseFriendsInterface->UnblockPlayer(LocalUserNum, PlayerId);
}

bool FOnlineFriendsEOSPlus::QueryBlockedPlayers(const FUniqueNetId& UserId)
{
	return BaseFriendsInterface->QueryBlockedPlayers(UserId);
}

bool FOnlineFriendsEOSPlus::GetBlockedPlayers(const FUniqueNetId& UserId, TArray< TSharedRef<FOnlineBlockedPlayer> >& OutBlockedPlayers)
{
	return BaseFriendsInterface->GetBlockedPlayers(UserId, OutBlockedPlayers);
}

void FOnlineFriendsEOSPlus::DumpBlockedPlayers() const
{
	BaseFriendsInterface->DumpBlockedPlayers();
}

void FOnlineFriendsEOSPlus::SetFriendAlias(int32 LocalUserNum, const FUniqueNetId& FriendId, const FString& ListName, const FString& Alias, const FOnSetFriendAliasComplete& Delegate)
{
	BaseFriendsInterface->SetFriendAlias(LocalUserNum, FriendId, ListName, Alias, Delegate);
}

void FOnlineFriendsEOSPlus::DeleteFriendAlias(int32 LocalUserNum, const FUniqueNetId& FriendId, const FString& ListName, const FOnDeleteFriendAliasComplete& Delegate)
{
	BaseFriendsInterface->DeleteFriendAlias(LocalUserNum, FriendId, ListName, Delegate);
}

void FOnlineFriendsEOSPlus::DumpRecentPlayers() const
{
	BaseFriendsInterface->DumpRecentPlayers();
}

