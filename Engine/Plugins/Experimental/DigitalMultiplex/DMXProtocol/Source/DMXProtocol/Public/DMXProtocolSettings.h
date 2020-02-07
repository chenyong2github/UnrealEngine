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
	UPROPERTY(GlobalConfig, EditAnywhere, Category = "DMX|Communication Settings", Meta = (DisplayName = "Interface IP address"))
	FString InterfaceIPAddress;

	UPROPERTY(GlobalConfig, EditAnywhere, Category = "DMX|Unicast")
	bool bShouldUseUnicast;

	UPROPERTY(GlobalConfig, EditAnywhere, Category = "DMX|Unicast", Meta = (EditCondition="bShouldUseUnicast"))
	FString UnicastEndpoint;

	/** Universe Remote Start for ArtNet */
	UPROPERTY(GlobalConfig, EditAnywhere, Category = "DMX|Universe Settings", Meta = (DisplayName = "Global Art-Net Universe Offset", ClampMin = "0", ClampMax = "65535"))
	int32 GlobalArtNetUniverseOffset;

	/** Universe Remote Start for sACN */
	UPROPERTY(GlobalConfig, EditAnywhere, Category = "DMX|Universe Settings", Meta = (DisplayName = "Global sACN Universe Offset", ClampMin = "0", ClampMax = "65535"))
	int32 GlobalSACNUniverseOffset;

	/** Fixture Categories ENum */
	UPROPERTY(GlobalConfig, EditAnywhere, Category = "DMX|Fixture Settings", Meta = (DisplayName = "Fixture Categories"))
	TSet<FName> FixtureCategories;
};
