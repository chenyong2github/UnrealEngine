// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/UnrealString.h"
#include "Engine/DeveloperSettings.h"

#include "DMXEditorSettings.generated.h"

/** Struct to describe a single fader, so it can be stored in the config */
USTRUCT()
struct FDMXOutputConsoleFaderDescriptor
{
	GENERATED_BODY()

	FDMXOutputConsoleFaderDescriptor()
		: Value(0)
		, MaxValue(255)
		, MinValue(0)
		, UniversID(1)
		, StartingAddress(1)
		, EndingAddress(1)
	{}

	UPROPERTY()
	FString FaderName;

	UPROPERTY()
	uint8 Value;

	UPROPERTY()
	uint8 MaxValue;

	UPROPERTY()
	uint8 MinValue;
	
	UPROPERTY()
	int32 UniversID;

	UPROPERTY()
	int32 StartingAddress;

	UPROPERTY()
	int32 EndingAddress;

	UPROPERTY()
	FName ProtocolName;
};

/**
 * Struct to describe a monitor source, so it can be stored in settings 
 * Defaults to Monitor all Inputs.
 */
USTRUCT()
struct FDMXMonitorSourceDescriptor
{
	GENERATED_BODY()

	FDMXMonitorSourceDescriptor()
		: bMonitorAllPorts(true)
		, bMonitorInputPorts(true)
		, MonitoredPortGuid(FGuid())
	{}

	/** True if all ports should be monitored */
	UPROPERTY()
	bool bMonitorAllPorts;

	/** True if Input Ports should be monitored. Only relevant if bMonitorAllPorts */
	UPROPERTY()
	bool bMonitorInputPorts;

	/** The monitored Port Guid. Only relevant if !bMonitorAllPorts*/
	UPROPERTY()
	FGuid MonitoredPortGuid;
};

/** Settings that holds editor configurations. Not accessible in Project Settings. TODO: Idealy rename to UDMXEditorConfiguration */
UCLASS(Config = DMXEditor, DefaultConfig, meta = (DisplayName = "DMXEditor"))
class UDMXEditorSettings : public UObject
{
	GENERATED_BODY()

public:
	/** Stores the faders specified in Output Console */
	UPROPERTY(Config)
	TArray<FDMXOutputConsoleFaderDescriptor> OutputConsoleFaders;
	
public:
	/** The Universe ID to be monitored in the Channels Monitor  */
	UPROPERTY(Config)
	int32 ChannelsMonitorUniverseID = 1;

	/** Source for the channels monitor */
	UPROPERTY(Config)
	FDMXMonitorSourceDescriptor ChannelsMonitorSource;

public:
	/** Source for the DMX Activity Monitor */
	UPROPERTY(Config)
	FDMXMonitorSourceDescriptor ActivityMonitorSource;

	/** ID of the first universe to monitor in the DMX Activity Monitor  */
	UPROPERTY(Config)
	int32 ActivityMonitorMinUniverseID = 1;

	/** ID of the last universe to monitor in the DMX Activity Monitor */
	UPROPERTY(Config)
	int32 ActivityMonitorMaxUniverseID = 100;
};
