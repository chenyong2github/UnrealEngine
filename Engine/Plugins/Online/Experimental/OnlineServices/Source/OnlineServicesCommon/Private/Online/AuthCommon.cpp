// Copyright Epic Games, Inc. All Rights Reserved.

#include "Online/AuthCommon.h"

#include "Online/OnlineAsyncOp.h"
#include "Online/OnlineErrorDefinitions.h"
#include "Online/OnlineServicesCommon.h"

namespace UE::Online {

FAuthCommon::FAuthCommon(FOnlineServicesCommon& InServices)
	: TOnlineComponent(TEXT("Auth"), InServices)
{
}

void FAuthCommon::RegisterCommands()
{
	RegisterCommand(&FAuthCommon::Login);
	RegisterCommand(&FAuthCommon::Logout);
	RegisterCommand(&FAuthCommon::GenerateAuthToken);
	RegisterCommand(&FAuthCommon::GetAuthToken);
	RegisterCommand(&FAuthCommon::GenerateAuthCode);
	RegisterCommand(&FAuthCommon::GetAccountByPlatformUserId);
	RegisterCommand(&FAuthCommon::GetAccountByAccountId);
}

TOnlineAsyncOpHandle<FAuthLogin> FAuthCommon::Login(FAuthLogin::Params&& Params)
{
	TOnlineAsyncOpRef<FAuthLogin> Operation = GetOp<FAuthLogin>(MoveTemp(Params));
	Operation->SetError(Errors::NotImplemented());  
	return Operation->GetHandle();
}

TOnlineAsyncOpHandle<FAuthLogout> FAuthCommon::Logout(FAuthLogout::Params&& Params)
{
	TOnlineAsyncOpRef<FAuthLogout> Operation = GetOp<FAuthLogout>(MoveTemp(Params));
	Operation->SetError(Errors::NotImplemented());
	return Operation->GetHandle();
}

TOnlineAsyncOpHandle<FAuthGenerateAuthToken> FAuthCommon::GenerateAuthToken(FAuthGenerateAuthToken::Params&& Params)
{
	TOnlineAsyncOpRef<FAuthGenerateAuthToken> Operation = GetOp<FAuthGenerateAuthToken>(MoveTemp(Params));
	Operation->SetError(Errors::NotImplemented());
	return Operation->GetHandle();
}

TOnlineResult<FAuthGetAuthToken> FAuthCommon::GetAuthToken(FAuthGetAuthToken::Params&& Params)
{
	return TOnlineResult<FAuthGetAuthToken>(Errors::NotImplemented());
}

TOnlineAsyncOpHandle<FAuthGenerateAuthCode> FAuthCommon::GenerateAuthCode(FAuthGenerateAuthCode::Params&& Params)
{
	TOnlineAsyncOpRef<FAuthGenerateAuthCode> Operation = GetOp<FAuthGenerateAuthCode>(MoveTemp(Params));
	Operation->SetError(Errors::NotImplemented());
	return Operation->GetHandle();
}

TOnlineResult<FAuthGetAccountByPlatformUserId> FAuthCommon::GetAccountByPlatformUserId(FAuthGetAccountByPlatformUserId::Params&& Params)
{
	return TOnlineResult<FAuthGetAccountByPlatformUserId>(Errors::NotImplemented());
}

TOnlineResult<FAuthGetAccountByAccountId> FAuthCommon::GetAccountByAccountId(FAuthGetAccountByAccountId::Params&& Params)
{
	return TOnlineResult<FAuthGetAccountByAccountId>(Errors::NotImplemented());
}

TOnlineEvent<void(const FLoginStatusChanged&)> FAuthCommon::OnLoginStatusChanged()
{
	return OnLoginStatusChangedEvent;
}

/* UE::Online */ }
