// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_EOS_SDK

#include "Delegates/Delegate.h"
#include "Features/IModularFeature.h"
#include "Templates/Function.h"

#if defined(EOS_PLATFORM_BASE_FILE_NAME)
#include EOS_PLATFORM_BASE_FILE_NAME
#endif
#include "eos_types.h"

class IEOSSDKManager : public IModularFeature
{
public:
	virtual EOS_EResult Initialize() = 0;
	virtual bool IsInitialized() const = 0;

	virtual EOS_HPlatform CreatePlatform(const EOS_Platform_Options& PlatformOptions) = 0;
	virtual void ReleasePlatform(EOS_HPlatform PlatformHandle) = 0;

	virtual bool Tick(float DeltaTime = 0.f) = 0;

	virtual FString GetProductName() const = 0;
	virtual FString GetProductVersion() const = 0;
};

#endif // WITH_EOS_SDK