// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/UnrealString.h"
#include "Engine/DeveloperSettings.h"

#include "DMXEditorSettings.generated.h"


USTRUCT()
struct FDMXOutputConsoleFaderDescriptor
{
	GENERATED_BODY()

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

UCLASS(Config = DMXEditor, DefaultConfig, meta = (DisplayName = "DMXEditor"))
class UDMXEditorSettings : public UObject
{
	GENERATED_BODY()

public:
	/** Stores the faders specified in Output Console */
	UPROPERTY(Config)
	TArray<FDMXOutputConsoleFaderDescriptor> OutputConsoleFaders;
	
public:
	/** Protocol the Channels Monitor uses */
	UPROPERTY(Config)
	FName ChannelsMonitorProtocol;

	/** The Universe ID to be monitored in the Channels Monitor  */
	UPROPERTY(Config)
	uint16 ChannelsMonitorUniverseID = 1;

public:
	/** Protocol the DMX Activity Monitor uses */
	UPROPERTY(Config)
	FName ActivityMonitorProtocol;

	/** Source of packets to monitor in the DMX Activity Monitor  */
	UPROPERTY(Config)
	FName ActivityMonitorSource;

	/** ID of the first universe to monitor in the DMX Activity Monitor  */
	UPROPERTY(Config)
	uint16 ActivityMonitorMinUniverseID = 1;

	/** ID of the last universe to monitor in the DMX Activity Monitor */
	UPROPERTY(Config)
	uint16 ActivityMonitorMaxUniverseID = 100;
};
