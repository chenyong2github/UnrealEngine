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

#if WITH_ENGINE
#include "InternetAddrEOS.h"
#include "NetDriverEOSBase.h"
#include "SocketSubsystemEOSUtils_OnlineServicesEOS.h"
#endif

namespace UE::Online {

struct FEOSPlatformConfig
{
	FString ProductId;
	FString SandboxId;
	FString DeploymentId;
	FString ClientId;
	FString ClientSecret;
	bool bUseEAS = false;
};

namespace Meta {

BEGIN_ONLINE_STRUCT_META(FEOSPlatformConfig)
	ONLINE_STRUCT_FIELD(FEOSPlatformConfig, ProductId),
	ONLINE_STRUCT_FIELD(FEOSPlatformConfig, SandboxId),
	ONLINE_STRUCT_FIELD(FEOSPlatformConfig, DeploymentId),
	ONLINE_STRUCT_FIELD(FEOSPlatformConfig, ClientId),
	ONLINE_STRUCT_FIELD(FEOSPlatformConfig, ClientSecret),
	ONLINE_STRUCT_FIELD(FEOSPlatformConfig, bUseEAS)
END_ONLINE_STRUCT_META()

/* Meta */ }

FOnlineServicesEOS::FOnlineServicesEOS()
	: FOnlineServicesCommon(TEXT("EOS"))
{
}

void FOnlineServicesEOS::RegisterComponents()
{
	FEOSPlatformConfig EOSPlatformConfig;
	LoadConfig(EOSPlatformConfig);

	Components.Register<FAuthEOS>(*this, EOSPlatformConfig.bUseEAS);
	if (EOSPlatformConfig.bUseEAS)
	{
		Components.Register<FFriendsEOS>(*this);
		Components.Register<FPresenceEOS>(*this);
	}
	Components.Register<FLobbiesEOS>(*this);
	Components.Register<FExternalUIEOS>(*this);
	FOnlineServicesCommon::RegisterComponents();
}

void FOnlineServicesEOS::Initialize()
{
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

	FEOSPlatformConfig EOSPlatformConfig;
	LoadConfig(EOSPlatformConfig);

	const FTCHARToUTF8 ProductId(*EOSPlatformConfig.ProductId);
	const FTCHARToUTF8 SandboxId(*EOSPlatformConfig.SandboxId);
	const FTCHARToUTF8 DeploymentId(*EOSPlatformConfig.DeploymentId);
	const FTCHARToUTF8 ClientId(*EOSPlatformConfig.ClientId);
	const FTCHARToUTF8 ClientSecret(*EOSPlatformConfig.ClientSecret);
	const FTCHARToUTF8 CacheDirectory(*(SDKManager->GetCacheDirBase() / TEXT("OnlineServicesEOS")));

	EOS_Platform_Options PlatformOptions = {};
	PlatformOptions.ApiVersion = EOS_PLATFORM_OPTIONS_API_LATEST;
	PlatformOptions.Reserved = nullptr;
	PlatformOptions.bIsServer = EOS_FALSE;
	PlatformOptions.OverrideCountryCode = nullptr;
	PlatformOptions.OverrideLocaleCode = nullptr;
	PlatformOptions.Flags = EOS_PF_WINDOWS_ENABLE_OVERLAY_D3D9 | EOS_PF_WINDOWS_ENABLE_OVERLAY_D3D10 | EOS_PF_WINDOWS_ENABLE_OVERLAY_OPENGL; // Enable overlay support for D3D9/10 and OpenGL. This sample uses D3D11 or SDL.

	PlatformOptions.ProductId = ProductId.Get();
	PlatformOptions.SandboxId = SandboxId.Get();
	PlatformOptions.DeploymentId = DeploymentId.Get();
	PlatformOptions.ClientCredentials.ClientId = ClientId.Get();
	PlatformOptions.ClientCredentials.ClientSecret = ClientSecret.Get();
	PlatformOptions.CacheDirectory = CacheDirectory.Get();

	EOSPlatformHandle = SDKManager->CreatePlatform(PlatformOptions);
	if (EOSPlatformHandle)
	{
#if WITH_ENGINE
		SocketSubsystem = MakeShareable(new FSocketSubsystemEOS(EOSPlatformHandle, MakeShareable(new FSocketSubsystemEOSUtils_OnlineServicesEOS(*this))));
		check(SocketSubsystem);

		FString ErrorStr;
		if (!SocketSubsystem->Init(ErrorStr))
		{
			UE_LOG(LogOnlineServices, Warning, TEXT("[FOnlineServicesEOS::Initialize] Unable to initialize Socket Subsystem. Error=[%s]"), *ErrorStr);
		}
#endif
	}
	else
	{
		UE_LOG(LogOnlineServices, Verbose, TEXT("[FOnlineServicesEOS::Initialize] Unable to initialize Socket Subsystem. EOS Platform Handle was invalid."));
		return;
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
#if WITH_ENGINE
 				//It should look like this: "EOS:0002aeeb5b2d4388a3752dd6d31222ec:GameNetDriver:97"
 				FString NetDriverName = GetDefault<UNetDriverEOSBase>()->NetDriverName.ToString();
 				FInternetAddrEOS TempAddr(GetProductUserIdChecked(Lobby->OwnerAccountId), NetDriverName, GetTypeHash(NetDriverName));
 				return TOnlineResult<FGetResolvedConnectString>({ TempAddr.ToString(true) });
#else
				return TOnlineResult<FGetResolvedConnectString>(Errors::NotImplemented());
#endif
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
