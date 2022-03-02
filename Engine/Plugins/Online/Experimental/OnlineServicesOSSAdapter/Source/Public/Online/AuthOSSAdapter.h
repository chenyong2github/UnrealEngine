// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Online/AuthCommon.h"

#include "OnlineSubsystemTypes.h"

class IOnlineIdentity;
using IOnlineIdentityPtr = TSharedPtr<IOnlineIdentity>;

namespace UE::Online {

class FAuthOSSAdapter : public FAuthCommon
{
public:
	using Super = FAuthCommon;

	using FAuthCommon::FAuthCommon;

	// IOnlineComponent
	virtual void PostInitialize() override;
	virtual void PreShutdown() override;

	// IAuth
	virtual TOnlineAsyncOpHandle<FAuthLogin> Login(FAuthLogin::Params&& Params) override;
	virtual TOnlineAsyncOpHandle<FAuthLogout> Logout(FAuthLogout::Params&& Params) override;
	virtual TOnlineAsyncOpHandle<FAuthGenerateAuthToken> GenerateAuthToken(FAuthGenerateAuthToken::Params&& Params) override;
	virtual TOnlineAsyncOpHandle<FAuthGenerateAuthCode> GenerateAuthCode(FAuthGenerateAuthCode::Params&& Params) override;
	virtual TOnlineResult<FAuthGetAccountByPlatformUserId> GetAccountByPlatformUserId(FAuthGetAccountByPlatformUserId::Params&& Params) override;
	virtual TOnlineResult<FAuthGetAccountByAccountId> GetAccountByAccountId(FAuthGetAccountByAccountId::Params&& Params) override;

	FUniqueNetIdRef GetUniqueNetId(FOnlineAccountIdHandle AccountIdHandle) const;
	int32 GetLocalUserNum(FOnlineAccountIdHandle AccountIdHandle) const;

	IOnlineIdentityPtr GetIdentityInterface() const;

protected:
	FDelegateHandle OnLoginStatusChangedHandle[MAX_LOCAL_PLAYERS];
};

/* UE::Online */ }
