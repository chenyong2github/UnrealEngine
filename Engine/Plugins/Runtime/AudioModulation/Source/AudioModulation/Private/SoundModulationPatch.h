// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "SoundModulationTransform.h"
#include "SoundModulationValue.h"
#include "SoundModulatorBus.h"
#include "SoundModulatorBusMix.h"

#include "SoundModulationPatch.generated.h"


// Forward Declarations
namespace AudioModulation
{
	struct FModulationInputProxy;
} // namespace AudioModulation


USTRUCT(BlueprintType)
struct FSoundModulationOutput
{
	GENERATED_USTRUCT_BODY()

	FSoundModulationOutput();

	/** Operator used when mixing input values */
	UPROPERTY(EditAnywhere, Category = Modulation, BlueprintReadWrite)
	ESoundModulatorOperator Operator;

	/** Final transform before passing to output */
	UPROPERTY(EditAnywhere, Category = Modulation, BlueprintReadWrite, meta = (ShowOnlyInnerProperties))
	FSoundModulationOutputTransform Transform;
};

USTRUCT(BlueprintType)
struct FSoundModulationInputBase
{
	GENERATED_USTRUCT_BODY()

	FSoundModulationInputBase();
	virtual ~FSoundModulationInputBase() = default;

	/** Get the modulated input value on parent patch initialization and hold that value for its lifetime */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Input, meta = (DisplayName = "Sample-And-Hold"))
	uint8 bSampleAndHold : 1;

	/** Transform to apply to the input prior to mix phase */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Input, meta = (ShowOnlyInnerProperties))
	FSoundModulationInputTransform Transform;

	virtual const USoundModulatorBusBase* GetBus() const PURE_VIRTUAL(FSoundModulationInputBase::GetBus, return nullptr; );
};

USTRUCT(BlueprintType)
struct FSoundVolumeModulationInput : public FSoundModulationInputBase
{
	GENERATED_USTRUCT_BODY()

	FSoundVolumeModulationInput();

	/** The input bus */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Input)
	USoundVolumeModulatorBus* Bus;

	virtual const USoundModulatorBusBase* GetBus() const { return Cast<USoundModulatorBusBase>(Bus); }
};

USTRUCT(BlueprintType)
struct FSoundPitchModulationInput : public FSoundModulationInputBase
{
	GENERATED_USTRUCT_BODY()

	FSoundPitchModulationInput();

	/** The input bus */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Input)
	USoundPitchModulatorBus* Bus;

	virtual const USoundModulatorBusBase* GetBus() const { return Cast<USoundModulatorBusBase>(Bus); }
};

USTRUCT(BlueprintType)
struct FSoundLPFModulationInput : public FSoundModulationInputBase
{
	GENERATED_USTRUCT_BODY()

	FSoundLPFModulationInput();

	/** The input bus */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Input)
	USoundLPFModulatorBus* Bus;

	virtual const USoundModulatorBusBase* GetBus() const { return Cast<USoundModulatorBusBase>(Bus); }
};

USTRUCT(BlueprintType)
struct FSoundHPFModulationInput : public FSoundModulationInputBase
{
	GENERATED_USTRUCT_BODY()

	FSoundHPFModulationInput();

	/** The input bus */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Input)
	USoundHPFModulatorBus* Bus;

	virtual const USoundModulatorBusBase* GetBus() const { return Cast<USoundModulatorBusBase>(Bus); }
};

USTRUCT(BlueprintType)
struct FSoundModulationPatchBase
{
	GENERATED_USTRUCT_BODY()

	FSoundModulationPatchBase();

	virtual ~FSoundModulationPatchBase() = default;

	/** Default value of patch (value used when all inputs are either not provided or not active) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Inputs)
	float DefaultInputValue;

	/** Final modulation parameters to apply */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Output, meta = (ShowOnlyInnerProperties))
	FSoundModulationOutput Output;

	virtual void GenerateProxies(TArray<AudioModulation::FModulationInputProxy>& InputProxies) const PURE_VIRTUAL(FSoundModulationPatchBase::GenerateProxies, );
};

USTRUCT(BlueprintType)
struct FSoundVolumeModulationPatch : public FSoundModulationPatchBase
{
	GENERATED_USTRUCT_BODY()

	/** Modulation inputs */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Inputs)
	TArray<FSoundVolumeModulationInput> Inputs;

	virtual void GenerateProxies(TArray<AudioModulation::FModulationInputProxy>& InputProxies) const
	{
		for (const FSoundVolumeModulationInput& Input : Inputs)
		{
			InputProxies.Emplace_GetRef(Input);
		}
	}
};

USTRUCT(BlueprintType)
struct FSoundPitchModulationPatch : public FSoundModulationPatchBase
{
	GENERATED_USTRUCT_BODY()

	/** Modulation inputs */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Inputs)
	TArray<FSoundPitchModulationInput> Inputs;

	virtual void GenerateProxies(TArray<AudioModulation::FModulationInputProxy>& InputProxies) const
	{
		for (const FSoundPitchModulationInput& Input : Inputs)
		{
			InputProxies.Emplace_GetRef(Input);
		}
	}
};

USTRUCT(BlueprintType)
struct FSoundLPFModulationPatch : public FSoundModulationPatchBase
{
	GENERATED_USTRUCT_BODY()

	/** Modulation inputs */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Inputs)
	TArray<FSoundLPFModulationInput> Inputs;

	virtual void GenerateProxies(TArray<AudioModulation::FModulationInputProxy>& InputProxies) const
	{
		for (const FSoundLPFModulationInput& Input : Inputs)
		{
			InputProxies.Emplace_GetRef(Input);
		}
	}
};

USTRUCT(BlueprintType)
struct FSoundHPFModulationPatch : public FSoundModulationPatchBase
{
	GENERATED_USTRUCT_BODY()

	/** Modulation inputs */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Inputs)
	TArray<FSoundHPFModulationInput> Inputs;

	virtual void GenerateProxies(TArray<AudioModulation::FModulationInputProxy>& InputProxies) const
	{
		for (const FSoundHPFModulationInput& Input : Inputs)
		{
			InputProxies.Emplace_GetRef(Input);
		}
	}
};

UCLASS(config = Engine, editinlinenew, BlueprintType, MinimalAPI)
class USoundModulationSettings : public USoundModulationPluginSourceSettingsBase
{
	GENERATED_UCLASS_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Volume)
	FSoundVolumeModulationPatch Volume;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Pitch)
	FSoundPitchModulationPatch Pitch;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Lowpass)
	FSoundLPFModulationPatch Lowpass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Highpass)
	FSoundHPFModulationPatch Highpass;

	// Mixes that will applied and removed when sounds utilizing settings
	// play and stop respectively. If mix has already been applied manually,
	// mix will be removed once all sound settings referencing mix stop. Manual
	// mix activation is ignored if already activated by means of modulation settings.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Mixes)
	TArray<USoundModulatorBusMix*> Mixes;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent) override;
#endif
};


namespace AudioModulation
{
	/** Modulation input instance */
	struct FModulationInputProxy
	{
		FModulationInputProxy();
		FModulationInputProxy(const FSoundModulationInputBase& Patch);

		BusId BusId;
		FSoundModulationInputTransform Transform;
		uint8 bSampleAndHold : 1;
	};

	/** Patch applied as the final stage of a modulation chain prior to output on the sound level (Always active, never removed) */
	struct FModulationOutputProxy
	{
		FModulationOutputProxy();
		FModulationOutputProxy(const FSoundModulationOutput& Patch);

		/** Whether patch has been initialized or not */
		uint8 bInitialized : 1;

		/** Operator used to calculate the output proxy value */
		ESoundModulatorOperator Operator;

		/** Cached value of sample-and-hold input values */
		float SampleAndHoldValue;

		/** Final transform before passing to output */
		FSoundModulationOutputTransform Transform;
	};

	struct FModulationPatchProxy
	{
		FModulationPatchProxy();
		FModulationPatchProxy(const FSoundModulationPatchBase& Patch);

		/** Default value of patch (value used when inputs are either not provided or not active)*/
		float DefaultInputValue;

		/** Optional modulation inputs */
		TArray<FModulationInputProxy> InputProxies;

		/** Final output modulation post input combination */
		FModulationOutputProxy OutputProxy;
	};

	struct FModulationSettingsProxy
	{
		FModulationSettingsProxy();
		FModulationSettingsProxy(const USoundModulationSettings& Settings);

		FModulationPatchProxy Volume;
		FModulationPatchProxy Pitch;
		FModulationPatchProxy Lowpass;
		FModulationPatchProxy Highpass;

		TArray<BusMixId> Mixes;

#if WITH_EDITOR
		uint32 ObjectID;
#endif // WITH_EDITOR
	};
} // namespace AudioModulation
