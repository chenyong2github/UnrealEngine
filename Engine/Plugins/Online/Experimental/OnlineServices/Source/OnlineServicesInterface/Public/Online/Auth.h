// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Online/CoreOnline.h"
#include "Online/OnlineAsyncOpHandle.h"
#include "Online/OnlineMeta.h"

namespace UE::Online {

class FOnlineError;

enum class ELoginStatus : uint8
{
	/** Player has not logged in or chosen a local profile */
	NotLoggedIn,
	/** Player is using a local profile but is not logged in */
	UsingLocalProfile,
	/** Player has been validated by the platform specific authentication service */
	LoggedIn
};
ONLINESERVICESINTERFACE_API const TCHAR* LexToString(ELoginStatus Status);
ONLINESERVICESINTERFACE_API void LexFromString(ELoginStatus& OutStatus, const TCHAR* InStr);

class FAccountInfo
{
public:
	/** Local user num */
	FPlatformUserId PlatformUserId;
	/** Account id */
	FOnlineAccountIdHandle UserId;
	/** Login status */
	ELoginStatus LoginStatus;
	/** Display name */
	FString DisplayName;
	// TODO: Other fields
};

using FCredentialsToken = TVariant<FString, TArray<uint8>>;

struct FAuthLogin
{
	static constexpr TCHAR Name[] = TEXT("Login");

	struct Params
	{
		FPlatformUserId PlatformUserId = PLATFORMUSERID_NONE;
		FString CredentialsType;
		FString CredentialsId;
		FCredentialsToken CredentialsToken;
		TArray<FString> Scopes;
	};

	struct Result
	{
		TSharedRef<FAccountInfo> AccountInfo;

	public:
		Result() = delete; // cannot default construct due to TSharedRef
	};
};

struct FAuthLogout
{
	static constexpr TCHAR Name[] = TEXT("Logout");

	struct Params
	{
		FOnlineAccountIdHandle LocalUserId;
		bool bDestroyAuth = false;
	};

	struct Result
	{
	};
};

struct FAuthGenerateAuthToken
{
	static constexpr TCHAR Name[] = TEXT("GenerateAuthToken");

	struct Params
	{
		FOnlineAccountIdHandle LocalUserId;
		FString Type;
		TArray<FString> Scopes;
	};

	struct Result
	{
		FString Type;
		FCredentialsToken Token;
	};
};

struct FAuthGetAuthToken
{
	static constexpr TCHAR Name[] = TEXT("GetAuthToken");

	struct Params
	{
		FOnlineAccountIdHandle LocalUserId;
		FString Type;
		TArray<FString> Scopes;
	};

	struct Result
	{
		FString Token;
	};
};

struct FAuthGenerateAuthCode
{
	static constexpr TCHAR Name[] = TEXT("GenerateAuthCode");

	struct Params
	{
		FOnlineAccountIdHandle LocalUserId;
		FString Type;
		TArray<FString> Scopes;
	};

	struct Result
	{
		FString Code;
	};
};

struct FAuthGetAccountByPlatformUserId
{
	static constexpr TCHAR Name[] = TEXT("GetAccountByPlatformUserId");

	struct Params
	{
		FPlatformUserId PlatformUserId = PLATFORMUSERID_NONE;
	};

	struct Result
	{
		TSharedRef<FAccountInfo> AccountInfo;
	};
};

struct FAuthGetAccountByAccountId
{
	static constexpr TCHAR Name[] = TEXT("GetAccountByAccountId");

	struct Params
	{
		FOnlineAccountIdHandle LocalUserId;
	};

	struct Result
	{
		TSharedRef<FAccountInfo> AccountInfo;
	};
};

/** Struct for LoginStatusChanged event */
struct FLoginStatusChanged
{
	/** User id whose status has changed */
	FOnlineAccountIdHandle LocalUserId;
	/** Previous login status */
	ELoginStatus PreviousStatus;
	/** Current login status */
	ELoginStatus CurrentStatus;
};

class IAuth
{
public:
	/**
	 *
	 */
	virtual TOnlineAsyncOpHandle<FAuthLogin> Login(FAuthLogin::Params&& Params) = 0;

	/**
	 *
	 */
	virtual TOnlineAsyncOpHandle<FAuthLogout> Logout(FAuthLogout::Params&& Params) = 0;

	/**
	 *
	 */
	virtual TOnlineAsyncOpHandle<FAuthGenerateAuthToken> GenerateAuthToken(FAuthGenerateAuthToken::Params&& Params) = 0;

	/**
	 * Retrive a cached auth token
	 */
	virtual TOnlineResult<FAuthGetAuthToken> GetAuthToken(FAuthGetAuthToken::Params&& Params) = 0;

	/**
	 *
	 */
	virtual TOnlineAsyncOpHandle<FAuthGenerateAuthCode> GenerateAuthCode(FAuthGenerateAuthCode::Params&& Params) = 0;

	/**
	 * Get logged in user by local user num
	 */ 
	virtual TOnlineResult<FAuthGetAccountByPlatformUserId> GetAccountByPlatformUserId(FAuthGetAccountByPlatformUserId::Params&& Params) = 0;

	/**
	 * Get logged in user by account id
	 */
	virtual TOnlineResult<FAuthGetAccountByAccountId> GetAccountByAccountId(FAuthGetAccountByAccountId::Params&& Params) = 0;

	/**
	 * Event triggered when a local user's login status changes
	 */
	virtual TOnlineEvent<void(const FLoginStatusChanged&)> OnLoginStatusChanged() = 0;
};


namespace Meta {
// TODO: Move to Auth_Meta.inl file?

BEGIN_ONLINE_STRUCT_META(FAccountInfo)
	ONLINE_STRUCT_FIELD(FAccountInfo, PlatformUserId),
	ONLINE_STRUCT_FIELD(FAccountInfo, UserId),
	ONLINE_STRUCT_FIELD(FAccountInfo, LoginStatus)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FAuthLogin::Params)
	ONLINE_STRUCT_FIELD(FAuthLogin::Params, PlatformUserId),
	ONLINE_STRUCT_FIELD(FAuthLogin::Params, CredentialsType),
	ONLINE_STRUCT_FIELD(FAuthLogin::Params, CredentialsId),
	ONLINE_STRUCT_FIELD(FAuthLogin::Params, CredentialsToken),
	ONLINE_STRUCT_FIELD(FAuthLogin::Params, Scopes)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FAuthLogin::Result)
	ONLINE_STRUCT_FIELD(FAuthLogin::Result, AccountInfo)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FAuthLogout::Params)
	ONLINE_STRUCT_FIELD(FAuthLogout::Params, LocalUserId)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FAuthLogout::Result)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FAuthGenerateAuthToken::Params)
	ONLINE_STRUCT_FIELD(FAuthGenerateAuthToken::Params, LocalUserId),
	ONLINE_STRUCT_FIELD(FAuthGenerateAuthToken::Params, Type),
	ONLINE_STRUCT_FIELD(FAuthGenerateAuthToken::Params, Scopes)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FAuthGenerateAuthToken::Result)
	ONLINE_STRUCT_FIELD(FAuthGenerateAuthToken::Result, Type),
	ONLINE_STRUCT_FIELD(FAuthGenerateAuthToken::Result, Token)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FAuthGetAuthToken::Params)
	ONLINE_STRUCT_FIELD(FAuthGetAuthToken::Params, LocalUserId),
	ONLINE_STRUCT_FIELD(FAuthGetAuthToken::Params, Type),
	ONLINE_STRUCT_FIELD(FAuthGetAuthToken::Params, Scopes)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FAuthGetAuthToken::Result)
	ONLINE_STRUCT_FIELD(FAuthGetAuthToken::Result, Token)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FAuthGenerateAuthCode::Params)
ONLINE_STRUCT_FIELD(FAuthGenerateAuthCode::Params, LocalUserId),
ONLINE_STRUCT_FIELD(FAuthGenerateAuthCode::Params, Type),
ONLINE_STRUCT_FIELD(FAuthGenerateAuthCode::Params, Scopes)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FAuthGenerateAuthCode::Result)
	ONLINE_STRUCT_FIELD(FAuthGenerateAuthCode::Result, Code)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FAuthGetAccountByPlatformUserId::Params)
	ONLINE_STRUCT_FIELD(FAuthGetAccountByPlatformUserId::Params, PlatformUserId)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FAuthGetAccountByPlatformUserId::Result)
	ONLINE_STRUCT_FIELD(FAuthGetAccountByPlatformUserId::Result, AccountInfo)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FAuthGetAccountByAccountId::Params)
	ONLINE_STRUCT_FIELD(FAuthGetAccountByAccountId::Params, LocalUserId)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FAuthGetAccountByAccountId::Result)
	ONLINE_STRUCT_FIELD(FAuthGetAccountByAccountId::Result, AccountInfo)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FLoginStatusChanged)
	ONLINE_STRUCT_FIELD(FLoginStatusChanged, LocalUserId),
	ONLINE_STRUCT_FIELD(FLoginStatusChanged, PreviousStatus),
	ONLINE_STRUCT_FIELD(FLoginStatusChanged, CurrentStatus)
END_ONLINE_STRUCT_META()

/* Meta*/ }

/* UE::Online */ }
