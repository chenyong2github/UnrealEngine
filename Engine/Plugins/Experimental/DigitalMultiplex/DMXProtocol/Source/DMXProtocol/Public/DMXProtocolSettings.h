// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/Optional.h"
#include "UObject/Object.h"
#include "DMXProtocolTypes.h"
#include "DMXAttribute.h"

#include "DMXProtocolSettings.generated.h"

/**  User defined protocol settings that apply to a whole protocol module */
UCLASS(config = Engine, defaultconfig, notplaceable)
class DMXPROTOCOL_API UDMXProtocolSettings : public UObject
{
public:
	GENERATED_BODY()

public:
	UDMXProtocolSettings();

#if WITH_EDITOR
	virtual void PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent) override;
#endif
public:
	/** Manual Interface IP Address */
	UPROPERTY(Config, EditAnywhere, Category = "DMX|Communication Settings", Meta = (DisplayName = "Interface IP address"))
	FString InterfaceIPAddress;

	/** Universe Remote Start for ArtNet */
	UPROPERTY(Config, EditAnywhere, Category = "DMX|Universe Settings", Meta = (DisplayName = "Global Art-Net Universe Offset", ClampMin = "0", ClampMax = "65535"))
	int32 GlobalArtNetUniverseOffset;

	/** Universe Remote Start for sACN */
	UPROPERTY(Config, EditAnywhere, Category = "DMX|Universe Settings", Meta = (DisplayName = "Global sACN Universe Offset", ClampMin = "0", ClampMax = "65535"))
	int32 GlobalSACNUniverseOffset;

	/** Fixture Categories ENum */
	UPROPERTY(Config, EditAnywhere, Category = "DMX|Fixture Settings", Meta = (DisplayName = "Fixture Categories"))
	TSet<FName> FixtureCategories;

	/** Common names to map Fixture Functions to and access them easily on Blueprints */
	UPROPERTY(Config, EditAnywhere, Category = "DMX|Fixture Settings", Meta = (DisplayName = "Fixture Function Attributes"))
	TSet<FDMXAttribute> Attributes;

	/**
	 * DMX packets senting refresh rate.
	 * User can set the value from 0 up to 1000
	 */
	UPROPERTY(Config, EditAnywhere, Category = "DMX|Sending Settings", Meta = (ClampMin = "0", ClampMax = "1000"))
	int32 SendingRefreshRate;
};
