// Copyright Epic Games, Inc. All Rights Reserved.

#include "OnlineServicesEOS.h"

#if WITH_EOS_SDK

#include "IEOSSDKManager.h"
#include "AuthEOS.h"

namespace UE::Online {

FOnlineServicesEOS::FOnlineServicesEOS()
{
	// Init EOS SDK
	EOS_Platform_Options PlatformOptions = {};
	PlatformOptions.ApiVersion = EOS_PLATFORM_OPTIONS_API_LATEST;
	PlatformOptions.bIsServer = EOS_FALSE;
	PlatformOptions.EncryptionKey = nullptr;
	PlatformOptions.OverrideCountryCode = nullptr;
	PlatformOptions.OverrideLocaleCode = nullptr;
	PlatformOptions.Flags = EOS_PF_WINDOWS_ENABLE_OVERLAY_D3D9 | EOS_PF_WINDOWS_ENABLE_OVERLAY_D3D10 | EOS_PF_WINDOWS_ENABLE_OVERLAY_OPENGL; // Enable overlay support for D3D9/10 and OpenGL. This sample uses D3D11 or SDL.

	char CacheDirectory[512];
	FCStringAnsi::Strncpy(CacheDirectory, TCHAR_TO_UTF8(*(FPlatformProcess::UserDir() / FString(TEXT("CacheDir")))), 512);
	PlatformOptions.CacheDirectory = CacheDirectory;

	PlatformOptions.ProductId = "";
	PlatformOptions.SandboxId = "";
	PlatformOptions.DeploymentId = "";

	PlatformOptions.ClientCredentials.ClientId = "";
	PlatformOptions.ClientCredentials.ClientSecret = "";

	IEOSSDKManager* SDKManager = IEOSSDKManager::Get();
	if (ensure(SDKManager))
	{
		EOSPlatformHandle = SDKManager->CreatePlatform(PlatformOptions);
	}
}

void FOnlineServicesEOS::RegisterComponents()
{
#if WITH_EOS_SDK
	Components.Register<FAuthEOS>(*this);
#endif
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

#endif // WITH_EOS_SDK