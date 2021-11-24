// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Online/OnlineAsyncOpHandle.h"
#include "Online/OnlineId.h"
#include "Online/OnlineMeta.h"

namespace UE::Online {

class FOnlineError;

enum class ELoginStatus
{
	/** Player has not logged in or chosen a local profile */
	NotLoggedIn,
	/** Player is using a local profile but is not logged in */
	UsingLocalProfile,
	/** Player has been validated by the platform specific authentication service */
	LoggedIn
};

inline const TCHAR* LexToString(ELoginStatus LoginStatus)
{
	switch (LoginStatus)
	{
		case ELoginStatus::NotLoggedIn:			return TEXT("NoLoggedIn"); break;
		case ELoginStatus::UsingLocalProfile:	return TEXT("UsingLocalProfile"); break;
		case ELoginStatus::LoggedIn:			return TEXT("LoggedIn"); break;
		default:								return TEXT("Unknown"); break;
	}
}

class FAccountInfo
{
public:
	/** Local user num */
	int32 LocalUserNum;
	/** Account id */
	FAccountId UserId;
	/** Login status */
	ELoginStatus LoginStatus;
	// TODO: Other fields
};

struct FAuthLogin
{
	static constexpr TCHAR Name[] = TEXT("Login");

	struct Params
	{
		int32 LocalUserNum;
		FString CredentialsType;
		FString CredentialsId;
		FString CredentialsToken;
		TArray<FString> Scopes;
	};

	struct Result
	{
		TSharedRef<FAccountInfo> AccountInfo;
	};
};

struct FAuthLogout
{
	static constexpr TCHAR Name[] = TEXT("Logout");

	struct Params
	{
		FAccountId LocalUserId;
		bool bDestroyAuth = false;
	};

	struct Result
	{
	};
};

struct FAuthGenerateAuth
{
	static constexpr TCHAR Name[] = TEXT("GenerateAuth");

	struct Params
	{
		FAccountId LocalUserId;
		FString Type;
		TArray<FString> Scopes;
	};

	struct Result
	{
	};
};

struct FAuthGetAccountByLocalUserNum
{
	static constexpr TCHAR Name[] = TEXT("GetAccountByLocalUserNum");

	struct Params
	{
		int32 LocalUserNum;
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
		FAccountId LocalUserId;
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
	FAccountId LocalUserId;
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
	virtual TOnlineAsyncOpHandle<FAuthGenerateAuth> GenerateAuth(FAuthGenerateAuth::Params&& Params) = 0;

	/**
	 * Get logged in user by local user num
	 */ 
	virtual TOnlineResult<FAuthGetAccountByLocalUserNum> GetAccountByLocalUserNum(FAuthGetAccountByLocalUserNum::Params&& Params) = 0;

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
	ONLINE_STRUCT_FIELD(FAccountInfo, LocalUserNum),
	ONLINE_STRUCT_FIELD(FAccountInfo, UserId),
	ONLINE_STRUCT_FIELD(FAccountInfo, LoginStatus)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FAuthLogin::Params)
	ONLINE_STRUCT_FIELD(FAuthLogin::Params, LocalUserNum),
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

BEGIN_ONLINE_STRUCT_META(FAuthGenerateAuth::Params)
	ONLINE_STRUCT_FIELD(FAuthGenerateAuth::Params, LocalUserId),
	ONLINE_STRUCT_FIELD(FAuthGenerateAuth::Params, Type),
	ONLINE_STRUCT_FIELD(FAuthGenerateAuth::Params, Scopes)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FAuthGenerateAuth::Result)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FAuthGetAccountByLocalUserNum::Params)
	ONLINE_STRUCT_FIELD(FAuthGetAccountByLocalUserNum::Params, LocalUserNum)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FAuthGetAccountByLocalUserNum::Result)
	ONLINE_STRUCT_FIELD(FAuthGetAccountByLocalUserNum::Result, AccountInfo)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FAuthGetAccountByAccountId::Params)
	ONLINE_STRUCT_FIELD(FAuthGetAccountByAccountId::Params, LocalUserId)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FAuthGetAccountByAccountId::Result)
	ONLINE_STRUCT_FIELD(FAuthGetAccountByAccountId::Result, AccountInfo)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FLoginStatusChanged)
END_ONLINE_STRUCT_META()

/* Meta*/ }

/* UE::Online */ }
