// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Online/OnlineAsyncOpHandle.h"
#include "Online/OnlineId.h"
#include "Online/OnlineMeta.h"

namespace UE::Online {

class FOnlineError;

class FAccountInfo
{
public:
	int32 LocalUserNum;
	FAccountId UserId;
	// TODO: Other fields
};

struct FAuthLogin
{
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
	struct Params
	{
		FAccountId LocalUserId;
	};

	struct Result
	{
		TSharedRef<FAccountInfo> AccountInfo;
	};
};

struct FLoginStatusChanged
{
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
	virtual TOnlineResult<FAuthGetAccountByLocalUserNum::Result> GetAccountByLocalUserNum(FAuthGetAccountByLocalUserNum::Params&& Params) = 0;

	/**
	 * Get logged in user by account id
	 */
	virtual TOnlineResult<FAuthGetAccountByAccountId::Result> GetAccountByAccountId(FAuthGetAccountByAccountId::Params&& Params) = 0;

	/**
	 * Event triggered when a local user's login status changes
	 */
	virtual TOnlineEvent<void(const FLoginStatusChanged&)> OnLoginStatusChanged() = 0;
};


namespace Meta {
// TODO: Move to Auth_Meta.inl file?

BEGIN_ONLINE_STRUCT_META(FAccountInfo)
	ONLINE_STRUCT_FIELD(FAccountInfo, LocalUserNum),
	ONLINE_STRUCT_FIELD(FAccountInfo, UserId)
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

BEGIN_ONLINE_STRUCT_META(FLoginStatusChanged)
END_ONLINE_STRUCT_META()

/* Meta*/ }

/* UE::Online */ }
