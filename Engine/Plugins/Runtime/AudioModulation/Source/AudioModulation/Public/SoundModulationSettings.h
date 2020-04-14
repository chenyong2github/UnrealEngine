// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "IAudioModulation.h"
#include "SoundModulationPatch.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"

#include "SoundModulationSettings.generated.h"


UCLASS(config = Engine, editinlinenew, BlueprintType, autoExpandCategories = (Volume, Pitch, Highpass, Lowpass))
class AUDIOMODULATION_API USoundModulationSettings : public USoundModulationPluginSourceSettingsBase
{
	GENERATED_UCLASS_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Volume)
	FSoundVolumeModulationPatch Volume;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Pitch)
	FSoundPitchModulationPatch Pitch;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Highpass)
	FSoundHPFModulationPatch Highpass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Lowpass)
	FSoundLPFModulationPatch Lowpass;

#if WITH_EDITOR
	void OnPostEditChange(UWorld* World);

	virtual void PostDuplicate(EDuplicateMode::Type DuplicateMode) override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent) override;
#endif // WITH_EDITOR
};
