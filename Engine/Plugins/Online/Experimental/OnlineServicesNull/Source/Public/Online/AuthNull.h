// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Online/AuthCommon.h"

namespace UE::Online {

class FOnlineAccountIdString
{
public:
	FString Data;
	int32 AccountIndex;
	FOnlineAccountIdHandle Handle;
};

class FOnlineAccountIdRegistryNull : public IOnlineAccountIdRegistry
{
public:
	static FOnlineAccountIdRegistryNull& Get();

	FOnlineAccountIdHandle Find(FString UserId) const;
	FOnlineAccountIdHandle Find(FPlatformUserId UserId) const;
	FOnlineAccountIdHandle Find(int32 UserId) const;

	FOnlineAccountIdHandle Create(FString UserId, FPlatformUserId LocalUserIndex = PLATFORMUSERID_NONE);

	// Begin IOnlineAccountIdRegistry
	virtual FString ToLogString(const FOnlineAccountIdHandle& Handle) const override;
	virtual TArray<uint8> ToReplicationData(const FOnlineAccountIdHandle& Handle) const override;
	virtual FOnlineAccountIdHandle FromReplicationData(const TArray<uint8>& ReplicationString) override;
	// End IOnlineAccountIdRegistry

	virtual ~FOnlineAccountIdRegistryNull() = default;

private:
	const FOnlineAccountIdString* GetInternal(const FOnlineAccountIdHandle& Handle) const;

	// how much of these are actually necessary?
	TArray<FOnlineAccountIdString> Ids;
	TMap<FString, FOnlineAccountIdString> StringToId; 
	TMap<int32, FOnlineAccountIdString> LocalUserMap;

};

class FOnlineServicesNull;

class ONLINESERVICESNULL_API FAuthNull : public FAuthCommon
{
public:
	using Super = FAuthCommon;

	FAuthNull(FOnlineServicesNull& InOwningSubsystem);
	virtual void Initialize() override;
	virtual void PreShutdown() override;
	virtual TOnlineAsyncOpHandle<FAuthLogin> Login(FAuthLogin::Params&& Params) override;
	virtual TOnlineAsyncOpHandle<FAuthLogout> Logout(FAuthLogout::Params&& Params) override;
	virtual TOnlineResult<FAuthGetAccountByPlatformUserId> GetAccountByPlatformUserId(FAuthGetAccountByPlatformUserId::Params&& Params) override;
	virtual TOnlineResult<FAuthGetAccountByAccountId> GetAccountByAccountId(FAuthGetAccountByAccountId::Params&& Params) override;

	bool IsLoggedIn(const FOnlineAccountIdHandle& AccountId) const;

protected:
	FString GenerateRandomUserId(int32 LocalUserNum);
	TResult<FOnlineAccountIdHandle, FOnlineError> GetAccountIdByLocalUserNum(int32 LocalUserNum) const;

	class FAccountInfoNull : public FAccountInfo
	{
	};

	TMap<FOnlineAccountIdHandle, TSharedRef<FAccountInfoNull>> AccountInfos;
};

/* UE::Online */ }
