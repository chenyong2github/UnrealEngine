// Copyright Epic Games, Inc. All Rights Reserved.

#include "IOSRuntimeSettings.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Misc/EngineBuildSettings.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformApplicationMisc.h"

UIOSRuntimeSettings::UIOSRuntimeSettings(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bEnableGameCenterSupport = true;
	bEnableCloudKitSupport = false;
    bRunAsCurrentUser = false;
	bSupportsPortraitOrientation = true;
	bSupportsITunesFileSharing = false;
	bSupportsFilesApp = false;
	BundleDisplayName = TEXT("UE4 Game");
	BundleName = TEXT("MyUE4Game");
	BundleIdentifier = TEXT("com.YourCompany.GameNameNoSpaces");
	VersionInfo = TEXT("1.0.0");
    FrameRateLock = EPowerUsageFrameRateLock::PUFRL_30;
	bEnableDynamicMaxFPS = false;
	bSupportsIPad = true;
	bSupportsIPhone = true;
	MinimumiOSVersion = EIOSVersion::IOS_12;
    bBuildAsFramework = true;
	bGeneratedSYMFile = false;
	bGeneratedSYMBundle = false;
	bGenerateXCArchive = false;
	bShipForBitcode = true;
	bUseRSync = true;
	bCustomLaunchscreenStoryboard = false;
	AdditionalPlistData = TEXT("");
	AdditionalLinkerFlags = TEXT("");
	AdditionalShippingLinkerFlags = TEXT("");
    bGameSupportsMultipleActiveControllers = false;
	bAllowRemoteRotation = true;
    bUseRemoteAsVirtualJoystick_DEPRECATED = true;
	bUseRemoteAbsoluteDpadValues = false;
	bDisableMotionData = false;
    bEnableRemoteNotificationsSupport = false;
    bEnableBackgroundFetch = false;
	bSupportsMetal = true;
	bSupportsMetalMRT = false;
	bDisableHTTPS = false;
}

void UIOSRuntimeSettings::PostReloadConfig(class FProperty* PropertyThatWasLoaded)
{
	Super::PostReloadConfig(PropertyThatWasLoaded);

#if PLATFORM_IOS

	FPlatformApplicationMisc::SetGamepadsAllowed(bAllowControllers);

#endif //PLATFORM_IOS
}

#if WITH_EDITOR
void UIOSRuntimeSettings::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	// Ensure that at least one orientation is supported
	if (!bSupportsPortraitOrientation && !bSupportsUpsideDownOrientation && !bSupportsLandscapeLeftOrientation && !bSupportsLandscapeRightOrientation)
	{
		bSupportsPortraitOrientation = true;
		UpdateSinglePropertyInConfigFile(GetClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UIOSRuntimeSettings, bSupportsPortraitOrientation)), GetDefaultConfigFilename());
	}

	// Ensure that at least one API is supported
	if (!bSupportsMetal && !bSupportsMetalMRT)
	{
		bSupportsMetal = true;
		UpdateSinglePropertyInConfigFile(GetClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UIOSRuntimeSettings, bSupportsMetal)), GetDefaultConfigFilename());
	}

}


void UIOSRuntimeSettings::PostInitProperties()
{
	Super::PostInitProperties();

	// We can have a look for potential keys
	if (!RemoteServerName.IsEmpty() && !RSyncUsername.IsEmpty())
	{
		SSHPrivateKeyLocation = TEXT("");

		const FString DefaultKeyFilename = TEXT("RemoteToolChainPrivate.key");
		const FString RelativeFilePathLocation = FPaths::Combine(TEXT("SSHKeys"), *RemoteServerName, *RSyncUsername, *DefaultKeyFilename);

		FString Path = FPlatformMisc::GetEnvironmentVariable(TEXT("APPDATA"));

		TArray<FString> PossibleKeyLocations;
		PossibleKeyLocations.Add(FPaths::Combine(*FPaths::ProjectDir(), TEXT("Restricted"), TEXT("NotForLicensees"), TEXT("Build"), *RelativeFilePathLocation));
		PossibleKeyLocations.Add(FPaths::Combine(*FPaths::ProjectDir(), TEXT("Restricted"), TEXT("NoRedist"), TEXT("Build"), *RelativeFilePathLocation));
		PossibleKeyLocations.Add(FPaths::Combine(*FPaths::ProjectDir(), TEXT("Build"), *RelativeFilePathLocation));
		PossibleKeyLocations.Add(FPaths::Combine(*FPaths::EngineDir(), TEXT("Restricted"), TEXT("NotForLicensees"), TEXT("Build"), TEXT("NotForLicensees"), *RelativeFilePathLocation));
		PossibleKeyLocations.Add(FPaths::Combine(*FPaths::EngineDir(), TEXT("Restricted"), TEXT("NoRedist"), TEXT("Build"), *RelativeFilePathLocation));
		PossibleKeyLocations.Add(FPaths::Combine(*FPaths::EngineDir(), TEXT("Build"), *RelativeFilePathLocation));
		PossibleKeyLocations.Add(FPaths::Combine(*Path, TEXT("Unreal Engine"), TEXT("UnrealBuildTool"), *RelativeFilePathLocation));

		// Find a potential path that we will use if the user hasn't overridden.
		// For information purposes only
		for (const FString& NextLocation : PossibleKeyLocations)
		{
			if (IFileManager::Get().FileSize(*NextLocation) > 0)
			{
				SSHPrivateKeyLocation = NextLocation;
				break;
			}
		}
	}

	if (MinimumiOSVersion < EIOSVersion::IOS_12)
	{
		MinimumiOSVersion = EIOSVersion::IOS_12;
		UpdateSinglePropertyInConfigFile(GetClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UIOSRuntimeSettings, MinimumiOSVersion)), GetDefaultConfigFilename());
	}
	if (!bSupportsMetal && !bSupportsMetalMRT)
	{
		bSupportsMetal = true;
		UpdateSinglePropertyInConfigFile(GetClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UIOSRuntimeSettings, bSupportsMetal)), GetDefaultConfigFilename());
	}
}
#endif
