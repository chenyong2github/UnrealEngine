// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Online/AuthCommon.h"
#include "Containers/Ticker.h"

#if WITH_EOS_SDK
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

protected:
	TOnlineResult<FAccountId> GetAccountIdByLocalUserNum(int32 LocalUserNum) const;
	virtual void Tick(float DeltaTime) override;

	class FAccountInfoEOS : public FAccountInfo
	{
	public:

	};

	TMap<FAccountId, TSharedRef<FAccountInfoEOS>> AccountInfos;
};

/* UE::Online */ }
#endif
