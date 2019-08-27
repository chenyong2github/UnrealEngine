// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Interfaces/ITargetDevice.h"
#include "Interfaces/ITargetPlatform.h"


/**
 * Type definition for shared pointers to instances of FXXXTargetDevice.
 */
typedef TSharedPtr<class FXXXTargetDevice, ESPMode::ThreadSafe> FXXXTargetDevicePtr;

/**
 * Type definition for shared references to instances of FXXXTargetDevice.
 */
typedef TSharedRef<class FXXXTargetDevice, ESPMode::ThreadSafe> FXXXTargetDeviceRef;

/**
 * Type definition for shared references to instances of FXXXTargetDeviceOutput.
 */
typedef TSharedPtr<class FXXXTargetDeviceOutput, ESPMode::ThreadSafe> FXXXTargetDeviceOutputPtr;


/**
 * Implements a XXX target device.
 */
class FXXXTargetDevice : public ITargetDevice
{
public:

	/**
	 * Creates and initializes a new XXX target device.
	 *
	 * @param InTargetPlatform - The target platform.
	 * @param InName - The device name.
	 */
	FXXXTargetDevice( const ITargetPlatform& InTargetPlatform, const FString& InName );

	/**
	 * Destructor.
	 */
	~FXXXTargetDevice( )
	{
	}

public:

	//~ Begin ITargetDevice Interface

	virtual bool Connect( ) override;

	virtual bool Deploy( const FString& SourceFolder, FString& OutAppId ) override;

	virtual void Disconnect( ) override;

	virtual ETargetDeviceTypes GetDeviceType( ) const override
	{
		return ETargetDeviceTypes::Console;
	}

	virtual FTargetDeviceId GetId( ) const override
	{
		return CachedId;
	}

	virtual FString GetName( ) const override
	{
		return CachedName;
	}

	virtual FString GetOperatingSystemName( ) override
	{
		return CachedOSName;
	}

	virtual int32 GetProcessSnapshot( TArray<FTargetDeviceProcessInfo>& OutProcessInfos ) override;

	virtual const class ITargetPlatform& GetTargetPlatform( ) const override
	{
		return TargetPlatform;
	}

	virtual bool GetUserCredentials( FString& OutUserName, FString& OutUserPassword ) override
	{
		return false;
	}

	virtual bool IsConnected( )
	{
		return true;
	}

	virtual bool IsDefault( ) const override
	{
		return CachedDefault;
	}

	virtual bool Launch( const FString& AppId, EBuildConfiguration BuildConfiguration, EBuildTargetType TargetType, const FString& Params, uint32* OutProcessId ) override;

	virtual bool PowerOff( bool Force ) override;

	virtual bool PowerOn( ) override;

	virtual bool Reboot( bool bReconnect = false ) override;

	virtual bool Run( const FString& ExecutablePath, const FString& Params, uint32* OutProcessId ) override;

	virtual void SetUserCredentials( const FString& UserName, const FString& UserPassword ) override { }

	virtual bool SupportsFeature( ETargetDeviceFeatures Feature ) const override;

	virtual bool SupportsSdkVersion( const FString& VersionString ) const override;

	virtual bool TerminateProcess( const int64 ProcessId ) override;

	virtual void ExecuteConsoleCommand(const FString& ExecCommand) const override;

	virtual ITargetDeviceOutputPtr CreateDeviceOutputRouter(FOutputDevice* Output) const override;


	//~ End ITargetDevice Interface


private:

	// Cached default flag.
	bool CachedDefault;

	// cache the Host Name.
	FString CachedHostName;

	// Cached device identifier.
	FTargetDeviceId CachedId;

	// Cached host name.
	FString CachedName;

	// Cached operating system name.
	FString CachedOSName;

	// Holds a reference to the device's target platform.
	const ITargetPlatform& TargetPlatform;
};
