// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "DMXProtocolTypes.h"
#include "DMXProtocolSettings.generated.h"

/**  User defined protocol settings that apply to a whole protocol module */
UCLASS(config = Engine, notplaceable)
class DMXPROTOCOL_API UDMXProtocolSettings : public UObject
{
public:
	GENERATED_BODY()

public:
	UDMXProtocolSettings();

#if WITH_EDITOR
	void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent);
#endif

public:
	/** Manual Interface IP Address */
	UPROPERTY(Config, EditAnywhere, Category = "DMX|Communication Settings", Meta = (DisplayName = "Interface IP address"))
	FString InterfaceIPAddress;

	UPROPERTY(Config, EditAnywhere, Category = "DMX|Unicast")
	bool bShouldUseUnicast;

	UPROPERTY(Config, EditAnywhere, Category = "DMX|Unicast", Meta = (EditCondition="bShouldUseUnicast"))
	FString UnicastEndpoint;

	/** Universe Remote Start for ArtNet */
	UPROPERTY(Config, EditAnywhere, Category = "DMX|Universe Settings", Meta = (DisplayName = "Global Art-Net Universe Offset", ClampMin = "0", ClampMax = "65535"))
	int32 GlobalArtNetUniverseOffset;

	/** Universe Remote Start for sACN */
	UPROPERTY(Config, EditAnywhere, Category = "DMX|Universe Settings", Meta = (DisplayName = "Global sACN Universe Offset", ClampMin = "0", ClampMax = "65535"))
	int32 GlobalSACNUniverseOffset;

	/** Fixture Categories ENum */
	UPROPERTY(Config, EditAnywhere, Category = "DMX|Fixture Settings", Meta = (DisplayName = "Fixture Categories"))
	TSet<FName> FixtureCategories;

	//~ Properties controlled by the Input Console (SDMXInputInfoSelecter)

	/** Set the current protocol to be monitored */
	UPROPERTY(Config)
	FName InputConsoleProtocol;

	/** Set the current Universe ID to be monitored */
	UPROPERTY(Config)
	uint16 InputConsoleUniverseID;
};
