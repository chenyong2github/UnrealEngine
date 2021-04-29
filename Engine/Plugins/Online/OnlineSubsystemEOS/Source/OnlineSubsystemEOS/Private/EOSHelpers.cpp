// Copyright Epic Games, Inc. All Rights Reserved.

#include "EOSHelpers.h"

#if WITH_EOS_SDK

#include "OnlineSubsystemEOS.h"
#include "UserManagerEOS.h"
#include "OnlineError.h"

FString FEOSHelpers::PlatformCreateCacheDir(const FString &ArtifactName, const FString &EOSSettingsCacheDir)
{
	FString CacheDir = FPlatformProcess::UserDir() / ArtifactName / EOSSettingsCacheDir;
	return MoveTemp(CacheDir);
}

void FEOSHelpers::PlatformAuthCredentials(EOS_Auth_Credentials &Credentials)
{
}

void FEOSHelpers::PlatformTriggerLoginUI(FOnlineSubsystemEOS* InEOSSubsystem, const int ControllerIndex, bool bShowOnlineOnly, bool bShowSkipButton, const FOnLoginUIClosedDelegate& Delegate)
{
	EOSSubsystem = InEOSSubsystem;
	check(EOSSubsystem != nullptr);

	UE_LOG_ONLINE_EXTERNALUI(Warning, TEXT("[FUserManagerEOS::ShowLoginUI] This method is not implemented."));

	EOSSubsystem->ExecuteNextTick([this, ControllerIndex, Delegate]()
	{
		Delegate.ExecuteIfBound(EOSSubsystem->UserManager->GetUniquePlayerId(ControllerIndex), ControllerIndex, FOnlineError(EOnlineErrorResult::NotImplemented));
	});
}

#endif

