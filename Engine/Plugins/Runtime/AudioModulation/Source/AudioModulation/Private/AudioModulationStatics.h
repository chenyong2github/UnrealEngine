// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"

#include "AudioModulationStatics.generated.h"

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
	UFUNCTION(BlueprintCallable, Category = "Audio", DisplayName = "Activate Sound Control Bus", meta = (WorldContext = "WorldContextObject", Keywords = "activate modulation modulator control bus"))
	static void ActivateBus(const UObject* WorldContextObject, USoundControlBusBase* Bus);

	/** Activates a bus modulator mix. Does nothing if an instance of the provided bus mix is already active
	 * @param BusMix - Mix to activate
	 */
	UFUNCTION(BlueprintCallable, Category = "Audio", DisplayName = "Activate Sound Control Bus Mix", meta = (WorldContext = "WorldContextObject", Keywords = "activate modulation modulator control bus mix"))
	static void ActivateBusMix(const UObject* WorldContextObject, USoundControlBusMix* BusMix);

	/** Activates a bus modulator (eg. LFO). Does nothing if an instance of the provided modulator is already active
	 * @param Modulator - Modulator to activate
	 */
	UFUNCTION(BlueprintCallable, Category = "Audio", DisplayName = "Activate Sound Bus Modulator", meta = (WorldContext = "WorldContextObject", Keywords = "activate modulation modulator lfo"))
	static void ActivateBusModulator(const UObject* WorldContextObject, USoundBusModulatorBase* Modulator);

	/** Creates a volume modulation bus with the provided default value.
	 * @param Name - Name of bus
	 * @param DefaultValue - Default value for created bus
	 * @param Activate - Whether or not to activate bus on creation.
	 */
	UFUNCTION(BlueprintCallable, Category = "Audio", DisplayName = "Create Volume Bus", meta = (WorldContext = "WorldContextObject", Keywords = "create bus modulation volume modulator"))
	static USoundVolumeControlBus* CreateVolumeBus(const UObject* WorldContextObject, FName Name, float DefaultValue, bool Activate);

	/** Creates a pitch modulation bus with the provided default value.
	 * @param Name - Name of bus
	 * @param DefaultValue - Default value for created bus
	 * @param Activate - Whether or not to activate bus on creation.
	 */
	UFUNCTION(BlueprintCallable, Category = "Audio", DisplayName = "Create Sound Pitch Control Bus", meta = (WorldContext = "WorldContextObject", Keywords = "create bus modulation pitch modulator"))
	static USoundPitchControlBus* CreatePitchBus(const UObject* WorldContextObject, FName Name, float DefaultValue, bool Activate);

	/** Creates a high-pass filter (HPF) modulation bus with the provided default value.
	 * @param Name - Name of bus
	 * @param DefaultValue - Default value for created bus
	 * @param Activate - Whether or not to activate bus on creation.
	 */
	UFUNCTION(BlueprintCallable, Category = "Audio", DisplayName = "Create Sound High-Pass Control Bus", meta = (WorldContext = "WorldContextObject", Keywords = "create bus modulation HPF modulator"))
	static USoundHPFControlBus* CreateHPFBus(const UObject* WorldContextObject, FName Name, float DefaultValue, bool Activate);

	/** Creates a low-pass filter (LPF) modulation bus with the provided default value.
	 * @param Name - Name of bus
	 * @param DefaultValue - Default value for created bus
	 * @param Activate - Whether or not to activate bus on creation.
	 */
	UFUNCTION(BlueprintCallable, Category = "Audio", DisplayName = "Create Sound Low-Pass Control Bus", meta = (WorldContext = "WorldContextObject", Keywords = "create bus modulation LPF modulator"))
	static USoundLPFControlBus* CreateLPFBus(const UObject* WorldContextObject, FName Name, float DefaultValue, bool Activate);

	/** Creates a modulation LFO.
	 * @param Name - Name of LFO
	 * @param Amplitude - Amplitude of new LFO.
	 * @param Frequency - Frequency of new LFO.
	 * @param Offset - Offset of new LFO.
	 * @param Activate - Whether or not to activate lfo on creation.
	 */
	UFUNCTION(BlueprintCallable, Category = "Audio", DisplayName = "Create Sound Modulation Bus LFO", meta = (WorldContext = "WorldContextObject", Keywords = "create lfo modulation modulator"))
	static USoundBusModulatorLFO* CreateLFO(const UObject* WorldContextObject, FName Name, float Amplitude, float Frequency, float Offset, bool Activate);

	/** Creates a modulation bus mix and adds a bus channel set to the provided target value
	 * @param Bus - Bus to add to mix
	 * @param TargetValue - value for added bus channel to target
	 * @param Activate - Whether or not to activate mix on creation.
	 */
	UFUNCTION(BlueprintCallable, Category = "Audio", DisplayName = "Create Sound Control Bus Mix", meta = (WorldContext = "WorldContextObject", Keywords = "create bus mix modulation modulator"))
	static USoundControlBusMix* CreateBusMix(const UObject* WorldContextObject, FName Name, TArray<USoundControlBusBase*> Buses, float TargetValue, bool Activate);

	/** Deactivates a bus. Does nothing if an instance of the provided bus is already inactive
	 * @param Bus - Scope of modulator
	 */
	UFUNCTION(BlueprintCallable, Category = "Audio", DisplayName = "Deactivate Sound Modulation Bus", meta = (WorldContext = "WorldContextObject", Keywords = "deactivate modulation modulator bus"))
	static void DeactivateBus(const UObject* WorldContextObject, USoundControlBusBase* Bus);

	/** Deactivates a modulation bus mix. Does nothing if an instance of the provided bus mix is already inactive
	 * @param BusMix - Mix to deactivate
	 */
	UFUNCTION(BlueprintCallable, Category = "Audio", DisplayName = "Deactivate Sound Bus Modulation Mix", meta = (WorldContext = "WorldContextObject", Keywords = "deactivate modulation modulator"))
	static void DeactivateBusMix(const UObject* WorldContextObject, USoundControlBusMix* BusMix);

	/** Deactivates a bus modulator. Does nothing if an instance of the provided bus mix is already inactive
	 * @param Modulator - Modulator to activate
	 * @param Scope - Scope of modulator
	 */
	UFUNCTION(BlueprintCallable, Category = "Audio", DisplayName = "Deactivate Sound Bus Modulator", meta = (WorldContext = "WorldContextObject", Keywords = "deactivate bus modulation modulator"))
	static void DeactivateBusModulator(const UObject* WorldContextObject, USoundBusModulatorBase* Modulator);

	/** Commits updates from a UObject definition of a modulator (e.g. Bus, Bus Mix, LFO) to active instance (does not apply if modulator has not been activated).
	 * @param Modulator - Modulator to update
	 */
	UFUNCTION(BlueprintCallable, Category = "Audio", DisplayName = "Update Sound Modulator", meta = (WorldContext = "WorldContextObject", Keywords = "update set control bus mix modulation modulator"))
	static void UpdateModulator(const UObject* WorldContextObject, USoundModulatorBase* Modulator);
};
