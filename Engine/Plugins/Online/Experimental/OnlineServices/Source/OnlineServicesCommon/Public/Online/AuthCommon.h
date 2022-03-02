// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Online/Auth.h"
#include "Online/OnlineComponent.h"

namespace UE::Online {

class FOnlineServicesCommon;

class ONLINESERVICESCOMMON_API FAuthCommon : public TOnlineComponent<IAuth>
{
public:
	using Super = IAuth;

	FAuthCommon(FOnlineServicesCommon& InServices);

	// TOnlineComponent
	virtual void RegisterCommands() override;

	// IAuth
	virtual TOnlineAsyncOpHandle<FAuthLogin> Login(FAuthLogin::Params&& Params) override;
	virtual TOnlineAsyncOpHandle<FAuthLogout> Logout(FAuthLogout::Params&& Params) override;
	virtual TOnlineAsyncOpHandle<FAuthGenerateAuthToken> GenerateAuthToken(FAuthGenerateAuthToken::Params&& Params) override;
	virtual TOnlineResult<FAuthGetAuthToken> GetAuthToken(FAuthGetAuthToken::Params&& Params) override;
	virtual TOnlineAsyncOpHandle<FAuthGenerateAuthCode> GenerateAuthCode(FAuthGenerateAuthCode::Params&& Params) override;
	virtual TOnlineResult<FAuthGetAccountByPlatformUserId> GetAccountByPlatformUserId(FAuthGetAccountByPlatformUserId::Params&& Params) override;
	virtual TOnlineResult<FAuthGetAccountByAccountId> GetAccountByAccountId(FAuthGetAccountByAccountId::Params&& Params) override;
	virtual TOnlineEvent<void(const FLoginStatusChanged&)> OnLoginStatusChanged() override;

protected:
	TOnlineEventCallable<void(const FLoginStatusChanged&)> OnLoginStatusChangedEvent;
};

/* UE::Online */ }
