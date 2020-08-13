// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "IAudioModulation.h"
#include "SoundModulationParameter.h"
#include "SoundModulationTransform.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"

#include "SoundModulationPatch.generated.h"

// Forward Declarations
class USoundControlBus;


USTRUCT(BlueprintType)
struct AUDIOMODULATION_API FSoundModulationOutputBase
{
	GENERATED_USTRUCT_BODY()

	/** Final transform before passing to output */
	UPROPERTY(EditAnywhere, Category = Modulation, BlueprintReadWrite, meta = (ShowOnlyInnerProperties))
	FSoundModulationOutputTransform Transform;

	virtual ~FSoundModulationOutputBase() = default;
};

USTRUCT(BlueprintType)
struct AUDIOMODULATION_API FSoundModulationOutputFixedOperator : public FSoundModulationOutputBase
{
	GENERATED_USTRUCT_BODY()
};

USTRUCT(BlueprintType)
struct AUDIOMODULATION_API FSoundModulationInputBase
{
	GENERATED_USTRUCT_BODY()

	FSoundModulationInputBase();
	virtual ~FSoundModulationInputBase() = default;

	/** Get the modulated input value on parent patch initialization and hold that value for its lifetime */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Input, meta = (DisplayName = "Sample-And-Hold"))
	uint8 bSampleAndHold : 1;

	/** Transform to apply to the input prior to mix phase */
	UPROPERTY()
	FSoundModulationInputTransform Transform;

	virtual const USoundControlBus* GetBus() const PURE_VIRTUAL(FSoundModulationInputBase::GetBus, return nullptr; );

	const USoundControlBus& GetBusChecked() const
	{
		const USoundControlBus* Bus = GetBus();
		check(Bus);
		return *GetBus();
	}
};

USTRUCT(BlueprintType)
struct AUDIOMODULATION_API FSoundControlModulationInput : public FSoundModulationInputBase
{
	GENERATED_USTRUCT_BODY()

	/** The input bus */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Input)
	USoundControlBus* Bus = nullptr;

	virtual const USoundControlBus* GetBus() const override;
};

USTRUCT(BlueprintType)
struct AUDIOMODULATION_API FSoundModulationPatchBase
{
	GENERATED_USTRUCT_BODY()

	FSoundModulationPatchBase();

	virtual ~FSoundModulationPatchBase() = default;

	/** Whether or not patch is bypassed */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Inputs)
	uint8 bBypass : 1;

	virtual float GetDefaultInputValue() const { return 1.0f; }
	virtual const Audio::FModulationMixFunction& GetMixFunction() const { return Audio::FModulationParameter::GetDefaultMixFunction(); }
	virtual TArray<const FSoundModulationInputBase*> GetInputs() const PURE_VIRTUAL(FSoundModulationPatchBase::GetInputs(), return TArray<const FSoundModulationInputBase*>(););
	virtual FSoundModulationOutputBase* GetOutput() PURE_VIRTUAL(FSoundModulationPatchBase::GetOutput(), return nullptr;);
	virtual const FSoundModulationOutputBase* GetOutput() const PURE_VIRTUAL(FSoundModulationPatchBase::GetOutput(), return nullptr;);

	FSoundModulationOutputBase& GetOutputChecked()
	{
		FSoundModulationOutputBase* Output = GetOutput();
		check(Output);

		return *Output;
	}

	const FSoundModulationOutputBase& GetOutputChecked() const
	{
		const FSoundModulationOutputBase* Output = GetOutput();
		check(Output);
		
		return *Output;
	}

#if WITH_EDITOR
	virtual void Clamp();
#endif // WITH_EDITOR
};

USTRUCT(BlueprintType)
struct AUDIOMODULATION_API FSoundControlModulationPatch
{
	GENERATED_USTRUCT_BODY()

	/** Whether or not patch is bypassed (patch is still active, but always returns output parameter default value when modulated) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Inputs)
	bool bBypass = true;

	/** Input parameter of patch */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Inputs, meta = (DisplayName = "Input Parameter"))
	USoundModulationParameter* InputParameter = nullptr;

	/** Modulation inputs */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Inputs)
	TArray<FSoundControlModulationInput> Inputs;

	UPROPERTY(EditAnywhere, Category = Output)
	USoundModulationParameter* OutputParameter = nullptr;

	/** Final transform before passing to output */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Output)
	FSoundModulationOutputTransform Transform;
};

UCLASS(config = Engine, editinlinenew, BlueprintType)
class AUDIOMODULATION_API USoundModulationPatch : public USoundModulatorBase
{
	GENERATED_UCLASS_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Modulation, meta = (ShowOnlyInnerProperties))
	FSoundControlModulationPatch PatchSettings;

	virtual FName GetOutputParameterName() const override
	{
		if (PatchSettings.OutputParameter)
		{
			return PatchSettings.OutputParameter->GetFName();
		}

		return Super::GetOutputParameterName();
	}

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& InPropertyChangedEvent) override;
	virtual void PostEditChangeChainProperty(FPropertyChangedChainEvent& InPropertyChangedEvent) override;
#endif // WITH_EDITOR
};
