// Copyright Epic Games, Inc. All Rights Reserved.

#include "Online/AuthCommon.h"

#include "Online/OnlineAsyncOp.h"
#include "Online/OnlineServicesCommon.h"

namespace UE::Online {

FAuthCommon::FAuthCommon(FOnlineServicesCommon& InServices)
	: TOnlineComponent(TEXT("Auth"), InServices.AsShared())
	, Services(InServices)
{
}

TOnlineAsyncOpHandle<FAuthLogin> FAuthCommon::Login(FAuthLogin::Params&& Params)
{
	TOnlineAsyncOp<FAuthLogin>& Operation = Services.OpCache.GetOp<FAuthLogin>(MoveTemp(Params));
	Operation.SetError(Errors::Unimplemented());
	return Operation.GetHandle();
}

TOnlineAsyncOpHandle<FAuthLogout> FAuthCommon::Logout(FAuthLogout::Params&& Params)
{
	TOnlineAsyncOp<FAuthLogout>& Operation = Services.OpCache.GetOp<FAuthLogout>(MoveTemp(Params));
	Operation.SetError(Errors::Unimplemented());
	return Operation.GetHandle();
}

TOnlineAsyncOpHandle<FAuthGenerateAuth> FAuthCommon::GenerateAuth(FAuthGenerateAuth::Params&& Params)
{
	TOnlineAsyncOp<FAuthGenerateAuth>& Operation = Services.OpCache.GetOp<FAuthGenerateAuth>(MoveTemp(Params));
	Operation.SetError(Errors::Unimplemented());
	return Operation.GetHandle();
}

TOnlineEvent<void(const FLoginStatusChanged&)> FAuthCommon::OnLoginStatusChanged()
{
	return OnLoginStatusChangedEvent;
}

/* UE::Online */ }
