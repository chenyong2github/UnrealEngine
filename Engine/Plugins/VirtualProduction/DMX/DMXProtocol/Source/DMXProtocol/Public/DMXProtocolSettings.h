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

	virtual void PostInitProperties() override;

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
	UPROPERTY(Config, EditAnywhere, Category = "DMX|Fixture Settings", Meta = (DisplayName = "Fixture Attributes"))
	TSet<FDMXAttribute> Attributes;

	/** Rate at which DMX is sent, in Hz from epsilon to 1000. 44Hz by standIf set to 0, DMX is sent instantly (not recommended). */
	UPROPERTY(Config, EditAnywhere, Category = "DMX|Sending Settings", Meta = (ClampMin = "0", ClampMax = "1000"))
	int32 SendingRefreshRate;

	/** Whether DMX is received from the network by default.  */
	UPROPERTY(Config, EditAnywhere, Category = "DMX|Receiving Settings", Meta = (DisplayName = "Receive DMX by default"))
	bool bDefaultReceiveDMXEnabled;

	/** Rate at which DMX is received, in Hz from epsilon to 1000. If set to 0, a receive event is raised for each inbound DMX packet (not recommended). */
	UPROPERTY(Config, EditAnywhere, Category = "DMX|Receiving Settings", Meta = (ClampMin = "0", ClampMax = "1000", EditCondition = "bUseSeparateReceivingThread"))
	int32 ReceivingRefreshRate;

	/** If true, received DMX packets are handled in a separate thread */
	UPROPERTY(Config, EditAnywhere, Category = "DMX|Receiving Settings")
	bool bUseSeparateReceivingThread;

private:
	/**  Helper to apply bDefaultReceiveDMXEnabled globally on property changes */
	void GlobalSetReceiveDMXEnabled(bool bReceiveDMXEnabled);
};
