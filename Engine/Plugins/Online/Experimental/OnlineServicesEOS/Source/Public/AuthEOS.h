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

	FAuthEOS(FOnlineServicesEOS& InOwningSubsystem);
	virtual void Initialize() override;
	virtual void PreShutdown() override;
	virtual TOnlineAsyncOpHandle<FAuthLogin> Login(FAuthLogin::Params&& Params) override;
	virtual TOnlineAsyncOpHandle<FAuthLogout> Logout(FAuthLogout::Params&& Params) override;
	virtual TOnlineAsyncOpHandle<FAuthGenerateAuth> GenerateAuth(FAuthGenerateAuth::Params&& Params) override;
	virtual TOnlineResult<FAuthGetAccountByLocalUserNum> GetAccountByLocalUserNum(FAuthGetAccountByLocalUserNum::Params&& Params) override;
	virtual TOnlineResult<FAuthGetAccountByAccountId> GetAccountByAccountId(FAuthGetAccountByAccountId::Params&& Params) override;

	bool IsLoggedIn(const FAccountId& AccountId) const;

protected:
	void OnEOSLoginStatusChanged(FAccountId LocalUserId, ELoginStatus PreviousStatus, ELoginStatus CurrentStatus);
	TResult<FAccountId, FOnlineError> GetAccountIdByLocalUserNum(int32 LocalUserNum) const;

	void ProcessSuccessfulLogin(TOnlineAsyncOp<FAuthLogin>& InAsyncOp);

	class FAccountInfoEOS : public FAccountInfo
	{
	public:

	};

	TMap<FAccountId, TSharedRef<FAccountInfoEOS>> AccountInfos;

	EOS_HAuth AuthHandle;
	EOS_HConnect ConnectHandle;
	EOS_NotificationId NotifyLoginStatusChangedNotificationId = 0;
};

/* UE::Online */ }
