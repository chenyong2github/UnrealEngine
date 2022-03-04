// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_ENGINE
#include "SocketSubsystemEOSUtils_OnlineServicesEOS.h"

#include "Online/OnlineServicesEOS.h"
#include "Online/OnlineServicesEOSTypes.h"
#include "Online/AuthEOS.h"
#include "Online/LobbiesEOS.h"
#include "Online/OnlineIdEOS.h"

namespace UE::Online {

FSocketSubsystemEOSUtils_OnlineServicesEOS::FSocketSubsystemEOSUtils_OnlineServicesEOS(FOnlineServicesEOS& InServicesEOS)
	: ServicesEOS(InServicesEOS)
{
}

FSocketSubsystemEOSUtils_OnlineServicesEOS::~FSocketSubsystemEOSUtils_OnlineServicesEOS()
{
}

EOS_ProductUserId FSocketSubsystemEOSUtils_OnlineServicesEOS::GetLocalUserId()
{
	EOS_ProductUserId Result = nullptr;

	IAuthPtr AuthEOS = ServicesEOS.GetAuthInterface();
	check(AuthEOS);

	FAuthGetAccountByPlatformUserId::Params AuthParams;
	AuthParams.PlatformUserId = FPlatformMisc::GetPlatformUserForUserIndex(0);
	TOnlineResult<FAuthGetAccountByPlatformUserId> AuthResult = AuthEOS->GetAccountByPlatformUserId(MoveTemp(AuthParams));
	if (AuthResult.IsOk())
	{
		UE::Online::FAuthGetAccountByPlatformUserId::Result OkValue = AuthResult.GetOkValue();

		Result = GetProductUserIdChecked(OkValue.AccountInfo->UserId);
	}
	else
	{
		UE_LOG(LogOnlineServices, Verbose, TEXT("[FSocketSubsystemEOSUtils_OnlineServicesEOS::GetLocalUserId] Unable to get account for platform user id [%s]. Error=[%s]"), *ToLogString(AuthParams.PlatformUserId), *AuthResult.GetErrorValue().GetLogString(true));
	}

	return Result;
}

FString FSocketSubsystemEOSUtils_OnlineServicesEOS::GetSessionId()
{
	FString Result;

	IAuthPtr AuthEOS = ServicesEOS.GetAuthInterface();
	check(AuthEOS);

	FAuthGetAccountByPlatformUserId::Params AuthParams;
	AuthParams.PlatformUserId = FPlatformMisc::GetPlatformUserForUserIndex(0);
	TOnlineResult<FAuthGetAccountByPlatformUserId> AuthResult = AuthEOS->GetAccountByPlatformUserId(MoveTemp(AuthParams));
	if (AuthResult.IsOk())
	{
		FAuthGetAccountByPlatformUserId::Result* AuthOkValue = AuthResult.TryGetOkValue();

		ILobbiesPtr LobbiesEOS = ServicesEOS.GetLobbiesInterface();
		check(LobbiesEOS);

		FGetJoinedLobbies::Params LobbiesParams;
		LobbiesParams.LocalUserId = AuthOkValue->AccountInfo->UserId;
		TOnlineResult<FGetJoinedLobbies> LobbiesResult = LobbiesEOS->GetJoinedLobbies(MoveTemp(LobbiesParams));
		if (LobbiesResult.IsOk())
		{
			FGetJoinedLobbies::Result* LobbiesOkValue = LobbiesResult.TryGetOkValue();

			// TODO: Pending support in Lobbies interface
			/*for (TSharedRef<FLobby> Lobby : LobbiesOkValue->Lobbies)
			{
					if (Lobby->SessionName == NAME_GameSession)
					Result = LobbiesEOS->GetLobbyIdString(Lobby);
			}*/
		}
		else
		{
			UE_LOG(LogOnlineServices, Verbose, TEXT("[FSocketSubsystemEOSUtils_OnlineServicesEOS::GetSessionId] Unable to get joined lobbies for local user id [%s]. Error=[%s]"), *ToLogString(LobbiesParams.LocalUserId), *AuthResult.GetErrorValue().GetLogString(true));
		}
	}
	else
	{
		UE_LOG(LogOnlineServices, Verbose, TEXT("[FSocketSubsystemEOSUtils_OnlineServicesEOS::GetSessionId] Unable to get account for platform user id [%s]. Error=[%s]"), *ToLogString(AuthParams.PlatformUserId), *AuthResult.GetErrorValue().GetLogString(true));
	}

	return Result;
}

FName FSocketSubsystemEOSUtils_OnlineServicesEOS::GetSubsystemInstanceName()
{
	// TODO: OnlineServices still doesn't have functionality that matches GetInstanceName
	return FName();
}

/* UE::Online */}

#endif // WITH_ENGINE
