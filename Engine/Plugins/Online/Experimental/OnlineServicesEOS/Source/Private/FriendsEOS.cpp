// Copyright Epic Games, Inc. All Rights Reserved.

#include "FriendsEOS.h"
#include "Online/OnlineAsyncOp.h"
#include "OnlineServicesEOS.h"
#include "OnlineServicesEOSTypes.h"
#include "AuthEOS.h"

#include "eos_friends.h"

namespace UE::Online {

inline TOptional<EFriendInviteStatus> EOSFriendStatusToInviteStatus(EOS_EFriendsStatus EOSFriendStatus)
{
	TOptional<EFriendInviteStatus> InviteStatus;
	switch (EOSFriendStatus)
	{
	case EOS_EFriendsStatus::EOS_FS_InviteSent:
		InviteStatus.Emplace(EFriendInviteStatus::PendingOutbound);
		break;
	case EOS_EFriendsStatus::EOS_FS_InviteReceived:
		InviteStatus.Emplace(EFriendInviteStatus::PendingInbound);
		break;
	}
	return InviteStatus;
}

FFriendsEOS::FFriendsEOS(FOnlineServicesEOS& InServices)
	: FFriendsCommon(InServices)
{
}

void FFriendsEOS::Initialize()
{
	FFriendsCommon::Initialize();

	FriendsHandle = EOS_Platform_GetFriendsInterface(static_cast<FOnlineServicesEOS&>(GetServices()).GetEOSPlatformHandle());
	check(FriendsHandle != nullptr);

	// Register for friend updates
	EOS_Friends_AddNotifyFriendsUpdateOptions Options = { };
	Options.ApiVersion = EOS_FRIENDS_ADDNOTIFYFRIENDSUPDATE_API_LATEST;
	NotifyFriendsUpdateNotificationId = EOS_Friends_AddNotifyFriendsUpdate(FriendsHandle, &Options, this, [](const EOS_Friends_OnFriendsUpdateInfo* Data)
	{
		FFriendsEOS* This = reinterpret_cast<FFriendsEOS*>(Data->ClientData);

		FAccountId LocalUserId = MakeEOSAccountId(Data->LocalUserId);
		FAccountId FriendUserId = MakeEOSAccountId(Data->TargetUserId);
		This->OnEOSFriendsUpdate(LocalUserId, FriendUserId, Data->PreviousStatus, Data->CurrentStatus);
	});
}

void FFriendsEOS::PreShutdown()
{
	EOS_Friends_RemoveNotifyFriendsUpdate(FriendsHandle, NotifyFriendsUpdateNotificationId);
}

TOnlineAsyncOpHandle<FQueryFriends> FFriendsEOS::QueryFriends(FQueryFriends::Params&& InParams)
{
	TOnlineAsyncOpRef<FQueryFriends> Op = GetJoinableOp<FQueryFriends>(MoveTemp(InParams));
	if (!Op->IsReady())
	{
		// Initialize
		const FQueryFriends::Params& Params = Op->GetParams();
		if (!Services.Get<FAuthEOS>()->IsLoggedIn(Params.LocalUserId))
		{
			// TODO: Error codes
			Op->SetError(Errors::Unknown());
			return Op->GetHandle();
		}

		EOS_Friends_QueryFriendsOptions QueryFriendsOptions = { };
		QueryFriendsOptions.ApiVersion = EOS_FRIENDS_QUERYFRIENDS_API_LATEST;
		TOptional<EOS_EpicAccountId> AccountId = EOSAccountIdFromOnlineServiceAccountId(Params.LocalUserId);
		check(AccountId); // Must be valid if IsLoggedIn succeeded
		QueryFriendsOptions.LocalUserId = AccountId.GetValue();

		Op->Then([this, QueryFriendsOptions](TOnlineAsyncOp<FQueryFriends>& InAsyncOp) mutable
			{
				return EOS_Async<EOS_Friends_QueryFriendsCallbackInfo>(InAsyncOp, EOS_Friends_QueryFriends, FriendsHandle, QueryFriendsOptions);
			})
			.Then([this](TOnlineAsyncOp<FQueryFriends>& InAsyncOp, const EOS_Friends_QueryFriendsCallbackInfo* Data) mutable
			{
				UE_LOG(LogTemp, Warning, TEXT("QueryFriendsResult: [%s]"), *LexToString(Data->ResultCode));

				if (Data->ResultCode == EOS_EResult::EOS_Success)
				{
					bool bAnyChanges = false;
					EOS_Friends_GetFriendsCountOptions GetFriendsCountOptions = {
						EOS_FRIENDS_GETFRIENDSCOUNT_API_LATEST,
						Data->LocalUserId
					};

					TMap<FAccountId, TSharedRef<FFriend>>& FriendsList = FriendsLists.FindOrAdd(InAsyncOp.GetParams().LocalUserId);
					TArray<FAccountId> NewFriendIds;
					TArray<FAccountId> PreviousFriendIds;
					TArray<FAccountId> UpdatedFriendIds; // TODO:  More granular updates like what changed?
					FriendsList.GenerateKeyArray(PreviousFriendIds);

					int32_t NumFriends = EOS_Friends_GetFriendsCount(FriendsHandle, &GetFriendsCountOptions);
					for (int32_t FriendIndex = 0; FriendIndex < NumFriends; ++FriendIndex)
					{
						EOS_Friends_GetFriendAtIndexOptions GetFriendAtIndexOptions = {
							EOS_FRIENDS_GETFRIENDATINDEX_API_LATEST,
							Data->LocalUserId,
							FriendIndex
						};

						EOS_EpicAccountId EOSFriendId = EOS_Friends_GetFriendAtIndex(FriendsHandle, &GetFriendAtIndexOptions);
						FAccountId FriendId = MakeEOSAccountId(EOSFriendId);
						//check(FriendId.IsValid()); // TODO:  Some way to check validity

						EOS_Friends_GetStatusOptions GetStatusOptions = {
							EOS_FRIENDS_GETSTATUS_API_LATEST,
							Data->LocalUserId,
							EOSFriendId
						};

						EOS_EFriendsStatus EOSFriendStatus = EOS_Friends_GetStatus(FriendsHandle, &GetStatusOptions);
						TOptional<EFriendInviteStatus> InviteStatus = EOSFriendStatusToInviteStatus(EOSFriendStatus);

						PreviousFriendIds.Remove(FriendId);
						if (TSharedRef<FFriend>* FriendPtr = FriendsList.Find(FriendId))
						{
							// Any updates?
							if (InviteStatus != (*FriendPtr)->InviteStatus)
							{
								UE_LOG(LogTemp, Warning, TEXT("QueryFriendsComplete: %s Invite status changed"), *LexToString(EOSFriendId));
								bAnyChanges = true;
								(*FriendPtr)->InviteStatus = InviteStatus;
								UpdatedFriendIds.Emplace(FriendId);
							}
						}
						else
						{
							// Add new friend
							UE_LOG(LogTemp, Warning, TEXT("QueryFriendsComplete: Adding %s"), *LexToString(EOSFriendId));
							TSharedRef<FFriend> Friend = MakeShared<FFriend>();
							Friend->UserId = FriendId;
							Friend->InviteStatus = InviteStatus;
							FriendsList.Emplace(FriendId, MoveTemp(Friend));

							bAnyChanges = true;
							NewFriendIds.Emplace(FriendId);
						}
					}

					// TODO:  Delegates
					bAnyChanges |= NewFriendIds.Num() > 0;
					bAnyChanges |= PreviousFriendIds.Num() > 0;
					bAnyChanges |= UpdatedFriendIds.Num() > 0;

					for (FAccountId AccountId : PreviousFriendIds)
					{
						UE_LOG(LogTemp, Warning, TEXT("QueryFriendsComplete: %s is no longer a friend, removing"), *LexToString(EOSAccountIdFromOnlineServiceAccountId(AccountId).GetValue()));
						FriendsList.Remove(AccountId);
					}

					InAsyncOp.SetResult(FQueryFriends::Result());
					
					if (bAnyChanges)
					{
						FFriendsListUpdated FriendsListUpdatedParams = {};
						FriendsListUpdatedParams.LocalUserId = InAsyncOp.GetParams().LocalUserId;
						FriendsListUpdatedParams.AddedFriends = NewFriendIds;
						FriendsListUpdatedParams.RemovedFriends = PreviousFriendIds;
						FriendsListUpdatedParams.UpdatedFriends = UpdatedFriendIds;
						OnFriendsListUpdatedEvent.Broadcast(FriendsListUpdatedParams);
					}
				}
				else
				{
					InAsyncOp.SetError(Errors::Unknown()); // TODO: Error codes
				}
			})
			.Enqueue(GetSerialQueue());
	}
	return Op->GetHandle();
}

TOnlineResult<FGetFriends> FFriendsEOS::GetFriends(FGetFriends::Params&& Params)
{
	if (TMap<FAccountId, TSharedRef<FFriend>>* FriendsList = FriendsLists.Find(Params.LocalUserId))
	{
		FGetFriends::Result Result;
		FriendsList->GenerateValueArray(Result.Friends);
		return TOnlineResult<FGetFriends>(MoveTemp(Result));
	}
	return TOnlineResult<FGetFriends>(Errors::Unknown()); // TODO: error codes
}

TOnlineAsyncOpHandle<FAddFriend> FFriendsEOS::AddFriend(FAddFriend::Params&& InParams)
{
	TOnlineAsyncOpRef<FAddFriend> Op = GetJoinableOp<FAddFriend>(MoveTemp(InParams));
	if (!Op->IsReady())
	{
#if 0 // At present this causes issues due to the EOS function executing the callback immediately, before the future can be bound to
		// Initialize
		const FAddFriend::Params& Params = Op->GetParams();
		if (!Services.Get<FAuthEOS>()->IsLoggedIn(Params.LocalUserId))
		{
			// TODO: Error codes
			Op->SetError(Errors::UnknownError());
			return Op->GetHandle();
		}

		// Target user valid?
		TOptional<EOS_EpicAccountId> TargetId = EOSAccountIdFromOnlineServiceAccountId(Params.FriendId);
		if (!TargetId)
		{
			// TODO: Error codes
			Op->SetError(Errors::UnknownError());
			return Op->GetHandle();
		}
		Op->Then([this](TOnlineAsyncOp<FAddFriend>& InAsyncOp) mutable
			{
				const FAddFriend::Params& Params = InAsyncOp.GetParams();
				EOS_Friends_SendInviteOptions SendInviteOptions = { };
				SendInviteOptions.ApiVersion = EOS_FRIENDS_SENDINVITE_API_LATEST;
				SendInviteOptions.LocalUserId = EOSAccountIdFromOnlineServiceAccountId(Params.LocalUserId).GetValue();
				SendInviteOptions.TargetUserId = EOSAccountIdFromOnlineServiceAccountId(Params.FriendId).GetValue();
				return EOS_Async<EOS_Friends_SendInviteCallbackInfo>(InAsyncOp, EOS_Friends_SendInvite, FriendsHandle, SendInviteOptions);
			})
			.Then([this](TOnlineAsyncOp<FAddFriend>& InAsyncOp, const EOS_Friends_SendInviteCallbackInfo* Data) mutable
			{
				UE_LOG(LogTemp, Warning, TEXT("SendInviteResult: [%s]"), *LexToString(Data->ResultCode));
				// TODO:  Handle response
			}).Enqueue();
#else
		Op->SetError(Errors::Unknown());
#endif
	}
	return Op->GetHandle();
}

void FFriendsEOS::OnEOSFriendsUpdate(FAccountId LocalUserId, FAccountId FriendUserId, EOS_EFriendsStatus PreviousStatus, EOS_EFriendsStatus CurrentStatus)
{
	// TODO
	UE_LOG(LogTemp, Warning, TEXT("OnEOSFriendsUpdate: [%s] [%s] %d %d"), 
		*LexToString(EOSAccountIdFromOnlineServiceAccountId(LocalUserId).GetValue()),
		*LexToString(EOSAccountIdFromOnlineServiceAccountId(FriendUserId).GetValue()),
		(int)PreviousStatus,
		(int)CurrentStatus);

	TMap<FAccountId, TSharedRef<FFriend>>& FriendsList = FriendsLists.FindOrAdd(LocalUserId);
	bool bAnyChanges = false;
	TArray<FAccountId, TInlineAllocator<1>> AddedFriends;
	TArray<FAccountId, TInlineAllocator<1>> RemovedFriends;
	TArray<FAccountId, TInlineAllocator<1>> UpdatedFriends;
	if (CurrentStatus == EOS_EFriendsStatus::EOS_FS_NotFriends)
	{
		bAnyChanges = FriendsList.Remove(FriendUserId) > 0;
		if (bAnyChanges)
		{
			RemovedFriends.Emplace(FriendUserId);
		}
	}
	else
	{
		TOptional<EFriendInviteStatus> InviteStatus = EOSFriendStatusToInviteStatus(CurrentStatus);
		if (TSharedRef<FFriend>* ExistingFriend = FriendsList.Find(FriendUserId))
		{
			if ((*ExistingFriend)->InviteStatus != InviteStatus)
			{
				(*ExistingFriend)->InviteStatus = InviteStatus;
				UpdatedFriends.Emplace(FriendUserId);
				bAnyChanges = true;
			}
		}
		else
		{
			// Create new entry
			TSharedRef<FFriend> Friend = MakeShared<FFriend>();
			Friend->UserId = FriendUserId;
			Friend->InviteStatus = InviteStatus;
			FriendsList.Emplace(FriendUserId, MoveTemp(Friend));

			AddedFriends.Emplace(FriendUserId);
			bAnyChanges = true;
		}
	}

	if (bAnyChanges)
	{
		FFriendsListUpdated FriendsListUpdatedParams = {};
		FriendsListUpdatedParams.LocalUserId = LocalUserId;
		FriendsListUpdatedParams.AddedFriends = AddedFriends;
		FriendsListUpdatedParams.RemovedFriends = RemovedFriends;
		FriendsListUpdatedParams.UpdatedFriends = UpdatedFriends;
		OnFriendsListUpdatedEvent.Broadcast(FriendsListUpdatedParams);
	}
}

/* UE::Online */ }
