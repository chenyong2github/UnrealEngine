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
		virtual bool IsInitialized() const override;
		virtual uint64 GetTime() const override;
		virtual ERivermaxTimeSource GetTimeSource() const override;
		virtual TConstArrayView<FRivermaxDeviceInfo> GetDevices() const override;
		virtual bool GetMatchingDevice(const FString& InSourceIP, FString& OutDeviceIP) const override;
		virtual bool IsValidIP(const FString& InSourceIP) const override;
		//~ End IRivermaxManager interface

	private:
		bool LoadRivermaxLibrary();
		void InitializeLibrary();
		void FindDevices();

	private:

		/** Whether the library has been initialized and is usable */
		bool bIsInitialized = false;
		void* LibraryHandle = nullptr;

		/** Current time source */
		ERivermaxTimeSource TimeSource = ERivermaxTimeSource::PTP;

		TUniquePtr<FRivermaxDeviceFinder> DeviceFinder;
	};
}


