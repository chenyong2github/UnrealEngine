// Copyright Epic Games, Inc. All Rights Reserved.

#include "ExternalUIEOS.h"

#include "OnlineServicesEOS.h"
#include "OnlineServicesEOSTypes.h"
//#include "Online/ExternalUIErrors.h"
#include "Online/OnlineErrorDefinitions.h"
#include "AuthEOS.h"

#include "eos_ui.h"
#include "eos_types.h"
#include "eos_init.h"
#include "eos_sdk.h"
#include "eos_logging.h"

namespace UE::Online {

FExternalUIEOS::FExternalUIEOS(FOnlineServicesEOS& InServices)
	: FExternalUICommon(InServices)
{
}

void FExternalUIEOS::Initialize()
{
	FExternalUICommon::Initialize();

	UIHandle = EOS_Platform_GetUIInterface(static_cast<FOnlineServicesEOS&>(GetServices()).GetEOSPlatformHandle());
	check(UIHandle != nullptr);
}

void FExternalUIEOS::PreShutdown()
{
}

TOnlineAsyncOpHandle<FExternalUIShowFriendsUI> FExternalUIEOS::ShowFriendsUI(FExternalUIShowFriendsUI::Params&& Params)
{
	TOnlineAsyncOpRef<FExternalUIShowFriendsUI> Op = GetOp<FExternalUIShowFriendsUI>(MoveTemp(Params));

	TOptional<EOS_EpicAccountId> EpicAccountId = EOSAccountIdFromOnlineServiceAccountId(Params.AccountId);
	if (!EpicAccountId.IsSet())
	{
		UE_LOG(LogTemp, Warning, TEXT("[FExternalUIEOS::ShowFriendsUI] EpicAccountId not found for AccountId [%d]"), Params.AccountId.Handle);
		Op->SetError(Errors::Unknown()); // TODO
		return Op->GetHandle();
	}

	EOS_UI_ShowFriendsOptions ShowFriendsOptions = {};
	ShowFriendsOptions.ApiVersion = EOS_UI_SHOWFRIENDS_API_LATEST;
	ShowFriendsOptions.LocalUserId = *EpicAccountId;

	Op->Then([this, ShowFriendsOptions](TOnlineAsyncOp<FExternalUIShowFriendsUI>& InAsyncOp) mutable
	{
		return EOS_Async<EOS_UI_ShowFriendsCallbackInfo>(InAsyncOp, EOS_UI_ShowFriends, UIHandle, ShowFriendsOptions);
	})
	.Then([this](TOnlineAsyncOp<FExternalUIShowFriendsUI>& InAsyncOp, const EOS_UI_ShowFriendsCallbackInfo* Data)
	{
		UE_LOG(LogTemp, Verbose, TEXT("[FExternalUIEOS::ShowFriendsUI] EOS_UI_ShowFriends Result [%s] for User [%s]."), *LexToString(Data->ResultCode), *LexToString(Data->LocalUserId));

		if (Data->ResultCode == EOS_EResult::EOS_Success)
		{
			FExternalUIShowFriendsUI::Result Result = {};
			InAsyncOp.SetResult(MoveTemp(Result));
		}
		else
		{
			InAsyncOp.SetError(Errors::Unknown());
		}
	})
	.Enqueue(GetSerialQueue());

	return Op->GetHandle();
}

/* UE::Online */ }
