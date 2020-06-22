// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "AudioDefines.h"
#include "AudioDeviceManager.h"
#include "AudioDevice.h"
#include "CoreMinimal.h"
#include "IAudioModulation.h"
#include "SoundModulationParameter.h"
#include "SoundModulatorLFO.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"

#include "SoundControlBus.generated.h"


// Forward Declarations
class USoundModulatorBase;

struct FPropertyChangedEvent;



namespace Audio
{
	// Returns the semitone amount a frequency multiplier corresponds to
	static FORCEINLINE float GetSemitones(const float InFreqMultiplier)
	{
		if (InFreqMultiplier == 1.0f)
		{
			return 0.0f;

		}

		return 12.0f * FMath::Log2(InFreqMultiplier);
	}
}


UCLASS(BlueprintType, hidecategories = Object, abstract, MinimalAPI)
class USoundControlBusBase : public USoundModulatorBase
{
	GENERATED_UCLASS_BODY()

public:
	/** If true, bypasses control bus from being modulated by parameters, patches, or mixed (control bus remains active and computed). */
	UPROPERTY(EditAnywhere, Category = General, BlueprintReadWrite)
		bool bBypass;

#if WITH_EDITORONLY_DATA
	/** If true, Address field is used in place of object name for address used when applying mix changes using filtering. */
	UPROPERTY(EditAnywhere, Category = Mix, BlueprintReadWrite)
		bool bOverrideAddress;
#endif // WITH_EDITORONLY_DATA

	/** Address to use when applying mix changes. */
	UPROPERTY(EditAnywhere, Category = Mix, BlueprintReadWrite, meta = (EditCondition = "bOverrideAddress"))
		FString Address;

	UPROPERTY(EditAnywhere, Category = Modulation, BlueprintReadWrite)
		TArray<USoundBusModulatorBase*> Modulators;

#if WITH_EDITOR
	virtual void PostDuplicate(EDuplicateMode::Type DuplicateMode) override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& InPropertyChangedEvent) override;
	virtual void PostInitProperties() override;
	virtual void PostRename(UObject* OldOuter, const FName OldName) override;
#endif // WITH_EDITOR

	virtual void BeginDestroy() override;
	virtual const Audio::FModulationMixFunction& GetMixFunction() const;
	virtual float GetDefaultValue() const { return 1.0f; }
	virtual float GetMin() const { return 0.0f; }
	virtual float GetMax() const { return 1.0f; }
};

UCLASS(BlueprintType, hidecategories = Object, editinlinenew, MinimalAPI)
class USoundVolumeControlBus : public USoundControlBusBase
{
	GENERATED_UCLASS_BODY()

public:
	/** Default value of modulator when no mix is applied. Value that is also returned to when mix is released. */
	UPROPERTY(EditAnywhere, Category = General, BlueprintReadWrite)
	float DefaultValue;

	/** Minimum value the bus can achieve (applied post mix phase, pre patch output) */
	UPROPERTY(EditAnywhere, Category = General, BlueprintReadWrite, meta = (DisplayAfter = "DefaultValue", UIMin = "0.0", UIMax = "1.0", ClampMin = "0.0"))
	float Min = 0.0f;

	/** Maximum value the bus can achieve (applied post mix phase, pre patch output) */
	UPROPERTY(EditAnywhere, Category = General, BlueprintReadWrite, meta = (DisplayAfter = "Min", UIMin = "0.0", UIMax = "1.0", ClampMin = "0.0"))
	float Max = 1.0f;

	virtual float GetDefaultValue() const { return DefaultValue; }
	virtual float GetMin() const override { return FMath::Clamp(Min, 0.0f, MAX_VOLUME); }
	virtual float GetMax() const override { return FMath::Clamp(Max, GetMin(), MAX_VOLUME); }

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& InPropertyChangedEvent) override;
#endif // WITH_EDITOR
};

UCLASS(BlueprintType, hidecategories = Object, editinlinenew, MinimalAPI)
class USoundPitchControlBus : public USoundControlBusBase
{
	GENERATED_UCLASS_BODY()

public:
	/** Default value of modulator when no mix is applied. Value that is also returned to when mix is released. */
	UPROPERTY(EditAnywhere, Category = General, BlueprintReadWrite)
	float DefaultValue;

	/** Minimum value the bus can achieve (applied post mix phase, pre patch output) */
	UPROPERTY(EditAnywhere, Category = General, BlueprintReadWrite, meta = (DisplayAfter = "DefaultValue", UIMin = "0.4", UIMax = "2.0", ClampMin = "0.0"))
	float Min = MIN_PITCH;

	/** Maximum value the bus can achieve (applied post mix phase, pre patch output) */
	UPROPERTY(EditAnywhere, Category = General, BlueprintReadWrite, meta = (DisplayAfter = "Min", UIMin = "0.4", UIMax = "2.0", ClampMin = "0.0"))
	float Max = MAX_PITCH;

	virtual float GetDefaultValue() const { return DefaultValue; }
	virtual float GetMin() const override { return FMath::Clamp(Min, MIN_PITCH, MAX_PITCH); }
	virtual float GetMax() const override { return FMath::Clamp(Max, GetMin(), MAX_PITCH); }

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& InPropertyChangedEvent) override;
#endif // WITH_EDITOR
};

UCLASS(BlueprintType, hidecategories = Object, editinlinenew, MinimalAPI)
class USoundHPFControlBus : public USoundControlBusBase
{
	GENERATED_UCLASS_BODY()

public:
	/** Default value of modulator when no mix is applied. Value that is also returned to when mix is released. */
	UPROPERTY(EditAnywhere, Category = General, BlueprintReadWrite)
	float DefaultValue;

	/** Minimum value the bus can achieve (applied post mix phase, pre patch output) */
	UPROPERTY(EditAnywhere, Category = General, BlueprintReadWrite, meta = (DisplayAfter = "DefaultValue", UIMin = "20.0", UIMax = "20000.0", ClampMin = "0"))
	float Min = MIN_FILTER_FREQUENCY;

	/** Maximum value the bus can achieve (applied post mix phase, pre patch output) */
	UPROPERTY(EditAnywhere, Category = General, BlueprintReadWrite, meta = (DisplayAfter = "Min", UIMin = "20.0", UIMax = "20000.0", ClampMin = "0"))
	float Max = MAX_FILTER_FREQUENCY;

	virtual float GetDefaultValue() const { return DefaultValue; }
	virtual float GetMin() const override { return FMath::Clamp(Min, MIN_FILTER_FREQUENCY, MAX_FILTER_FREQUENCY); }
	virtual float GetMax() const override { return FMath::Clamp(Max, GetMin(), MAX_FILTER_FREQUENCY); }

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& InPropertyChangedEvent) override;
#endif // WITH_EDITOR
};

UCLASS(BlueprintType, hidecategories = Object, editinlinenew, MinimalAPI)
class USoundLPFControlBus : public USoundControlBusBase
{
	GENERATED_UCLASS_BODY()

public:
	/** Default value of modulator when no mix is applied. Value that is also returned to when mix is released. */
	UPROPERTY(EditAnywhere, Category = General, BlueprintReadWrite)
	float DefaultValue;

	/** Minimum value the bus can achieve (applied post mix phase, pre patch output) */
	UPROPERTY(EditAnywhere, Category = General, BlueprintReadWrite, meta = (DisplayAfter = "DefaultValue", UIMin = "20.0", UIMax = "20000.0", ClampMin = "0"))
	float Min = MIN_FILTER_FREQUENCY;

	/** Maximum value the bus can achieve (applied post mix phase, pre patch output) */
	UPROPERTY(EditAnywhere, Category = General, BlueprintReadWrite, meta = (DisplayAfter = "Min", UIMin = "20.0", UIMax = "20000.0", ClampMin = "0"))
	float Max = MAX_FILTER_FREQUENCY;

	virtual float GetDefaultValue() const { return DefaultValue; }
	virtual float GetMin() const override { return FMath::Clamp(Min, MIN_FILTER_FREQUENCY, MAX_FILTER_FREQUENCY); }
	virtual float GetMax() const override { return FMath::Clamp(Max, GetMin(), MAX_FILTER_FREQUENCY); }

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& InPropertyChangedEvent) override;
#endif // WITH_EDITOR
};

UCLASS(BlueprintType, hidecategories = Object, editinlinenew, MinimalAPI)
class USoundControlBus : public USoundControlBusBase
{
	GENERATED_UCLASS_BODY()

public:
	UPROPERTY(EditAnywhere, Category = General, BlueprintReadOnly)
	USoundModulationParameter* Parameter;

	virtual float GetDefaultValue() const { return Parameter ? Parameter->ConvertLinearToUnit(Parameter->Settings.ValueLinear) : 1.0f; }
	virtual float GetMin() const override { return 0.0f; }
	virtual float GetMax() const override { return 1.0f; }

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& InPropertyChangedEvent) override;
#endif // WITH_EDITOR
};
