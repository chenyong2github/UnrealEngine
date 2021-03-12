// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DMXProtocolTypes.h"
#include "DMXAttribute.h"
#include "IO/DMXInputPortConfig.h"
#include "IO/DMXOutputPortConfig.h"

#include "CoreMinimal.h"
#include "Misc/Optional.h"
#include "UObject/Object.h"

#include "DMXProtocolSettings.generated.h"



/**  DMX Project Settings */
UCLASS(Config = Engine, DefaultConfig, NotPlaceable)
class DMXPROTOCOL_API UDMXProtocolSettings 
	: public UObject
{
	DECLARE_MULTICAST_DELEGATE_OneParam(FDMXOnSendDMXEnabled, bool /** bEnabled */);
	DECLARE_MULTICAST_DELEGATE_OneParam(FDMXOnReceiveDMXEnabled, bool /** bEnabled */);

public:
	GENERATED_BODY()

public:
	UDMXProtocolSettings();

	virtual void PostInitProperties() override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedChainEvent) override;
#endif // WITH_EDITOR

public:
	/** Manual Interface IP Address */
	UPROPERTY(Meta = (DeprecatedProperty, DeprecationMessage = "InterfaceIPAddress is deprecated. Use Ports instead."))
	FString InterfaceIPAddress_DEPRECATED;

	/** DMX Input Port Configs */
	UPROPERTY(Config, EditAnywhere, Category = "DMX|Communication Settings", Meta = (DisplayName = "Input Ports"))
	TArray<FDMXInputPortConfig> InputPortConfigs;

	/** DMX Output Port Configs */
	UPROPERTY(Config, EditAnywhere, Category = "DMX|Communication Settings", Meta = (DisplayName = "Output Ports"))
	TArray<FDMXOutputPortConfig> OutputPortConfigs;

	/** Universe Remote Start for ArtNet */
	UPROPERTY(Meta = (DeprecatedProperty, DeprecationMessage = "GlobalArtNetUniverseOffset is deprecated. Use Ports instead."))
	int32 GlobalArtNetUniverseOffset_DEPRECATED;

	/** Universe Remote Start for sACN */
	UPROPERTY(Meta = (DeprecatedProperty, DeprecationMessage = "GlobalSACNUniverseOffset is deprecated. Use Ports instead."))
	int32 GlobalSACNUniverseOffset_DEPRECATED;

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
	UPROPERTY(Meta = (DeprecatedProperty, DeprecationMessage = "ReceivingRefreshRate is deprecated without replacement. It would deter timestamps on the receivers. Instead use a per object rate where desired."))
	uint32 ReceivingRefreshRate;

	/** Broadcast when send DMX is enabled or disabled */
	FDMXOnSendDMXEnabled OnSetSendDMXEnabled;

	/** Broadcast when receive DMX is enabled or disabled */
	FDMXOnReceiveDMXEnabled OnSetReceiveDMXEnabled;

	/** Returns whether send DMX is currently enabled, considering runtime override */
	bool IsSendDMXEnabled() const { return bOverrideSendDMXEnabled; }

	/** Overrides if send DMX is enabled at runtime */
	void OverrideSendDMXEnabled(bool bEnabled);

	/** Returns whether receive DMX is currently enabled, considering runtime override */
	bool IsReceiveDMXEnabled() const { return bOverrideReceiveDMXEnabled; }

	/** Overrides if send DMX is enabled at runtime */
	void OverrideReceiveDMXEnabled(bool bEnabled);

private:
	/** Whether DMX is received from the network. Recalled whenever editor or game starts. */
	UPROPERTY(Config, EditAnywhere, Category = "DMX|Sending Settings", Meta = (AllowPrivateAccess = true, DisplayName = "Send DMX by default"))
	bool bDefaultSendDMXEnabled;

	/** Whether DMX is sent to the network. Recalled whenever editor or game starts.  */
	UPROPERTY(Config, EditAnywhere, Category = "DMX|Receiving Settings", Meta = (AllowPrivateAccess = true, DisplayName = "Receive DMX by default"))
	bool bDefaultReceiveDMXEnabled;

	/** Overrides the default bDefaultSendDMXEnabled value at runtime */
	bool bOverrideReceiveDMXEnabled;

	/** Overrides the default bDefaultSendDMXEnabled value at runtime */
	bool bOverrideSendDMXEnabled;
};
