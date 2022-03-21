// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Common/TargetPlatformBase.h"
#include "Misc/ConfigCacheIni.h"
#include "Serialization/MemoryLayout.h"

// Extend from either WIndows or Linux device, since there will be a lot of shared functionality
template<class ParentDeviceClass>
class TSteamDeckDevice : public ParentDeviceClass
{
public:
	TSteamDeckDevice(FString InIpAddr, FString InDeviceName, FString InUserName, const ITargetPlatform& InTargetPlatform, const TCHAR* InRuntimeOSName)
		: ParentDeviceClass(InTargetPlatform)
		, IpAddr(InIpAddr)
		, UserName(InUserName)
		, RuntimeOSName(InRuntimeOSName)
	{
		DeviceName = FString::Printf(TEXT("%s (%s)"), *InDeviceName, InRuntimeOSName);
	}

	virtual FString GetName() const override
	{
		return DeviceName;
	}

	virtual FTargetDeviceId GetId() const override
	{
		return FTargetDeviceId(this->TargetPlatform.PlatformName(), IpAddr);
	}

	virtual FString GetOperatingSystemName() override
	{
		return FString::Printf(TEXT("SteamOS (%s)"), *RuntimeOSName);
	}

	virtual bool GetUserCredentials(FString& OutUserName, FString& OutUserPassword) override
	{
		OutUserName = UserName;

		// No need for a password, as it will use an rsa key that is part of the SteamOS Devkit Client
		OutUserPassword = FString();

		return true;
	}

	static TArray<ITargetDevicePtr> DiscoverDevices(const ITargetPlatform& TargetPlatform, const TCHAR* RuntimeOSName)
	{
		TArray<FString> EngineIniSteamDeckDevices;

		// Expected ini format: +SteamDeckDevice=(IpAddr=10.1.33.19,Name=MySteamDeck,UserName=deck)
		GConfig->GetArray(TEXT("SteamDeck"), TEXT("SteamDeckDevice"), EngineIniSteamDeckDevices, GEngineIni);

		TArray<ITargetDevicePtr> SteamDevices;
		for (const FString& Device : EngineIniSteamDeckDevices)
		{
			FString IpAddr;
			FString Name;
			FString UserName;
			if (!FParse::Value(*Device, TEXT("IpAddr="), IpAddr))
			{
				continue;
			}
			// Name is not required, if not set use the IpAddr. This is what is displayed in the Editor
			if (!FParse::Value(*Device, TEXT("Name="), Name))
			{
				Name = IpAddr;
			}
			if (!FParse::Value(*Device, TEXT("UserName="), UserName))
			{
				continue;
			}

			SteamDevices.Add(MakeShareable(new TSteamDeckDevice<ParentDeviceClass>(IpAddr, Name, UserName, TargetPlatform, RuntimeOSName)));
		}

		return SteamDevices;
	}

private:
	FString IpAddr;
	FString DeviceName;
	FString UserName;
	FString RuntimeOSName;
};
