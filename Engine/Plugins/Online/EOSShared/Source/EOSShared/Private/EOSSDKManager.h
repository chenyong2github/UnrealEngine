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

struct FEOSPlatformHandle;

class FEOSSDKManager : public IEOSSDKManager
{
public:
	FEOSSDKManager();
	virtual ~FEOSSDKManager();

	// Begin IEOSSDKManager
	virtual EOS_EResult Initialize() override;
	virtual bool IsInitialized() const override { return bInitialized; }

	virtual IEOSPlatformHandlePtr CreatePlatform(const EOS_Platform_Options& PlatformOptions) override;

	virtual FString GetProductName() const override;
	virtual FString GetProductVersion() const override;
	// End IEOSSDKManager

	void Shutdown();

protected:
	virtual EOS_EResult EOSInitialize(EOS_InitializeOptions& Options);

private:
	friend struct FEOSPlatformHandle;

	void ReleasePlatform(EOS_HPlatform PlatformHandle);
	bool Tick(float);
	void OnLogVerbosityChanged(const FLogCategoryName& CategoryName, ELogVerbosity::Type OldVerbosity, ELogVerbosity::Type NewVerbosity);

#if EOSSDK_RUNTIME_LOAD_REQUIRED
	void* SDKHandle = nullptr;
#endif

	/** Are we currently initialized */
	bool bInitialized = false;
	/** Created platforms */
	TArray<EOS_HPlatform> PlatformHandles;
	/** Handle to ticker delegate for Tick(), valid whenever EosPlatformHandles is non-empty. */
	FDelegateHandle TickerHandle;
};

struct FEOSPlatformHandle : public IEOSPlatformHandle
{
	FEOSPlatformHandle(FEOSSDKManager& InManager, EOS_HPlatform InPlatformHandle)
		: IEOSPlatformHandle(InPlatformHandle), Manager(InManager)
	{
	}

	virtual ~FEOSPlatformHandle();
	virtual void Tick() override;

	/* Reference to the EOSSDK manager */
	FEOSSDKManager& Manager;
};

#endif // WITH_EOS_SDK