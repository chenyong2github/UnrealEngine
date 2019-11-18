// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"

#include "SoundControlBusMix.h"

#include "AudioModulationStatics.generated.h"

// Forward Declarations
class USoundControlBus;
class USoundControlBusMix;


namespace AudioModulation
{
	class FAudioModulationImpl;
} // namespace AudioModulation


UCLASS()
class UAudioModulationStatics : public UBlueprintFunctionLibrary
{
	GENERATED_UCLASS_BODY()

public:
	/**
	 * Returns world associated with provided context object
	 */
	static UWorld* GetAudioWorld(const UObject* WorldContextObject);

	/**
	 * Returns modulation implementation associated with the provided world
	 */
	static AudioModulation::FAudioModulationImpl* GetModulationImpl(UWorld* World);

	/** Activates a bus. Does nothing if an instance of the provided bus is already active
	 * @param Bus - Bus to activate
	 */
	UFUNCTION(BlueprintCallable, Category = "Audio", DisplayName = "Activate Control Bus", meta = (WorldContext = "WorldContextObject", Keywords = "activate modulation modulator control bus"))
	static void ActivateBus(const UObject* WorldContextObject, USoundControlBusBase* Bus);

	/** Activates a bus modulator mix. Does nothing if an instance of the provided bus mix is already active
	 * @param BusMix - Mix to activate
	 */
	UFUNCTION(BlueprintCallable, Category = "Audio", DisplayName = "Activate Control Bus Mix", meta = (WorldContext = "WorldContextObject", Keywords = "activate modulation modulator control bus mix"))
	static void ActivateBusMix(const UObject* WorldContextObject, USoundControlBusMix* BusMix);

	/** Activates a bus modulator (eg. LFO). Does nothing if an instance of the provided modulator is already active
	 * @param Modulator - Modulator to activate
	 */
	UFUNCTION(BlueprintCallable, Category = "Audio", DisplayName = "Activate Control Bus Modulator", meta = (WorldContext = "WorldContextObject", Keywords = "activate modulation modulator lfo"))
	static void ActivateBusModulator(const UObject* WorldContextObject, USoundBusModulatorBase* Modulator);

	/** Creates a volume modulation bus with the provided default value.
	 * @param Name - Name of bus
	 * @param DefaultValue - Default value for created bus
	 * @param Activate - Whether or not to activate bus on creation.
	 */
	UFUNCTION(BlueprintCallable, Category = "Audio", DisplayName = "Create Volume Control Bus", meta = (WorldContext = "WorldContextObject", Keywords = "make create bus modulation volume modulator"))
	static USoundVolumeControlBus* CreateVolumeBus(const UObject* WorldContextObject, FName Name, float DefaultValue, bool Activate);

	/** Creates a pitch modulation bus with the provided default value.
	 * @param Name - Name of bus
	 * @param DefaultValue - Default value for created bus
	 * @param Activate - Whether or not to activate bus on creation.
	 */
	UFUNCTION(BlueprintCallable, Category = "Audio", DisplayName = "Create Pitch Control Bus", meta = (WorldContext = "WorldContextObject", Keywords = "make create bus modulation pitch modulator"))
	static USoundPitchControlBus* CreatePitchBus(const UObject* WorldContextObject, FName Name, float DefaultValue, bool Activate);

	/** Creates a high-pass filter (HPF) modulation bus with the provided default value.
	 * @param Name - Name of bus
	 * @param DefaultValue - Default value for created bus
	 * @param Activate - Whether or not to activate bus on creation.
	 */
	UFUNCTION(BlueprintCallable, Category = "Audio", DisplayName = "Create HPF Control Bus", meta = (WorldContext = "WorldContextObject", Keywords = "make create bus modulation HPF modulator"))
	static USoundHPFControlBus* CreateHPFBus(const UObject* WorldContextObject, FName Name, float DefaultValue, bool Activate);

	/** Creates a low-pass filter (LPF) modulation bus with the provided default value.
	 * @param Name - Name of bus
	 * @param DefaultValue - Default value for created bus
	 * @param Activate - Whether or not to activate bus on creation.
	 */
	UFUNCTION(BlueprintCallable, Category = "Audio", DisplayName = "Create LPF Control Bus", meta = (WorldContext = "WorldContextObject", Keywords = "make create bus modulation LPF modulator"))
	static USoundLPFControlBus* CreateLPFBus(const UObject* WorldContextObject, FName Name, float DefaultValue, bool Activate);

	/** Creates an LFO modulator.
	 * @param Name - Name of LFO
	 * @param Amplitude - Amplitude of new LFO.
	 * @param Frequency - Frequency of new LFO.
	 * @param Offset - Offset of new LFO.
	 * @param Activate - Whether or not to activate lfo on creation.
	 */
	UFUNCTION(BlueprintCallable, Category = "Audio", DisplayName = "Create Control Bus LFO", meta = (WorldContext = "WorldContextObject", Keywords = "make create lfo modulation modulator"))
	static USoundBusModulatorLFO* CreateLFO(const UObject* WorldContextObject, FName Name, float Amplitude, float Frequency, float Offset, bool Activate);

	/** Creates a channel used to mix a control bus.
	 * @param Bus - Bus channel is in charge of applying mix value to.
	 * @param Channels - Value for added bus channel to target when mix is active.
	 * @param Attack/ReleaseTime - Time in seconds for channel to mix in/out.
	 */
	UFUNCTION(BlueprintCallable, Category = "Audio", DisplayName = "Create Control Bus Mix Channel", meta = (AdvancedDisplay = "3", WorldContext = "WorldContextObject", Keywords = "make create control bus mix modulation modulator channel"))
	static FSoundControlBusMixChannel CreateBusMixChannel(const UObject* WorldContextObject, USoundControlBusBase* Bus, float Value, float AttackTime = 0.1f, float ReleaseTime = 0.1f);

	/** Creates a modulation bus mix and adds a bus channel set to the provided target value
	 * @param Name - Name of mix.
	 * @param Channels - Channels mix is responsible for.
	 * @param Activate - Whether or not to activate mix on creation.
	 */
	UFUNCTION(BlueprintCallable, Category = "Audio", DisplayName = "Create Control Bus Mix", meta = (WorldContext = "WorldContextObject", Keywords = "make create control bus mix modulation modulator"))
	static USoundControlBusMix* CreateBusMix(const UObject* WorldContextObject, FName Name, TArray<FSoundControlBusMixChannel> Channels, bool Activate);

	/** Deactivates a bus. Does nothing if an instance of the provided bus is already inactive
	 * @param Bus - Scope of modulator
	 */
	UFUNCTION(BlueprintCallable, Category = "Audio", DisplayName = "Deactivate Control Bus", meta = (WorldContext = "WorldContextObject", Keywords = "deactivate modulation modulator bus"))
	static void DeactivateBus(const UObject* WorldContextObject, USoundControlBusBase* Bus);

	/** Deactivates a modulation bus mix. Does nothing if an instance of the provided bus mix is already inactive
	 * @param BusMix - Mix to deactivate
	 */
	UFUNCTION(BlueprintCallable, Category = "Audio", DisplayName = "Deactivate Control Bus Mix", meta = (WorldContext = "WorldContextObject", Keywords = "deactivate modulation modulator"))
	static void DeactivateBusMix(const UObject* WorldContextObject, USoundControlBusMix* BusMix);

	/** Deactivates a bus modulator. Does nothing if an instance of the provided bus mix is already inactive
	 * @param Modulator - Modulator to activate
	 * @param Scope - Scope of modulator
	 */
	UFUNCTION(BlueprintCallable, Category = "Audio", DisplayName = "Deactivate Control Bus Modulator", meta = (WorldContext = "WorldContextObject", Keywords = "deactivate bus modulation modulator"))
	static void DeactivateBusModulator(const UObject* WorldContextObject, USoundBusModulatorBase* Modulator);

	/** Updates a mix with the provided channel data.
	 * @param Mix - Mix to update
	 * @param Channels - Channels to set.  If channel's bus is not referenced by mix, channel's update request is ignored.
	 */
	UFUNCTION(BlueprintCallable, Category = "Audio", DisplayName = "Update Control Bus Mix", meta = (WorldContext = "WorldContextObject", Keywords = "update bus control modulation modulator mix channel"))
	static void UpdateMix(const UObject* WorldContextObject, USoundControlBusMix* Mix, TArray<FSoundControlBusMixChannel> Channels);

	/** Updates filtered channels of a given class to a provided target value.
	 * @param Mix - Mix to modify
	 * @param AddressFilter - Address filter to apply to provided mix's channels.
	 * @param BusClass - Filters buses by subclass.
	 * @param Value - Target value to mix filtered channels to.
	 * @param AttackTime - If non-negative, updates the attack time for the resulting bus channels found matching the provided filter.
	 * @param ReleaseTime - If non-negative, updates the release time for the resulting bus channels found matching the provided filter.
	 */
	UFUNCTION(BlueprintCallable, Category = "Audio", DisplayName = "Update Control Bus Mix By Filter", meta = (AdvancedDisplay = "5", WorldContext = "WorldContextObject", Keywords = "update bus control class modulation modulator mix channel value filter"))
	static void UpdateMixByFilter(
		const UObject*						WorldContextObject,
		USoundControlBusMix*				Mix,
		FString								AddressFilter,
		TSubclassOf<USoundControlBusBase>	BusClassFilter,
		float								Value,
		float								AttackTime	= -1.0f,
		float								ReleaseTime = -1.0f);

	/** Commits updates from a UObject definition of a modulator (e.g. Bus, Bus Mix, LFO) to active instance (does not apply if modulator has not been activated).
	 * @param Modulator - Modulator to update
	 */
	UFUNCTION(BlueprintCallable, Category = "Audio", DisplayName = "Update Modulator", meta = (WorldContext = "WorldContextObject", Keywords = "update set control bus mix modulation modulator"))
	static void UpdateModulator(const UObject* WorldContextObject, USoundModulatorBase* Modulator);
};
