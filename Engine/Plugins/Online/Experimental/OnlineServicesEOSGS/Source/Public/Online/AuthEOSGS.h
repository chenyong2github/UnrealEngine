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

const int EOS_STRING_BUFFER_LENGTH = 256;
const int EOS_MAX_TOKEN_SIZE = 4096;

class FOnlineServicesEOSGS;

/* Base class for EOS_Connect login credentials */
class ONLINESERVICESEOSGS_API FEOSConnectLoginCredentials : public EOS_Connect_Credentials
{
public:
	FEOSConnectLoginCredentials();
	virtual ~FEOSConnectLoginCredentials() = default;
};

/* EOS_Connect login credentials using EAS auth */
class ONLINESERVICESEOSGS_API FEOSConnectLoginCredentialsEAS
	: public FEOSConnectLoginCredentials
	, public FNoncopyable
{
public:
	FEOSConnectLoginCredentialsEAS(EOS_Auth_Token* InEASToken);
	virtual ~FEOSConnectLoginCredentialsEAS();
protected:
	EOS_Auth_Token* EASToken = nullptr;
};

/* EOS_Auth login credentials */
struct ONLINESERVICESEOSGS_API FEOSAuthCredentials : public EOS_Auth_Credentials
{
	FEOSAuthCredentials();
	FEOSAuthCredentials(EOS_EExternalCredentialType InExternalType, const TArray<uint8>& InToken);

	FEOSAuthCredentials(const FEOSAuthCredentials& Other);
	FEOSAuthCredentials& operator=(FEOSAuthCredentials& Other);

	void SetToken(const FCredentialsToken& InToken);

	char IdAnsi[EOS_STRING_BUFFER_LENGTH];
	char TokenAnsi[EOS_MAX_TOKEN_SIZE];
};

class ONLINESERVICESEOSGS_API FAuthEOSGS : public FAuthCommon
{
public:
	using Super = FAuthCommon;

	FAuthEOSGS(FOnlineServicesEOSGS& InOwningSubsystem);
	virtual ~FAuthEOSGS() = default;

	// Begin IOnlineComponent
	virtual void Initialize() override;
	// End IOnlineComponent

	// Begin IAuth
	virtual TOnlineAsyncOpHandle<FAuthLogin> Login(FAuthLogin::Params&& Params) override;
	virtual TOnlineAsyncOpHandle<FAuthLogout> Logout(FAuthLogout::Params&& Params) override;
	virtual TOnlineResult<FAuthGetAccountByPlatformUserId> GetAccountByPlatformUserId(FAuthGetAccountByPlatformUserId::Params&& Params) override;
	virtual TOnlineResult<FAuthGetAccountByAccountId> GetAccountByAccountId(FAuthGetAccountByAccountId::Params&& Params) override;
	// End IAuth

	// Begin FAuthEOSGS
	virtual TFuture<FOnlineAccountIdHandle> ResolveAccountId(const FOnlineAccountIdHandle& LocalUserId, const EOS_ProductUserId ProductUserId);
	virtual TFuture<TArray<FOnlineAccountIdHandle>> ResolveAccountIds(const FOnlineAccountIdHandle& LocalUserId, const TArray<EOS_ProductUserId>& ProductUserIds);
	virtual TFunction<TFuture<FOnlineAccountIdHandle>(FOnlineAsyncOp& InAsyncOp, const EOS_ProductUserId& ProductUserId)> ResolveProductIdFn();
	virtual TFunction<TFuture<TArray<FOnlineAccountIdHandle>>(FOnlineAsyncOp& InAsyncOp, const TArray<EOS_ProductUserId>& ProductUserIds)> ResolveProductIdsFn();
	// End FAuthEOSGS

	bool IsLoggedIn(const FOnlineAccountIdHandle& AccountId) const;

protected:
	TOnlineChainableAsyncOp<FAuthLogin, TSharedPtr<FEOSConnectLoginCredentials>> LoginEAS(TOnlineAsyncOp<FAuthLogin>& InAsyncOp);
	void ProcessSuccessfulLogin(TOnlineAsyncOp<FAuthLogin>& InAsyncOp, const EOS_ProductUserId ProductUserId);

	TResult<FOnlineAccountIdHandle, FOnlineError> GetAccountIdByPlatformUserId(FPlatformUserId PlatformUserId) const;

	static FOnlineAccountIdHandle CreateAccountId(const EOS_ProductUserId ProductUserId);

	using FAccountInfoEOS = FAccountInfo;
	TMap<FOnlineAccountIdHandle, TSharedRef<FAccountInfoEOS>> AccountInfos;

	EOS_HAuth AuthHandle = nullptr;
	EOS_HConnect ConnectHandle = nullptr;
};

ELoginStatus ONLINESERVICESEOSGS_API ToELoginStatus(EOS_ELoginStatus InStatus);

/* UE::Online */ }
