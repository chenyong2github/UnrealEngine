// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"

#include "AudioModulationStatics.generated.h"


class USoundModulatorBus;
class USoundModulatorBusMix;

UCLASS()
class UAudioModulationStatics : public UBlueprintFunctionLibrary
{
	GENERATED_UCLASS_BODY()

public:
	/** Activates a bus. Does nothing if an instance of the provided bus is already active
	 * @param Bus - Bus to activate
	 */
	UFUNCTION(BlueprintCallable, Category = "Audio", DisplayName = "Activate Sound Bus", meta = (WorldContext = "WorldContextObject", Keywords = "activate modulation modulator bus"))
	static void ActivateBus(const UObject* WorldContextObject, USoundModulatorBusBase* Bus);

	/** Activates a bus modulator mix. Does nothing if an instance of the provided bus mix is already active
	 * @param BusMix - Mix to activate
	 */
	UFUNCTION(BlueprintCallable, Category = "Audio", DisplayName = "Activate Sound Modulation Bus Mix", meta = (WorldContext = "WorldContextObject", Keywords = "activate modulation modulator bus mix"))
	static void ActivateBusMix(const UObject* WorldContextObject, USoundModulatorBusMix* BusMix);

	/** Activates a modulator. Does nothing if an instance of the provided modulator is already active
	 * @param Modulator - Modulator to activate
	 */
	UFUNCTION(BlueprintCallable, Category = "Audio", DisplayName = "Activate Sound Modulator", meta = (WorldContext = "WorldContextObject", Keywords = "activate modulation modulator"))
	static void ActivateModulator(const UObject* WorldContextObject, USoundModulatorBase* Modulator);

	/** Creates a volume modulation bus with the provided default value.
	 * @param Name - Name of bus
	 * @param DefaultValue - Default value for created bus
	 * @param Activate - Whether or not to activate bus on creation.
	 */
	UFUNCTION(BlueprintCallable, Category = "Audio", DisplayName = "Create Volume Bus", meta = (WorldContext = "WorldContextObject", Keywords = "create bus modulation volume modulator"))
	static USoundVolumeModulatorBus* CreateVolumeBus(const UObject* WorldContextObject, FName Name, float DefaultValue, bool Activate);

	/** Creates a pitch modulation bus with the provided default value.
	 * @param Name - Name of bus
	 * @param DefaultValue - Default value for created bus
	 * @param Activate - Whether or not to activate bus on creation.
	 */
	UFUNCTION(BlueprintCallable, Category = "Audio", DisplayName = "Create Sound Modulator Pitch Bus", meta = (WorldContext = "WorldContextObject", Keywords = "create bus modulation pitch modulator"))
	static USoundPitchModulatorBus* CreatePitchBus(const UObject* WorldContextObject, FName Name, float DefaultValue, bool Activate);

	/** Creates a high-pass filter (HPF) modulation bus with the provided default value.
	 * @param Name - Name of bus
	 * @param DefaultValue - Default value for created bus
	 * @param Activate - Whether or not to activate bus on creation.
	 */
	UFUNCTION(BlueprintCallable, Category = "Audio", DisplayName = "Create Sound Modulator High-Pass Bus", meta = (WorldContext = "WorldContextObject", Keywords = "create bus modulation HPF modulator"))
	static USoundHPFModulatorBus* CreateHPFBus(const UObject* WorldContextObject, FName Name, float DefaultValue, bool Activate);

	/** Creates a low-pass filter (LPF) modulation bus with the provided default value.
	 * @param Name - Name of bus
	 * @param DefaultValue - Default value for created bus
	 * @param Activate - Whether or not to activate bus on creation.
	 */
	UFUNCTION(BlueprintCallable, Category = "Audio", DisplayName = "Create Sound Modulator Low-Pass Bus", meta = (WorldContext = "WorldContextObject", Keywords = "create bus modulation LPF modulator"))
	static USoundLPFModulatorBus* CreateLPFBus(const UObject* WorldContextObject, FName Name, float DefaultValue, bool Activate);

	/** Creates a modulation LFO.
	 * @param Name - Name of LFO
	 * @param Amplitude - Amplitude of new LFO.
	 * @param Frequency - Frequency of new LFO.
	 * @param Offset - Offset of new LFO.
	 * @param Activate - Whether or not to activate lfo on creation.
	 */
	UFUNCTION(BlueprintCallable, Category = "Audio", DisplayName = "Create Sound Modulation LFO", meta = (WorldContext = "WorldContextObject", Keywords = "create lfo modulation modulator"))
	static USoundModulatorLFO* CreateLFO(const UObject* WorldContextObject, FName Name, float Amplitude, float Frequency, float Offset, bool Activate);

	/** Creates a modulation bus mix and adds a bus channel set to the provided target value
	 * @param Bus - Bus to add to mix
	 * @param TargetValue - value for added bus channel to target
	 * @param Activate - Whether or not to activate mix on creation.
	 */
	UFUNCTION(BlueprintCallable, Category = "Audio", DisplayName = "Create Sound Modulation Bus Mix", meta = (WorldContext = "WorldContextObject", Keywords = "create bus mix modulation modulator"))
	static USoundModulatorBusMix* CreateBusMix(const UObject* WorldContextObject, FName Name, TArray<USoundModulatorBusBase*> Buses, float TargetValue, bool Activate);

	/** Deactivates a bus. Does nothing if an instance of the provided bus is already inactive
	 * @param Bus - Scope of modulator
	 */
	UFUNCTION(BlueprintCallable, Category = "Audio", DisplayName = "Deactivate Sound Modulator Bus", meta = (WorldContext = "WorldContextObject", Keywords = "deactivate modulation modulator bus"))
	static void DeactivateBus(const UObject* WorldContextObject, USoundModulatorBusBase* Bus);

	/** Deactivates a modulation bus mix. Does nothing if an instance of the provided bus mix is already inactive
	 * @param BusMix - Mix to deactivate
	 */
	UFUNCTION(BlueprintCallable, Category = "Audio", DisplayName = "Deactivate Sound Modulation Bus Mix", meta = (WorldContext = "WorldContextObject", Keywords = "deactivate modulation modulator"))
	static void DeactivateBusMix(const UObject* WorldContextObject, USoundModulatorBusMix* BusMix);

	/** Deactivates a modulator. Does nothing if an instance of the provided bus mix is already inactive
	 * @param Modulator - Modulator to activate
	 * @param Scope - Scope of modulator
	 */
	UFUNCTION(BlueprintCallable, Category = "Audio", DisplayName = "Deactivate Sound Modulator", meta = (WorldContext = "WorldContextObject", Keywords = "deactivate modulation modulator"))
	static void DeactivateModulator(const UObject* WorldContextObject, USoundModulatorBase* Modulator);

	/** Sets a bus's default value
	 * @param Bus - Bus of which to modify default value
	 * @param Value - Value for channel to target
	 */
	UFUNCTION(BlueprintCallable, Category = "Audio", DisplayName = "Set Bus Default Value", meta = (WorldContext = "WorldContextObject", Keywords = "set bus default modulation modulator"))
	static void SetBusDefault(const UObject* WorldContextObject, USoundModulatorBusBase* Bus, float Value);

	/** Sets a mix's bus channel(s) to the provided target value
	 * @param BusMix - BusMix to adjust channel of
	 * @param Bus - Bus of channel to modify the target value
	 * @param TargetValue - Value for channel to target
	 */
	UFUNCTION(BlueprintCallable, Category = "Audio", DisplayName = "Set Bus Mix Channel", meta = (WorldContext = "WorldContextObject", Keywords = "set bus mix channel modulation modulator"))
	static void SetBusMixChannel(const UObject* WorldContextObject, USoundModulatorBusMix* BusMix, USoundModulatorBusBase* Bus, float TargetValue);


	/** Sets an LFO's frequency to the provided value
	 * @param LFO - LFO to adjust frequency
	 * @param Bus - Bus of channel to modify the target value
	 * @param TargetValue - Value for channel to target
	 */
	UFUNCTION(BlueprintCallable, Category = "Audio", DisplayName = "Set Modulation LFO Frequency", meta = (WorldContext = "WorldContextObject", Keywords = "set lfo modulation modulator"))
	static void SetLFOFrequency(const UObject* WorldContextObject, USoundModulatorLFO* LFO, float Freq);
};
