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
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
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

	/** Rate at which DMX is sent, in Hz from 1 to 1000. 44Hz is recommended. */
	UPROPERTY(Config, EditAnywhere, Category = "DMX|Sending Settings", Meta = (ClampMin = "1", ClampMax = "1000"))
	uint32 SendingRefreshRate;

	/** Rate at which DMX is received, in Hz from 1 to 1000. 44Hz is recommended */
	UPROPERTY(Config, EditAnywhere, Category = "DMX|Receiving Settings", Meta = (ClampMin = "1", ClampMax = "1000", EditCondition = "bUseSeparateReceivingThread"))
	uint32 ReceivingRefreshRate;

	/** Returns whether send DMX is currently enabled, considering runtime override */
	bool IsSendDMXEnabled() const { return bOverrideSendDMXEnabled; }

	/** Overrides if send DMX is enabled at runtime */
	void OverrideSendDMXEnabled(bool bEnabled) { bOverrideSendDMXEnabled = bEnabled; }

	/** Returns whether receive DMX is currently enabled, considering runtime override */
	bool IsReceiveDMXEnabled() const { return bOverrideReceiveDMXEnabled; }

	/** Overrides if send DMX is enabled at runtime */
	void OverrideReceiveDMXEnabled(bool bEnabled) { bOverrideReceiveDMXEnabled = bEnabled; }

private:	
	/** Whether DMX is received from the network. Recalled whenever editor or game starts. */
	UPROPERTY(Config, EditAnywhere, Category = "DMX|Sending Settings", Meta = (AllowPrivateAccess = true, DisplayName = "Send DMX by default"))
	bool bDefaultReceiveDMXEnabled;

	/** Whether DMX is sent to the network. Recalled whenever editor or game starts.  */
	UPROPERTY(Config, EditAnywhere, Category = "DMX|Receiving Settings", Meta = (AllowPrivateAccess = true, DisplayName = "Receive DMX by default"))
	bool bDefaultSendDMXEnabled;

	/** Overrides the default bDefaultSendDMXEnabled at runtime */
	bool bOverrideReceiveDMXEnabled;

	/** Overrides the default bDefaultSendDMXEnabled at runtime */
	bool bOverrideSendDMXEnabled;
};
