// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_EOS_SDK

#include "Containers/UnrealString.h"

#if defined(EOS_PLATFORM_BASE_FILE_NAME)
#include EOS_PLATFORM_BASE_FILE_NAME
#endif
#include "eos_common.h"
#include "eos_init.h"

#include "IEOSSDKManager.h"

class FEOSSDKManager : public IEOSSDKManager
{
public:
	FEOSSDKManager();
	virtual ~FEOSSDKManager();

	// Begin IEOSSDKManager
	virtual EOS_EResult Initialize() override;
	virtual bool IsInitialized() const override { return bInitialized; }

	virtual EOS_HPlatform CreatePlatform(const EOS_Platform_Options& PlatformOptions) override;
	virtual void ReleasePlatform(EOS_HPlatform EosPlatformHandle) override;

	virtual bool Tick(float) override;

	virtual FString GetProductName() const override;
	virtual FString GetProductVersion() const override;
	// End IEOSSDKManager

	void Shutdown();

protected:
	virtual EOS_EResult EOSInitialize(EOS_InitializeOptions& Options);

	void UpdateConfiguration();

#if EOSSDK_RUNTIME_LOAD_REQUIRED
	void* SDKHandle = nullptr;
#endif

	struct
	{
		FString LogLevel;
	} Config;

	/** Are we currently initialized */
	bool bInitialized = false;
	/** Set of EOS platforms created with CreatePlatform */
	TSet<EOS_HPlatform> EosPlatformHandles;
	/** Handle to ticker delegate for Tick(), valid whenever EosPlatformHandles is non-empty. */
	FDelegateHandle TickerHandle;
};

#endif // WITH_EOS_SDK