// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IRivermaxManager.h"

namespace UE::RivermaxCore::Private
{
	class FRivermaxDeviceFinder;
	
	class FRivermaxManager : public UE::RivermaxCore::IRivermaxManager
	{
	public:
		FRivermaxManager();
		~FRivermaxManager();

	public:
		//~ Begin IRivermaxManager interface
		virtual bool IsManagerInitialized() const override;
		virtual bool IsLibraryInitialized() const override;
		virtual FOnPostRivermaxManagerInit& OnPostRivermaxManagerInit() override;
		virtual uint64 GetTime() const override;
		virtual ERivermaxTimeSource GetTimeSource() const override;
		virtual TConstArrayView<FRivermaxDeviceInfo> GetDevices() const override;
		virtual bool GetMatchingDevice(const FString& InSourceIP, FString& OutDeviceIP) const override;
		virtual bool IsValidIP(const FString& InSourceIP) const override;
		virtual bool IsGPUDirectSupported() const;
		//~ End IRivermaxManager interface

	private:
		bool LoadRivermaxLibrary();
		void InitializeLibrary();
		void VerifyGPUDirectCapability();
		bool InitializeClock(ERivermaxTimeSource DesiredTimeSource);

	private:

		/** True when manager has been initialized */
		bool bIsInitialized = false;

		/** True when library is usable */
		bool bIsLibraryInitialized = false;

		/** Whether library was initialized but failed along the way and needs to be cleaned up on exit */
		bool bIsCleanupRequired = false;

		/** Handle pointer to the loaded library */
		void* LibraryHandle = nullptr;

		/** Whether GPU direct is supported globally for rivermax. Currently means Cuda is present with RDMA support */
		bool bIsGPUDirectSupported = false;

		/** Current time source */
		ERivermaxTimeSource TimeSource = ERivermaxTimeSource::PTP;

		/** Rivermax device finder listing all usable interfaces */
		TUniquePtr<FRivermaxDeviceFinder> DeviceFinder;

		/** Delegate triggered when initialization has completed */
		FOnPostRivermaxManagerInit PostInitDelegate;
	};
}


