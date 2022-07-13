// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Online/AuthCommon.h"

#include "OnlineSubsystemTypes.h"

class IOnlineSubsystem;
class IOnlineIdentity;
using IOnlineIdentityPtr = TSharedPtr<IOnlineIdentity>;

namespace UE::Online {

class FOnlineServicesOSSAdapter;

struct FAccountInfoOSSAdapter final : public FAccountInfo
{
	FUniqueNetIdPtr UniqueNetId;
	int32 LocalUserNum = INDEX_NONE;
};

class FAccountInfoRegistryOSSAdapter final : public FAccountInfoRegistry
{
public:
	using Super = FAccountInfoRegistry;

	virtual ~FAccountInfoRegistryOSSAdapter() = default;

	TSharedPtr<FAccountInfoOSSAdapter> Find(FPlatformUserId PlatformUserId) const;
	TSharedPtr<FAccountInfoOSSAdapter> Find(FOnlineAccountIdHandle AccountIdHandle) const;

	void Register(const TSharedRef<FAccountInfoOSSAdapter>&UserAuthData);
	void Unregister(FOnlineAccountIdHandle AccountId);
};

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
	virtual TOnlineAsyncOpHandle<FAuthQueryExternalServerAuthTicket> QueryExternalServerAuthTicket(FAuthQueryExternalServerAuthTicket::Params&& Params) override;
	virtual TOnlineAsyncOpHandle<FAuthQueryExternalAuthToken> QueryExternalAuthToken(FAuthQueryExternalAuthToken::Params&& Params) override;

	FUniqueNetIdPtr GetUniqueNetId(FOnlineAccountIdHandle AccountIdHandle) const;
	FOnlineAccountIdHandle GetAccountIdHandle(const FUniqueNetIdRef& UniqueNetId) const;
	int32 GetLocalUserNum(FOnlineAccountIdHandle AccountIdHandle) const;

protected:
	virtual const FAccountInfoRegistry& GetAccountInfoRegistry() const override;

	const FOnlineServicesOSSAdapter& GetOnlineServicesOSSAdapter() const;
	FOnlineServicesOSSAdapter& GetOnlineServicesOSSAdapter();
	const IOnlineSubsystem& GetSubsystem() const;
	IOnlineIdentityPtr GetIdentityInterface() const;

	struct FHandleLoginStatusChangedImpl
	{
		static constexpr TCHAR Name[] = TEXT("HandleLoginStatusChangedImpl");

		struct Params
		{
			FPlatformUserId PlatformUserId;
			FOnlineAccountIdHandle AccountId;
			ELoginStatus NewLoginStatus;
		};

		struct Result
		{
		};
	};

	TOnlineAsyncOpHandle<FHandleLoginStatusChangedImpl> HandleLoginStatusChangedImplOp(FHandleLoginStatusChangedImpl::Params&& Params);

	FAccountInfoRegistryOSSAdapter AccountInfoRegistryOSSAdapter;
	FDelegateHandle OnLoginStatusChangedHandle[MAX_LOCAL_PLAYERS];
};

/* UE::Online */ }
