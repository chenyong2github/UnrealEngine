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



/**  
 * DMX Project Settings. 
 * 
 * Note: To handle Port changes in code please refer to FDMXPortManager.
 */
UCLASS(Config = Engine, DefaultConfig, AutoExpandCategories = ("DMX|Communication Settings"), Meta = (DisplayName = "DMX"))
class DMXPROTOCOL_API UDMXProtocolSettings 
	: public UObject
{
	DECLARE_MULTICAST_DELEGATE_OneParam(FDMXOnSendDMXEnabled, bool /** bEnabled */);
	DECLARE_MULTICAST_DELEGATE_OneParam(FDMXOnReceiveDMXEnabled, bool /** bEnabled */);

public:
	GENERATED_BODY()

public:
	UDMXProtocolSettings();

	// ~Begin UObject Interface
	virtual void PostInitProperties() override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedChainEvent) override;
#endif // WITH_EDITOR
	// ~End UObject Interface

	/** Returns the input port matching the predicate, or nullptr if it cannot be found */
	template <typename Predicate>
	FDMXInputPortConfig* FindInputPortConfig(Predicate Pred)
	{
		return InputPortConfigs.FindByPredicate(Pred);
	}

	/** Returns the input port matching the predicate, or nullptr if it cannot be found */
	template <typename Predicate>
	FDMXOutputPortConfig* FindOutputPortConfig(Predicate Pred)
	{
		return OutputPortConfigs.FindByPredicate(Pred);
	}

	/** DMX Input Port Configs */
	UPROPERTY(Config, EditAnywhere, Category = "DMX|Communication Settings", Meta = (DisplayName = "Input Ports"))
	TArray<FDMXInputPortConfig> InputPortConfigs;

	/** DMX Output Port Configs */
	UPROPERTY(Config, EditAnywhere, Category = "DMX|Communication Settings", Meta = (DisplayName = "Output Ports"))
	TArray<FDMXOutputPortConfig> OutputPortConfigs;
		
	/** Rate at which DMX is sent, in Hz from 1 to 1000. 44Hz is recommended. */
	UPROPERTY(Config, EditAnywhere, Category = "DMX|Communication Settings", Meta = (ClampMin = "1", ClampMax = "1000"), Meta = (DisplayName = "DMX Send Rate"))
	uint32 SendingRefreshRate;

	/** Rate at which DMX is received, in Hz from 1 to 1000. 44Hz is recommended */
	UPROPERTY(Meta = (DeprecatedProperty, DeprecationMessage = "ReceivingRefreshRate is deprecated without replacement. It would prevent from precise timestamps on the receivers."))
	uint32 ReceivingRefreshRate_DEPRECATED;

	/** Fixture Categories ENum */
	UPROPERTY(Config, EditAnywhere, Category = "DMX|Fixture Settings", Meta = (DisplayName = "Fixture Categories"))
	TSet<FName> FixtureCategories;

	/** Common names to map Fixture Functions to and access them easily on Blueprints */
	UPROPERTY(Config, EditAnywhere, Category = "DMX|Fixture Settings", Meta = (DisplayName = "Fixture Attributes"))
	TSet<FDMXAttribute> Attributes;

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
	UPROPERTY(Config, EditAnywhere, Category = "DMX|Communication Settings", Meta = (AllowPrivateAccess = true, DisplayName = "Send DMX by default"))
	bool bDefaultSendDMXEnabled;

	/** Whether DMX is sent to the network. Recalled whenever editor or game starts.  */
	UPROPERTY(Config, EditAnywhere, Category = "DMX|Communication Settings", Meta = (AllowPrivateAccess = true, DisplayName = "Receive DMX by default"))
	bool bDefaultReceiveDMXEnabled;

	/** Overrides the default bDefaultSendDMXEnabled value at runtime */
	bool bOverrideSendDMXEnabled;

	/** Overrides the default bDefaultReceiveDMXEnabled value at runtime */
	bool bOverrideReceiveDMXEnabled;

	///////////////////
	// DEPRECATED 4.27
public:
	/** DEPRECATED 4.27 */
	UE_DEPRECATED(4.27, "Now in Port Configs to support many NICs")
	UPROPERTY(Config, Meta = (DeprecatedProperty, DeprecationMessage = "InterfaceIPAddress is deprecated. Use Ports instead."))
	FString InterfaceIPAddress_DEPRECATED;

	/** DEPRECATED 4.27 */
	UE_DEPRECATED(4.27, "Now in Port Configs to support many NICs")
	UPROPERTY(Config, Meta = (DeprecatedProperty, DeprecationMessage = "GlobalArtNetUniverseOffset is deprecated. Use Ports instead."))
	int32 GlobalArtNetUniverseOffset_DEPRECATED;

	/** DEPRECATED 4.27 */
	UE_DEPRECATED(4.27, "Now in Port Configs to support many NICs")
	UPROPERTY(Config, Meta = (DeprecatedProperty, DeprecationMessage = "GlobalSACNUniverseOffset is deprecated. Use Ports instead."))
	int32 GlobalSACNUniverseOffset_DEPRECATED;
};
