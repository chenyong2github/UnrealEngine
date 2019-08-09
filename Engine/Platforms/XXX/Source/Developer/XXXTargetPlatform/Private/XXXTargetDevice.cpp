// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "XXXTargetDevice.h"
#include "XXXTargetDeviceOutput.h"
#include "SocketSubsystem.h"
#include "Sockets.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY_STATIC(LogXXXTargetDevice, Log, All);

/* FXXXTargetDevice structors
 *****************************************************************************/
FXXXTargetDevice::FXXXTargetDevice( const ITargetPlatform& InTargetPlatform, const FString& InName)
	: CachedHostName( InName )
	, TargetPlatform( InTargetPlatform )
{
}

/* ITargetDevice interface
 *****************************************************************************/

bool FXXXTargetDevice::Connect()
{
	return true;
}


bool FXXXTargetDevice::Deploy( const FString& SourceFolder, FString& OutAppId )
{
	checkNoEntry();

	return false;
}


void FXXXTargetDevice::Disconnect( )
{
}


int32 FXXXTargetDevice::GetProcessSnapshot( TArray<FTargetDeviceProcessInfo>& OutProcessInfos ) 
{
	return 0;
}


bool FXXXTargetDevice::Launch( const FString& AppId, EBuildConfigurations::Type BuildConfiguration, EBuildTargetType TargetType, const FString& Params, uint32* OutProcessId )
{
	checkNoEntry();
	return false;
}

bool FXXXTargetDevice::PowerOff( bool Force )
{
	return true;
}


bool FXXXTargetDevice::PowerOn( )
{
	return true;
}


bool FXXXTargetDevice::Reboot( bool bReconnect )
{
	return true;
}


bool FXXXTargetDevice::Run( const FString& ExecutablePath, const FString& Params, uint32* OutProcessId )
{
	checkNoEntry();

	return true;
}


bool FXXXTargetDevice::SupportsFeature( ETargetDeviceFeatures Feature ) const
{
	switch (Feature)
	{
	case ETargetDeviceFeatures::PowerOff:
		return true;

	case ETargetDeviceFeatures::PowerOn:
		return true;

	case ETargetDeviceFeatures::ProcessSnapshot:
		return true;

	case ETargetDeviceFeatures::Reboot:
		return true;
	}

	return false;
}


bool FXXXTargetDevice::SupportsSdkVersion( const FString& VersionString ) const
{
	return true;
}


bool FXXXTargetDevice::TerminateProcess( const int64 ProcessId )
{
	return true;
}

void FXXXTargetDevice::ExecuteConsoleCommand( const FString& ExecCommand ) const
{
}


ITargetDeviceOutputPtr FXXXTargetDevice::CreateDeviceOutputRouter(FOutputDevice* Output) const
{
	FXXXTargetDeviceOutputPtr DeviceOutputPtr = MakeShareable(new FXXXTargetDeviceOutput());
	if (DeviceOutputPtr->Init(CachedHostName,Output))
	{
		return DeviceOutputPtr;
	}

	return nullptr;
}


