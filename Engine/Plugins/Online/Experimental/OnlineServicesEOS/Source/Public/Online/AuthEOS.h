// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Online/AuthCommon.h"

#if defined(EOS_PLATFORM_BASE_FILE_NAME)
#include EOS_PLATFORM_BASE_FILE_NAME
#endif
#include "eos_auth_types.h"
#include "eos_connect_types.h"

namespace UE::Online {

class FOnlineServicesEOS;

class ONLINESERVICESEOS_API FAuthEOS : public FAuthCommon
{
public:
	using Super = FAuthCommon;

	FAuthEOS(FOnlineServicesEOS& InOwningSubsystem, bool bInUseEAS);
	virtual void Initialize() override;
	virtual void PreShutdown() override;
	virtual TOnlineAsyncOpHandle<FAuthLogin> Login(FAuthLogin::Params&& Params) override;
	virtual TOnlineAsyncOpHandle<FAuthLogout> Logout(FAuthLogout::Params&& Params) override;
	virtual TOnlineResult<FAuthGetAccountByPlatformUserId> GetAccountByPlatformUserId(FAuthGetAccountByPlatformUserId::Params&& Params) override;
	virtual TOnlineResult<FAuthGetAccountByAccountId> GetAccountByAccountId(FAuthGetAccountByAccountId::Params&& Params) override;

	bool IsLoggedIn(const FOnlineAccountIdHandle& AccountId) const;

	TFuture<FOnlineAccountIdHandle> ResolveAccountId(const FOnlineAccountIdHandle& LocalUserId, const EOS_EpicAccountId EpicAccountId);
	TFuture<FOnlineAccountIdHandle> ResolveAccountId(const FOnlineAccountIdHandle& LocalUserId, const EOS_ProductUserId ProductUserId);
	TFuture<TArray<FOnlineAccountIdHandle>> ResolveAccountIds(const FOnlineAccountIdHandle& LocalUserId, const TArray<EOS_EpicAccountId>& EpicAccountIds);
	TFuture<TArray<FOnlineAccountIdHandle>> ResolveAccountIds(const FOnlineAccountIdHandle& LocalUserId, const TArray<EOS_ProductUserId>& ProductUserIds);

	TFunction<TFuture<FOnlineAccountIdHandle>(FOnlineAsyncOp& InAsyncOp, const EOS_EpicAccountId& EpicAccountId)> ResolveEpicIdFn();
	TFunction<TFuture<FOnlineAccountIdHandle>(FOnlineAsyncOp& InAsyncOp, const EOS_ProductUserId& ProductUserId)> ResolveProductIdFn();
	TFunction<TFuture<TArray<FOnlineAccountIdHandle>>(FOnlineAsyncOp& InAsyncOp, const TArray<EOS_EpicAccountId>& EpicAccountIds)> ResolveEpicIdsFn();
	TFunction<TFuture<TArray<FOnlineAccountIdHandle>>(FOnlineAsyncOp& InAsyncOp, const TArray<EOS_ProductUserId>& ProductUserIds)> ResolveProductIdsFn();

protected:
	TOnlineChainableAsyncOp<FAuthLogin, TSharedPtr<class FEOSConnectLoginCredentials>> LoginEAS(TOnlineAsyncOp<FAuthLogin>& InAsyncOp);
	TSharedPtr<class FEOSConnectLoginCredentials> MakeConnectLoginCredentials(TOnlineAsyncOp<FAuthLogin>& InAsyncOp);

	void OnEOSLoginStatusChanged(FOnlineAccountIdHandle LocalUserId, ELoginStatus PreviousStatus, ELoginStatus CurrentStatus);
	TResult<FOnlineAccountIdHandle, FOnlineError> GetAccountIdByPlatformUserId(FPlatformUserId PlatformUserId) const;

	void ProcessSuccessfulLogin(TOnlineAsyncOp<FAuthLogin>& InAsyncOp);

	class FAccountInfoEOS : public FAccountInfo
	{
	public:

	};

	TMap<FOnlineAccountIdHandle, TSharedRef<FAccountInfoEOS>> AccountInfos;

	EOS_HAuth AuthHandle;
	EOS_HConnect ConnectHandle;
	EOS_NotificationId NotifyLoginStatusChangedNotificationId = 0;

	/** Are we configured to use EAS? */
	bool bUseEAS = false;
};

/* UE::Online */ }
