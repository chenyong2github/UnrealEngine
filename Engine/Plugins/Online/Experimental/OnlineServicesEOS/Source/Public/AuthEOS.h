// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_EOS_SDK
#include "CoreMinimal.h"
#include "Online/AuthCommon.h"

#include "eos_auth_types.h"

namespace UE::Online {

class FOnlineServicesEOS;

class ONLINESERVICESEOS_API FAuthEOS : public FAuthCommon
{
public:
	using Super = FAuthCommon;

	FAuthEOS(FOnlineServicesEOS& InOwningSubsystem);
	virtual TOnlineAsyncOpHandle<FAuthLogin> Login(FAuthLogin::Params&& Params) override;
	virtual TOnlineAsyncOpHandle<FAuthLogout> Logout(FAuthLogout::Params&& Params) override;
	virtual TOnlineAsyncOpHandle<FAuthGenerateAuth> GenerateAuth(FAuthGenerateAuth::Params&& Params) override;
	virtual TOnlineResult<FAuthGetAccountByLocalUserNum::Result> GetAccountByLocalUserNum(FAuthGetAccountByLocalUserNum::Params&& Params) override;
	virtual TOnlineResult<FAuthGetAccountByAccountId::Result> GetAccountByAccountId(FAuthGetAccountByAccountId::Params&& Params) override;

	bool IsLoggedIn(const FAccountId& AccountId) const;

protected:
	TOnlineResult<FAccountId> GetAccountIdByLocalUserNum(int32 LocalUserNum) const;

	class FAccountInfoEOS : public FAccountInfo
	{
	public:

	};

	TMap<FAccountId, TSharedRef<FAccountInfoEOS>> AccountInfos;

	EOS_HAuth AuthHandle;
};

/* UE::Online */ }
#endif // WITH_EOS_SDK