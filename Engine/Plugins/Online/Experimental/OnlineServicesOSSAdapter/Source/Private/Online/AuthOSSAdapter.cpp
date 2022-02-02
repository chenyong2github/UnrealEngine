// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Online/AuthOSSAdapter.h"

#include "Online/OnlineServicesOSSAdapter.h"
#include "Online/OnlineIdOSSAdapter.h"
#include "Online/OnlineErrorDefinitions.h"

#include "OnlineSubsystem.h"
#include "Interfaces/OnlineIdentityInterface.h"


namespace UE::Online {

void FAuthOSSAdapter::PostInitialize()
{
	Super::PostInitialize();

	if (IOnlineIdentityPtr Identity = GetIdentityInterface())
	{
		for (int LocalPlayerNum = 0; LocalPlayerNum < MAX_LOCAL_PLAYERS; ++LocalPlayerNum)
		{
			OnLoginStatusChangedHandle[LocalPlayerNum] = Identity->OnLoginStatusChangedDelegates[LocalPlayerNum].AddLambda(
				[WeakThis = TWeakPtr<IAuth>(AsShared()), this](int32 LocalUserNum, ::ELoginStatus::Type OldStatus, ::ELoginStatus::Type NewStatus, const FUniqueNetId& NewId)
				{
					TSharedPtr<IAuth> PinnedThis = WeakThis.Pin();
					if (PinnedThis.IsValid())
					{
						FLoginStatusChanged LoginStatusChanged;
						if (NewId.IsValid())
						{
							LoginStatusChanged.LocalUserId = static_cast<FOnlineServicesOSSAdapter&>(Services).GetAccountIdRegistry().FindOrAddHandle(NewId.AsShared());
						}
						switch (OldStatus)
						{
							case ::ELoginStatus::NotLoggedIn:
								LoginStatusChanged.PreviousStatus = ELoginStatus::NotLoggedIn;
								break;
							case ::ELoginStatus::UsingLocalProfile:
								LoginStatusChanged.PreviousStatus = ELoginStatus::UsingLocalProfile;
								break;
							case ::ELoginStatus::LoggedIn:
								LoginStatusChanged.PreviousStatus = ELoginStatus::LoggedIn;
								break;
						}
						switch (NewStatus)
						{
							case ::ELoginStatus::NotLoggedIn:
								LoginStatusChanged.CurrentStatus = ELoginStatus::NotLoggedIn;
								break;
							case ::ELoginStatus::UsingLocalProfile:
								LoginStatusChanged.CurrentStatus = ELoginStatus::UsingLocalProfile;
								break;
							case ::ELoginStatus::LoggedIn:
								LoginStatusChanged.CurrentStatus = ELoginStatus::LoggedIn;
								break;
						}
						OnLoginStatusChangedEvent.Broadcast(LoginStatusChanged);
					}
				});
		}
	}
}

void FAuthOSSAdapter::PreShutdown()
{
	if (IOnlineIdentityPtr Identity = GetIdentityInterface())
	{
		for (int LocalPlayerNum = 0; LocalPlayerNum < MAX_LOCAL_PLAYERS; ++LocalPlayerNum)
		{
			Identity->ClearOnLoginStatusChangedDelegate_Handle(LocalPlayerNum, OnLoginStatusChangedHandle[LocalPlayerNum]);
		}
	}

	Super::PreShutdown();
}

TOnlineAsyncOpHandle<FAuthLogin> FAuthOSSAdapter::Login(FAuthLogin::Params&& Params)
{
	TSharedRef<TOnlineAsyncOp<FAuthLogin>> Op = GetOp<FAuthLogin>(MoveTemp(Params));

	if (!Op->IsReady())
	{
		int32 LocalUserIndex = FPlatformMisc::GetUserIndexForPlatformUser(Op->GetParams().PlatformUserId);

		if (LocalUserIndex < 0 || LocalUserIndex >= MAX_LOCAL_PLAYERS)
		{
			Op->SetError(Errors::InvalidParams());
			return Op->GetHandle();
		}

		Op->Then([this, LocalUserIndex](TOnlineAsyncOp<FAuthLogin>& Op, TPromise<TOnlineResult<FAuthLogin>>&& Result)
			{
				FOnlineAccountCredentials Credentials;
				Credentials.Type = Op.GetParams().CredentialsType;
				Credentials.Id = Op.GetParams().CredentialsId;
				Credentials.Token = Op.GetParams().CredentialsToken.Get<FString>(); // TODO: handle binary token

				TSharedPtr<TPair<FDelegateHandle, TPromise<TOnlineResult<FAuthLogin>>>> ResultPtr = MakeShared<TPair<FDelegateHandle, TPromise<TOnlineResult<FAuthLogin>>>>(FDelegateHandle(), MoveTemp(Result));
				ResultPtr->Get<FDelegateHandle>() = GetIdentityInterface()->OnLoginCompleteDelegates[LocalUserIndex].AddLambda(
					[WeakThis = TWeakPtr<IAuth>(AsShared()), this, ResultPtr, PlatformUserId = Op.GetParams().PlatformUserId, LocalUserIndex](int32 LocalUserNum, bool bWasSuccessful, const FUniqueNetId& UserId, const FString& Error) mutable
					{
						if (LocalUserIndex != LocalUserNum)
						{
							return;
						}

						TSharedPtr<IAuth> PinnedThis = WeakThis.Pin();
						if (PinnedThis.IsValid())
						{
							TPromise<TOnlineResult<FAuthLogin>>& OnlineResult = ResultPtr->Get<TPromise<TOnlineResult<FAuthLogin>>>();
							if (bWasSuccessful)
							{
								FOnlineAccountIdHandle Handle = static_cast<FOnlineServicesOSSAdapter&>(Services).GetAccountIdRegistry().FindOrAddHandle(UserId.AsShared());
								FAuthLogin::Result Result = { MakeShared<FAccountInfo>() };
								Result.AccountInfo->PlatformUserId = PlatformUserId;
								Result.AccountInfo->UserId = Handle;
								Result.AccountInfo->LoginStatus = ELoginStatus::LoggedIn;
								OnlineResult.EmplaceValue(MoveTemp(Result));
							}
							else
							{
								FOnlineError V2Error = Errors::Unknown(); // TODO: V1 to V2 error conversion/error from string conversion
								OnlineResult.EmplaceValue(MoveTemp(V2Error));
							}
						}

						GetIdentityInterface()->OnLoginCompleteDelegates[LocalUserIndex].Remove(ResultPtr->Get<FDelegateHandle>());
					});

				GetIdentityInterface()->Login(LocalUserIndex, Credentials);
			})
			.Then([this](TOnlineAsyncOp<FAuthLogin>& Op, const TOnlineResult<FAuthLogin>& Result)
			{
				if (Result.IsError())
				{
					Op.SetError(CopyTemp(Result.GetErrorValue()));
				}
				else
				{
					Op.SetResult(CopyTemp(Result.GetOkValue()));
				}
			})
			.Enqueue(GetSerialQueue());
	}
	return Op->GetHandle();
}

TOnlineAsyncOpHandle<FAuthLogout> FAuthOSSAdapter::Logout(FAuthLogout::Params&& Params)
{
	TSharedRef<TOnlineAsyncOp<FAuthLogout>> Op = GetOp<FAuthLogout>(MoveTemp(Params));

	if (!Op->IsReady())
	{
		int32 LocalUserIndex = GetLocalUserNum(Op->GetParams().LocalUserId);

		if (LocalUserIndex < 0 || LocalUserIndex >= MAX_LOCAL_PLAYERS)
		{
			Op->SetError(Errors::InvalidParams());
			return Op->GetHandle();
		}

		Op->Then([this, LocalUserIndex](TOnlineAsyncOp<FAuthLogout>& Op, TPromise<TOnlineResult<FAuthLogout>>&& Result)
			{
				TSharedPtr<TPair<FDelegateHandle, TPromise<TOnlineResult<FAuthLogout>>>> ResultPtr = MakeShared<TPair<FDelegateHandle, TPromise<TOnlineResult<FAuthLogout>>>>(FDelegateHandle(), MoveTemp(Result));
				ResultPtr->Get<FDelegateHandle>() = GetIdentityInterface()->OnLogoutCompleteDelegates[LocalUserIndex].AddLambda(
					[WeakThis = TWeakPtr<IAuth>(AsShared()), this, ResultPtr, LocalUserIndex](int32 LocalUserNum, bool bWasSuccessful) mutable
				{
					if (LocalUserIndex != LocalUserNum)
					{
						return;
					}

					TPromise<TOnlineResult<FAuthLogout>>& OnlineResult = ResultPtr->Get<TPromise<TOnlineResult<FAuthLogout>>>();

					TSharedPtr<IAuth> PinnedThis = WeakThis.Pin();
					if (PinnedThis.IsValid())
					{
						if (bWasSuccessful)
						{
							FAuthLogout::Result Result;
							OnlineResult.EmplaceValue(MoveTemp(Result));
						}
						else
						{
							FOnlineError V2Error = Errors::Unknown(); // TODO: V1 to V2 error conversion/error from string conversion
							OnlineResult.EmplaceValue(MoveTemp(V2Error));
						}
					}

					GetIdentityInterface()->OnLogoutCompleteDelegates[LocalUserIndex].Remove(ResultPtr->Get<FDelegateHandle>());
				});

				GetIdentityInterface()->Logout(LocalUserIndex);
			})
			.Then([this](TOnlineAsyncOp<FAuthLogout>& Op, const TOnlineResult<FAuthLogout>& Result)
				{
					if (Result.IsError())
					{
						Op.SetError(CopyTemp(Result.GetErrorValue()));
					}
					else
					{
						Op.SetResult(CopyTemp(Result.GetOkValue()));
					}
				})
				.Enqueue(GetSerialQueue());
	}
	return Op->GetHandle();
}

TOnlineAsyncOpHandle<FAuthGenerateAuth> FAuthOSSAdapter::GenerateAuth(FAuthGenerateAuth::Params&& Params)
{
	// TODO: If no cached auth token available, call Login with appropriate parameters to generate an auth token

	return Super::GenerateAuth(MoveTemp(Params));
}

TOnlineResult<FAuthGetAccountByPlatformUserId> FAuthOSSAdapter::GetAccountByPlatformUserId(FAuthGetAccountByPlatformUserId::Params&& Params)
{
	return Super::GetAccountByPlatformUserId(MoveTemp(Params));
}

TOnlineResult<FAuthGetAccountByAccountId> FAuthOSSAdapter::GetAccountByAccountId(FAuthGetAccountByAccountId::Params&& Params)
{
	return Super::GetAccountByAccountId(MoveTemp(Params));
}

FUniqueNetIdRef FAuthOSSAdapter::GetUniqueNetId(FOnlineAccountIdHandle AccountIdHandle) const
{
	return static_cast<FOnlineServicesOSSAdapter&>(Services).GetAccountIdRegistry().GetIdValue(AccountIdHandle);
}

int32 FAuthOSSAdapter::GetLocalUserNum(FOnlineAccountIdHandle AccountIdHandle) const
{
	FUniqueNetIdRef UniqueNetId = GetUniqueNetId(AccountIdHandle);
	if (UniqueNetId->IsValid())
	{
		return GetIdentityInterface()->GetLocalUserNumFromPlatformUserId(GetIdentityInterface()->GetPlatformUserIdFromUniqueNetId(*UniqueNetId));
	}
	return INDEX_NONE;
}

IOnlineIdentityPtr FAuthOSSAdapter::GetIdentityInterface() const
{
	return static_cast<FOnlineServicesOSSAdapter&>(Services).GetSubsystem().GetIdentityInterface();
}

/* UE::Online */ }
