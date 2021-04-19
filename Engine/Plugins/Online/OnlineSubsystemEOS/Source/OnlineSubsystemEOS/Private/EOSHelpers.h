// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_EOS_SDK

#include "eos_auth_types.h"
#include "Interfaces/OnlineExternalUIInterface.h"

class FOnlineSubsystemEOS;

class FEOSHelpers
{
public:
	virtual ~FEOSHelpers() = default;

	virtual FString PlatformCreateCacheDir(const FString &ArtifactName, const FString &EOSSettingsCacheDir);
	virtual void PlatformAuthCredentials(EOS_Auth_Credentials &Credentials);
	virtual void PlatformTriggerLoginUI(FOnlineSubsystemEOS* EOSSubsystem, const int ControllerIndex, bool bShowOnlineOnly, bool bShowSkipButton, const FOnLoginUIClosedDelegate& Delegate);

private:
	FOnlineSubsystemEOS* EOSSubsystem;
};

#endif

