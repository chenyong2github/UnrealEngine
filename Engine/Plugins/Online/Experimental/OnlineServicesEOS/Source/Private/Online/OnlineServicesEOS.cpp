// Copyright Epic Games, Inc. All Rights Reserved.

#include "Online/OnlineServicesEOS.h"
#include "Online/OnlineServicesEOSTypes.h"

#include "EOSShared.h"
#include "EOSSharedTypes.h"
#include "IEOSSDKManager.h"

#include "Online/AuthEOS.h"
#include "Online/FriendsEOS.h"
#include "Online/LobbiesEOS.h"
#include "Online/OnlineIdEOS.h"
#include "Online/PresenceEOS.h"
#include "Online/ExternalUIEOS.h"
#include "InternetAddrEOS.h"
#include "Engine/EngineBaseTypes.h"
#include "NetDriverEOS.h"

namespace UE::Online {

FSocketSubsystemEOSUtils_OnlineServicesEOS::FSocketSubsystemEOSUtils_OnlineServicesEOS(FOnlineServicesEOS* InServicesEOS)
	: ServicesEOS(InServicesEOS)
{
	check(ServicesEOS);
}

FSocketSubsystemEOSUtils_OnlineServicesEOS::~FSocketSubsystemEOSUtils_OnlineServicesEOS()
{
	ServicesEOS = nullptr;
}

EOS_ProductUserId FSocketSubsystemEOSUtils_OnlineServicesEOS::GetLocalUserId()
{
	EOS_ProductUserId Result = nullptr;

	if (ServicesEOS)
	{
		using namespace UE::Online;

		IAuthPtr AuthEOS = ServicesEOS->GetAuthInterface();
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
	}

	return Result;
}

FString FSocketSubsystemEOSUtils_OnlineServicesEOS::GetSessionId()
{
	FString Result;

	if (ServicesEOS)
	{
		using namespace UE::Online;

		IAuthPtr AuthEOS = ServicesEOS->GetAuthInterface();
		check(AuthEOS);

		FAuthGetAccountByPlatformUserId::Params AuthParams;
		AuthParams.PlatformUserId = FPlatformMisc::GetPlatformUserForUserIndex(0);
		TOnlineResult<FAuthGetAccountByPlatformUserId> AuthResult = AuthEOS->GetAccountByPlatformUserId(MoveTemp(AuthParams));
		if (AuthResult.IsOk())
		{
			FAuthGetAccountByPlatformUserId::Result* AuthOkValue = AuthResult.TryGetOkValue();

			ILobbiesPtr LobbiesEOS = ServicesEOS->GetLobbiesInterface();
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
	}

	return Result;
}

struct FEOSPlatformConfig
{
	FString ProductId;
	FString SandboxId;
	FString DeploymentId;
	FString ClientId;
	FString ClientSecret;
};

namespace Meta {

BEGIN_ONLINE_STRUCT_META(FEOSPlatformConfig)
	ONLINE_STRUCT_FIELD(FEOSPlatformConfig, ProductId),
	ONLINE_STRUCT_FIELD(FEOSPlatformConfig, SandboxId),
	ONLINE_STRUCT_FIELD(FEOSPlatformConfig, DeploymentId),
	ONLINE_STRUCT_FIELD(FEOSPlatformConfig, ClientId),
	ONLINE_STRUCT_FIELD(FEOSPlatformConfig, ClientSecret)
END_ONLINE_STRUCT_META()

/* Meta */ }

FOnlineServicesEOS::FOnlineServicesEOS()
	: FOnlineServicesCommon(TEXT("EOS"))
{
}

void FOnlineServicesEOS::RegisterComponents()
{
	Components.Register<FAuthEOS>(*this);
	Components.Register<FFriendsEOS>(*this);
	Components.Register<FLobbiesEOS>(*this);
	Components.Register<FPresenceEOS>(*this);
	Components.Register<FExternalUIEOS>(*this);
	FOnlineServicesCommon::RegisterComponents();
}

void FOnlineServicesEOS::Initialize()
{
	FEOSPlatformConfig EOSPlatformConfig;
	LoadConfig(EOSPlatformConfig);

	FTCHARToUTF8 ProductId(*EOSPlatformConfig.ProductId);
	FTCHARToUTF8 SandboxId(*EOSPlatformConfig.SandboxId);
	FTCHARToUTF8 DeploymentId(*EOSPlatformConfig.DeploymentId);
	FTCHARToUTF8 ClientId(*EOSPlatformConfig.ClientId);
	FTCHARToUTF8 ClientSecret(*EOSPlatformConfig.ClientSecret);

	EOS_Platform_Options PlatformOptions = {};
	PlatformOptions.ApiVersion = EOS_PLATFORM_OPTIONS_API_LATEST;
	PlatformOptions.Reserved = nullptr;
	PlatformOptions.bIsServer = EOS_FALSE;
	PlatformOptions.OverrideCountryCode = nullptr;
	PlatformOptions.OverrideLocaleCode = nullptr;
	PlatformOptions.Flags = EOS_PF_WINDOWS_ENABLE_OVERLAY_D3D9 | EOS_PF_WINDOWS_ENABLE_OVERLAY_D3D10 | EOS_PF_WINDOWS_ENABLE_OVERLAY_OPENGL; // Enable overlay support for D3D9/10 and OpenGL. This sample uses D3D11 or SDL.

	char CacheDirectory[512];
	FCStringAnsi::Strncpy(CacheDirectory, TCHAR_TO_UTF8(*(FPlatformProcess::UserDir() / FString(TEXT("CacheDir")))), 512);
	PlatformOptions.CacheDirectory = CacheDirectory;

	PlatformOptions.ProductId = ProductId.Get();
	PlatformOptions.SandboxId = SandboxId.Get();
	PlatformOptions.DeploymentId = DeploymentId.Get();

	PlatformOptions.ClientCredentials.ClientId = ClientId.Get();
	PlatformOptions.ClientCredentials.ClientSecret = ClientSecret.Get();

	IEOSSDKManager* SDKManager = IEOSSDKManager::Get();
	if (!SDKManager)
	{
		return;
	}

	EOS_EResult InitResult = SDKManager->Initialize();
	if (InitResult != EOS_EResult::EOS_Success)
	{
		return;
	}

	EOSPlatformHandle = SDKManager->CreatePlatform(PlatformOptions);
	if (EOSPlatformHandle)
	{
		SocketSubsystem = MakeShareable(new FSocketSubsystemEOS(EOSPlatformHandle, MakeShareable(new FSocketSubsystemEOSUtils_OnlineServicesEOS(this))));
		if (SocketSubsystem)
		{
			FString ErrorStr;
			if (!SocketSubsystem->Init(ErrorStr))
			{
				UE_LOG(LogOnlineServices, Verbose, TEXT("[FOnlineServicesEOS::Initialize] Unable to initialize Socket Subsystem. Error=[%s]"), *ErrorStr);
			}
		}
	}
	else
	{
		UE_LOG(LogOnlineServices, Verbose, TEXT("[FOnlineServicesEOS::Initialize] Unable to initialize Socket Subsystem. EOS Platform Handle was invalid."));
	}

	FOnlineServicesCommon::Initialize();
}

TOnlineResult<FGetResolvedConnectString> FOnlineServicesEOS::GetResolvedConnectString(FGetResolvedConnectString::Params&& Params)
{
	ILobbiesPtr LobbiesEOS = GetLobbiesInterface();
	check(LobbiesEOS);

	TOnlineResult<FGetJoinedLobbies> JoinedLobbies = LobbiesEOS->GetJoinedLobbies({ Params.LocalUserId });
	if (JoinedLobbies.IsOk())
	{
		for (TSharedRef<const FLobby>& Lobby : JoinedLobbies.GetOkValue().Lobbies)
		{
			if (Lobby->LobbyId == Params.LobbyId)
			{
				//It should look like this: "EOS:0002aeeb5b2d4388a3752dd6d31222ec:GameNetDriver:97"
				FString NetDriverName = GetDefault<UNetDriverEOS>()->NetDriverName.ToString();
				FInternetAddrEOS TempAddr(GetProductUserIdChecked(Lobby->OwnerAccountId), NetDriverName, GetTypeHash(NetDriverName));
				return TOnlineResult<FGetResolvedConnectString>({ TempAddr.ToString(true) });
			}
		}
		// No matching lobby
		return TOnlineResult<FGetResolvedConnectString>(Errors::InvalidParams());
	}
	else
	{
		return TOnlineResult<FGetResolvedConnectString>(JoinedLobbies.GetErrorValue());
	}
}

EOS_HPlatform FOnlineServicesEOS::GetEOSPlatformHandle() const
{
	if (EOSPlatformHandle)
	{
		return *EOSPlatformHandle;
	}
	return nullptr;
}


/* UE::Online */ }
