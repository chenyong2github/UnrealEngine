// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Online/AuthCommon.h"
#include "Online/OnlineServicesEOSGSTypes.h"

#if defined(EOS_PLATFORM_BASE_FILE_NAME)
#include EOS_PLATFORM_BASE_FILE_NAME
#endif
#include "eos_auth_types.h"
#include "eos_connect_types.h"

namespace UE::Online {

const int EOS_STRING_BUFFER_LENGTH = 256;
const int EOS_MAX_TOKEN_SIZE = 4096;

class FOnlineServicesEOSGS;
struct FAccountInfoEOS;
class FAccountInfoRegistryEOS;

struct FAccountInfoEOS final : public FAccountInfo
{
	FTSTicker::FDelegateHandle RestoreLoginTimer;
	EOS_EpicAccountId EpicAccountId = nullptr;
	EOS_ProductUserId ProductUserId = nullptr;
};

class ONLINESERVICESEOSGS_API FAccountInfoRegistryEOS final : public FAccountInfoRegistry
{
public:
	using Super = FAccountInfoRegistry;

	virtual ~FAccountInfoRegistryEOS() = default;

	TSharedPtr<FAccountInfoEOS> Find(FPlatformUserId PlatformUserId) const;
	TSharedPtr<FAccountInfoEOS> Find(FOnlineAccountIdHandle AccountIdHandle) const;
	TSharedPtr<FAccountInfoEOS> Find(EOS_EpicAccountId EpicAccountId) const;
	TSharedPtr<FAccountInfoEOS> Find(EOS_ProductUserId ProductUserId) const;

	void Register(const TSharedRef<FAccountInfoEOS>& UserAuthData);
	void Unregister(FOnlineAccountIdHandle AccountId);

protected:
	virtual void DoRegister(const TSharedRef<FAccountInfo>& AccountInfo);
	virtual void DoUnregister(const TSharedRef<FAccountInfo>& AccountInfo);

private:
	TMap<EOS_EpicAccountId, TSharedRef<FAccountInfoEOS>> AuthDataByEpicAccountId;
	TMap<EOS_ProductUserId, TSharedRef<FAccountInfoEOS>> AuthDataByProductUserId;
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
	virtual void PreShutdown() override;
	virtual TOnlineAsyncOpHandle<FAuthLogin> Login(FAuthLogin::Params&& Params) override;
	virtual TOnlineAsyncOpHandle<FAuthLogout> Logout(FAuthLogout::Params&& Params) override;
	virtual TOnlineAsyncOpHandle<FAuthQueryVerifiedAuthTicket> QueryVerifiedAuthTicket(FAuthQueryVerifiedAuthTicket::Params&& Params) override;
	virtual TOnlineAsyncOpHandle<FAuthCancelVerifiedAuthTicket> CancelVerifiedAuthTicket(FAuthCancelVerifiedAuthTicket::Params&& Params) override;
	virtual TOnlineAsyncOpHandle<FAuthBeginVerifiedAuthSession> BeginVerifiedAuthSession(FAuthBeginVerifiedAuthSession::Params&& Params) override;
	virtual TOnlineAsyncOpHandle<FAuthEndVerifiedAuthSession> EndVerifiedAuthSession(FAuthEndVerifiedAuthSession::Params&& Params) override;
	// End IAuth

	// Begin FAuthEOSGS
	virtual TFuture<FOnlineAccountIdHandle> ResolveAccountId(const FOnlineAccountIdHandle& LocalUserId, const EOS_ProductUserId ProductUserId);
	virtual TFuture<TArray<FOnlineAccountIdHandle>> ResolveAccountIds(const FOnlineAccountIdHandle& LocalUserId, const TArray<EOS_ProductUserId>& ProductUserIds);
	virtual TFunction<TFuture<FOnlineAccountIdHandle>(FOnlineAsyncOp& InAsyncOp, const EOS_ProductUserId& ProductUserId)> ResolveProductIdFn();
	virtual TFunction<TFuture<TArray<FOnlineAccountIdHandle>>(FOnlineAsyncOp& InAsyncOp, const TArray<EOS_ProductUserId>& ProductUserIds)> ResolveProductIdsFn();
	// End FAuthEOSGS

protected:
	// internal operations.

	struct FLoginEASImpl
	{
		static constexpr TCHAR Name[] = TEXT("LoginEASImpl");

		struct Params
		{
			FPlatformUserId PlatformUserId = PLATFORMUSERID_NONE;
			FName CredentialsType;
			FString CredentialsId;
			TVariant<FString, FExternalAuthToken> CredentialsToken;
			TArray<FString> Scopes;
		};

		struct Result
		{
			EOS_EpicAccountId EpicAccountId = nullptr;
		};
	};

	TFuture<TDefaultErrorResult<FLoginEASImpl>> LoginEASImpl(const FLoginEASImpl::Params& LoginParams);

	struct FLogoutEASImpl
	{
		static constexpr TCHAR Name[] = TEXT("LogoutEASImpl");

		struct Params
		{
			EOS_EpicAccountId EpicAccountId = nullptr;
		};

		struct Result
		{
		};
	};

	TFuture<TDefaultErrorResult<FLogoutEASImpl>> LogoutEASImpl(const FLogoutEASImpl::Params& LogoutParams);

	struct FGetExternalAuthTokenImpl
	{
		static constexpr TCHAR Name[] = TEXT("GetExternalAuthTokenImpl");

		struct Params
		{
			EOS_EpicAccountId EpicAccountId = nullptr;
		};

		struct Result
		{
			FExternalAuthToken Token;
		};
	};

	TDefaultErrorResult<FGetExternalAuthTokenImpl> GetExternalAuthTokenImpl(const FGetExternalAuthTokenImpl::Params& Params);

	struct FLoginConnectImpl
	{
		static constexpr TCHAR Name[] = TEXT("LoginConnectImpl");

		struct Params
		{
			FPlatformUserId PlatformUserId = PLATFORMUSERID_NONE;
			FExternalAuthToken ExternalAuthToken;
		};

		struct Result
		{
			EOS_ProductUserId ProductUserId = nullptr;
		};
	};

	TFuture<TDefaultErrorResult<FLoginConnectImpl>> LoginConnectImpl(const FLoginConnectImpl::Params& LoginParams);

	struct FConnectLoginRecoveryImpl
	{
		static constexpr TCHAR Name[] = TEXT("ConnectLoginRecovery");

		struct Params
		{
			/** The Epic Account ID of the local user whose connect login should be recovered. */
			EOS_EpicAccountId LocalUserId = nullptr;
		};

		struct Result
		{
		};
	};

	TOnlineAsyncOpHandle<FConnectLoginRecoveryImpl> ConnectLoginRecoveryImplOp(FConnectLoginRecoveryImpl::Params&& Params);

	struct FHandleConnectLoginStatusChangedImpl
	{
		static constexpr TCHAR Name[] = TEXT("HandleConnectLoginStatusChangedImpl");

		struct Params
		{
			/** The Product User ID of the local player whose status has changed. */
			EOS_ProductUserId LocalUserId = nullptr;
			/** The status prior to the change. */
			EOS_ELoginStatus PreviousStatus = EOS_ELoginStatus::EOS_LS_NotLoggedIn;
			/** The status at the time of the notification. */
			EOS_ELoginStatus CurrentStatus = EOS_ELoginStatus::EOS_LS_NotLoggedIn;
		};

		struct Result
		{
		};
	};

	TOnlineAsyncOpHandle<FHandleConnectLoginStatusChangedImpl> HandleConnectLoginStatusChangedImplOp(FHandleConnectLoginStatusChangedImpl::Params&& Params);

	struct FHandleConnectAuthNotifyExpirationImpl
	{
		static constexpr TCHAR Name[] = TEXT("HandleConnectAuthNotifyExpirationImpl");

		struct Params
		{
			/** The Product User ID of the local player whose status has changed. */
			EOS_ProductUserId LocalUserId = nullptr;
		};

		struct Result
		{
		};
	};

	TOnlineAsyncOpHandle<FHandleConnectAuthNotifyExpirationImpl> HandleConnectAuthNotifyExpirationImplOp(FHandleConnectAuthNotifyExpirationImpl::Params&& Params);

	struct FHandleEASLoginStatusChangedImpl
	{
		static constexpr TCHAR Name[] = TEXT("HandleEASLoginStatusChangedImpl");

		struct Params
		{
			/** The Epic Account ID of the local user whose status has changed */
			EOS_EpicAccountId LocalUserId = nullptr;
			/** The status prior to the change */
			EOS_ELoginStatus PrevStatus = EOS_ELoginStatus::EOS_LS_NotLoggedIn;
			/** The status at the time of the notification */
			EOS_ELoginStatus CurrentStatus = EOS_ELoginStatus::EOS_LS_NotLoggedIn;
		};

		struct Result
		{
		};
	};

	TOnlineAsyncOpHandle<FHandleEASLoginStatusChangedImpl> HandleEASLoginStatusChangedImplOp(FHandleEASLoginStatusChangedImpl::Params&& Params);

protected:
	// Service event handling.
	void RegisterHandlers();
	void UnregisterHandlers();
	void OnConnectLoginStatusChanged(const EOS_Connect_LoginStatusChangedCallbackInfo* Data);
	void OnConnectAuthNotifyExpiration(const EOS_Connect_AuthExpirationCallbackInfo* Data);
	void OnEASLoginStatusChanged(const EOS_Auth_LoginStatusChangedCallbackInfo* Data);

protected:
	virtual const FAccountInfoRegistry& GetAccountInfoRegistry() const override;

	void InitializeConnectLoginRecoveryTimer(const TSharedRef<FAccountInfoEOS>& UserAuthData);

	static FOnlineAccountIdHandle CreateAccountId(const EOS_ProductUserId ProductUserId);

	EOS_HAuth AuthHandle = nullptr;
	EOS_HConnect ConnectHandle = nullptr;
	EOSEventRegistrationPtr OnConnectLoginStatusChangedEOSEventRegistration;
	EOSEventRegistrationPtr OnConnectAuthNotifyExpirationEOSEventRegistration;
	FAccountInfoRegistryEOS AccountInfoRegistryEOS;
};

/* UE::Online */ }
