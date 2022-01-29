// Copyright Epic Games, Inc. All Rights Reserved.

#include "Online/FriendsEOS.h"
#include "Online/OnlineAsyncOp.h"
#include "Online/OnlineServicesEOS.h"
#include "Online/OnlineServicesEOSTypes.h"
#include "Online/OnlineIdEOS.h"
#include "Online/AuthEOS.h"

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

		const FOnlineAccountIdHandle LocalUserId = FindAccountIdChecked(Data->LocalUserId);
		This->Services.Get<FAuthEOS>()->ResolveAccountId(LocalUserId, Data->TargetUserId)
		.Next([This, LocalUserId, PreviousStatus = Data->PreviousStatus, CurrentStatus = Data->CurrentStatus](const FOnlineAccountIdHandle& FriendUserId)
		{
			UE_LOG(LogTemp, Warning, TEXT("OnEOSFriendsUpdate: LocalUserId=[%s] FriendUserId=[%s]"), *ToLogString(LocalUserId), *ToLogString(FriendUserId));
			This->OnEOSFriendsUpdate(LocalUserId, FriendUserId, PreviousStatus, CurrentStatus);
		});
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
		EOS_EpicAccountId LocalUserEasId = GetEpicAccountIdChecked(Params.LocalUserId);
		QueryFriendsOptions.LocalUserId = LocalUserEasId;

		Op->Then([this, QueryFriendsOptions](TOnlineAsyncOp<FQueryFriends>& InAsyncOp) mutable
		{
			return EOS_Async<EOS_Friends_QueryFriendsCallbackInfo>(EOS_Friends_QueryFriends, FriendsHandle, QueryFriendsOptions);
		})
		.Then([this](TOnlineAsyncOp<FQueryFriends>& InAsyncOp, const EOS_Friends_QueryFriendsCallbackInfo* Data) mutable
		{
			UE_LOG(LogTemp, Warning, TEXT("QueryFriendsResult: [%s]"), *LexToString(Data->ResultCode));

			if (Data->ResultCode != EOS_EResult::EOS_Success)
			{
				InAsyncOp.SetError(Errors::Unknown()); // TODO: Error codes
				return MakeFulfilledPromise<TArray<EOS_EpicAccountId>>().GetFuture();
			}

			EOS_Friends_GetFriendsCountOptions GetFriendsCountOptions = {
				EOS_FRIENDS_GETFRIENDSCOUNT_API_LATEST,
				Data->LocalUserId
			};

			int32_t NumFriends = EOS_Friends_GetFriendsCount(FriendsHandle, &GetFriendsCountOptions);
			TArray<EOS_EpicAccountId> CurrentFriends;
			CurrentFriends.Reserve(NumFriends);
			for (int32_t FriendIndex = 0; FriendIndex < NumFriends; ++FriendIndex)
			{
				EOS_Friends_GetFriendAtIndexOptions GetFriendAtIndexOptions = {
					EOS_FRIENDS_GETFRIENDATINDEX_API_LATEST,
					Data->LocalUserId,
					FriendIndex
				};

				const EOS_EpicAccountId FriendEasId = EOS_Friends_GetFriendAtIndex(FriendsHandle, &GetFriendAtIndexOptions);
				check(EOS_EpicAccountId_IsValid(FriendEasId));
				CurrentFriends.Emplace(FriendEasId);
			}
			return MakeFulfilledPromise<TArray<EOS_EpicAccountId>>(CurrentFriends).GetFuture();
		}).Then(Services.Get<FAuthEOS>()->ResolveEpicIdsFn())
		.Then([this](TOnlineAsyncOp<FQueryFriends>& InAsyncOp, const TArray<FOnlineAccountIdHandle>& CurrentFriendIds) mutable
		{
			const FOnlineAccountIdHandle LocalUserId = InAsyncOp.GetParams().LocalUserId;
			const EOS_EpicAccountId LocalUserEasId = GetEpicAccountIdChecked(LocalUserId);

			TMap<FOnlineAccountIdHandle, TSharedRef<FFriend>>& FriendsList = FriendsLists.FindOrAdd(LocalUserId);
			TArray<FOnlineAccountIdHandle> NewFriendIds;
			TArray<FOnlineAccountIdHandle> PreviousFriendIds;
			TArray<FOnlineAccountIdHandle> UpdatedFriendIds; // TODO:  More granular updates like what changed?
			FriendsList.GenerateKeyArray(PreviousFriendIds);
				
			for (const FOnlineAccountIdHandle& FriendId : CurrentFriendIds)
			{
				const EOS_EpicAccountId FriendEasId = GetEpicAccountIdChecked(FriendId);

				EOS_Friends_GetStatusOptions GetStatusOptions = {
					EOS_FRIENDS_GETSTATUS_API_LATEST,
					LocalUserEasId,
					FriendEasId
				};
				const EOS_EFriendsStatus EOSFriendStatus = EOS_Friends_GetStatus(FriendsHandle, &GetStatusOptions);
				TOptional<EFriendInviteStatus> InviteStatus = EOSFriendStatusToInviteStatus(EOSFriendStatus);

				PreviousFriendIds.Remove(FriendId);
				if (TSharedRef<FFriend>* FriendPtr = FriendsList.Find(FriendId))
				{
					// Any updates?
					if (InviteStatus != (*FriendPtr)->InviteStatus)
					{
						UE_LOG(LogTemp, Warning, TEXT("QueryFriendsComplete: %s Invite status changed"), *LexToString(FriendEasId));
						(*FriendPtr)->InviteStatus = InviteStatus;
						UpdatedFriendIds.Emplace(FriendId);
					}
				}
				else
				{
					// Add new friend
					UE_LOG(LogTemp, Warning, TEXT("QueryFriendsComplete: Adding %s"), *LexToString(FriendEasId));
					TSharedRef<FFriend> Friend = MakeShared<FFriend>();
					Friend->UserId = FriendId;
					Friend->InviteStatus = InviteStatus;
					FriendsList.Emplace(FriendId, MoveTemp(Friend));
					NewFriendIds.Emplace(FriendId);
				}
			}

			// TODO:  Delegates

			for (const FOnlineAccountIdHandle& AccountId : PreviousFriendIds)
			{
				UE_LOG(LogTemp, Warning, TEXT("QueryFriendsComplete: %s is no longer a friend, removing"), *ToLogString(AccountId));
				FriendsList.Remove(AccountId);
			}

			InAsyncOp.SetResult(FQueryFriends::Result());
					
			const bool bAnyChanges = NewFriendIds.Num() > 0
				|| PreviousFriendIds.Num() > 0
				|| UpdatedFriendIds.Num() > 0;
			if (bAnyChanges)
			{
				FFriendsListUpdated FriendsListUpdatedParams = {};
				FriendsListUpdatedParams.LocalUserId = InAsyncOp.GetParams().LocalUserId;
				FriendsListUpdatedParams.AddedFriends = NewFriendIds;
				FriendsListUpdatedParams.RemovedFriends = PreviousFriendIds;
				FriendsListUpdatedParams.UpdatedFriends = UpdatedFriendIds;
				OnFriendsListUpdatedEvent.Broadcast(FriendsListUpdatedParams);
			}
		})
		.Enqueue(GetSerialQueue());
	}
	return Op->GetHandle();
}

TOnlineResult<FGetFriends> FFriendsEOS::GetFriends(FGetFriends::Params&& Params)
{
	if (TMap<FOnlineAccountIdHandle, TSharedRef<FFriend>>* FriendsList = FriendsLists.Find(Params.LocalUserId))
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
		EOS_EpicAccountId TargetId = GetEpicAccountId(Params.FriendId);
		if (!EOS_EpicAccountId_IsValid(TargetId))
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
			return EOS_Async<EOS_Friends_SendInviteCallbackInfo>(EOS_Friends_SendInvite, FriendsHandle, SendInviteOptions);
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

void FFriendsEOS::OnEOSFriendsUpdate(FOnlineAccountIdHandle LocalUserId, FOnlineAccountIdHandle FriendUserId, EOS_EFriendsStatus PreviousStatus, EOS_EFriendsStatus CurrentStatus)
{
	// TODO
	UE_LOG(LogTemp, Warning, TEXT("OnEOSFriendsUpdate: LocalUserId=[%s] FriendUserId=[%s] PreviousStatus=[%s] CurrentStatus=[%s]"), *ToLogString(LocalUserId), *ToLogString(FriendUserId), *LexToString(PreviousStatus), *LexToString(CurrentStatus));

	TMap<FOnlineAccountIdHandle, TSharedRef<FFriend>>& FriendsList = FriendsLists.FindOrAdd(LocalUserId);
	bool bAnyChanges = false;
	TArray<FOnlineAccountIdHandle, TInlineAllocator<1>> AddedFriends;
	TArray<FOnlineAccountIdHandle, TInlineAllocator<1>> RemovedFriends;
	TArray<FOnlineAccountIdHandle, TInlineAllocator<1>> UpdatedFriends;
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
