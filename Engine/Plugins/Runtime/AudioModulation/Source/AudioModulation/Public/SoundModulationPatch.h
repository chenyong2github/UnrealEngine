// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "SoundModulationTransform.h"
#include "SoundControlBus.h"
#include "SoundControlBusMix.h"

#include "SoundModulationPatch.generated.h"


// Forward Declarations
namespace AudioModulation
{
	struct FModulationInputProxy;
} // namespace AudioModulation


USTRUCT(BlueprintType)
struct FSoundModulationOutputBase
{
	GENERATED_USTRUCT_BODY()

	virtual ~FSoundModulationOutputBase() = default;

	/** Final transform before passing to output */
	UPROPERTY(EditAnywhere, Category = Modulation, BlueprintReadWrite, meta = (ShowOnlyInnerProperties))
	FSoundModulationOutputTransform Transform;

	virtual ESoundModulatorOperator GetOperator() const PURE_VIRTUAL(FSoundModulationOutputBase::GetOperator, return ESoundModulatorOperator::Multiply;);
};

USTRUCT(BlueprintType)
struct FSoundModulationOutputFixedOperator : public FSoundModulationOutputBase
{
	GENERATED_USTRUCT_BODY()

	FSoundModulationOutputFixedOperator();

protected:
	/** Operator used when mixing input values */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Modulation)
	ESoundModulatorOperator Operator;

public:
	ESoundModulatorOperator GetOperator() const override { return Operator; }
	void SetOperator(ESoundModulatorOperator InOperator) { Operator = InOperator; }
};

USTRUCT(BlueprintType)
struct FSoundModulationOutput : public FSoundModulationOutputBase
{
	GENERATED_USTRUCT_BODY()

	FSoundModulationOutput();

protected:
	/** Operator used when mixing input values */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Modulation)
	ESoundModulatorOperator Operator;

public:
	ESoundModulatorOperator GetOperator() const override { return Operator; }
	void SetOperator(ESoundModulatorOperator InOperator) { Operator = InOperator; }
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
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Input, meta = (DisplayName = "Advanced"))
	FSoundModulationInputTransform Transform;

	virtual const USoundControlBusBase* GetBus() const PURE_VIRTUAL(FSoundModulationInputBase::GetBus, return nullptr; );
};

USTRUCT(BlueprintType)
struct FSoundVolumeModulationInput : public FSoundModulationInputBase
{
	GENERATED_USTRUCT_BODY()

	FSoundVolumeModulationInput();

	/** The input bus */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Input)
	USoundVolumeControlBus* Bus;

	virtual const USoundControlBusBase* GetBus() const { return Cast<USoundControlBusBase>(Bus); }
};

USTRUCT(BlueprintType)
struct FSoundPitchModulationInput : public FSoundModulationInputBase
{
	GENERATED_USTRUCT_BODY()

	FSoundPitchModulationInput();

	/** The input bus */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Input)
	USoundPitchControlBus* Bus;

	virtual const USoundControlBusBase* GetBus() const { return Cast<USoundControlBusBase>(Bus); }
};

USTRUCT(BlueprintType)
struct FSoundLPFModulationInput : public FSoundModulationInputBase
{
	GENERATED_USTRUCT_BODY()

	FSoundLPFModulationInput();

	/** The input bus */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Input)
	USoundLPFControlBus* Bus;

	virtual const USoundControlBusBase* GetBus() const { return Cast<USoundControlBusBase>(Bus); }
};

USTRUCT(BlueprintType)
struct FSoundHPFModulationInput : public FSoundModulationInputBase
{
	GENERATED_USTRUCT_BODY()

	FSoundHPFModulationInput();

	/** The input bus */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Input)
	USoundHPFControlBus* Bus;

	virtual const USoundControlBusBase* GetBus() const { return Cast<USoundControlBusBase>(Bus); }
};

USTRUCT(BlueprintType)
struct FSoundControlModulationInput : public FSoundModulationInputBase
{
	GENERATED_USTRUCT_BODY()

	FSoundControlModulationInput();

	/** The input bus */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Input)
	USoundControlBus* Bus;

	virtual const USoundControlBusBase* GetBus() const { return Cast<USoundControlBusBase>(Bus); }
};

USTRUCT(BlueprintType)
struct FSoundModulationPatchBase
{
	GENERATED_USTRUCT_BODY()

	FSoundModulationPatchBase();

	virtual ~FSoundModulationPatchBase() = default;

	/** Default value of patch, included in mix calculation regardless of number of active buses referenced. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Inputs, meta = (UIMin = "0", UIMax = "1"))
	float DefaultInputValue;

	/** Whether or not patch is bypassed */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Inputs, meta = (UIMin = "0", UIMax = "1"))
	uint8 bBypass : 1;

	virtual TArray<const FSoundModulationInputBase*> GetInputs() const PURE_VIRTUAL(FSoundModulationPatchBase::GetInputs(), return TArray<const FSoundModulationInputBase*>(););
	virtual FSoundModulationOutputBase* GetOutput() PURE_VIRTUAL(FSoundModulationPatchBase::GetOutput(), return nullptr;);
	virtual const FSoundModulationOutputBase* GetOutput() const PURE_VIRTUAL(FSoundModulationPatchBase::GetOutput(), return nullptr;);

#if WITH_EDITOR
	virtual void Clamp();
#endif // WITH_EDITOR
};

USTRUCT(BlueprintType)
struct FSoundVolumeModulationPatch : public FSoundModulationPatchBase
{
	GENERATED_USTRUCT_BODY()

	/** Modulation inputs */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Inputs)
	TArray<FSoundVolumeModulationInput> Inputs;

	/** Final modulation parameters to apply */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Output, meta = (ShowOnlyInnerProperties))
	FSoundModulationOutputFixedOperator Output;

	virtual TArray<const FSoundModulationInputBase*> GetInputs() const override;
	virtual FSoundModulationOutputBase* GetOutput() override { return static_cast<FSoundModulationOutputBase*>(&Output); }
	virtual const FSoundModulationOutputBase* GetOutput() const override { return static_cast<const FSoundModulationOutputBase*>(&Output); }

#if WITH_EDITOR
	virtual void Clamp() override;
#endif // WITH_EDITOR
};

USTRUCT(BlueprintType)
struct FSoundPitchModulationPatch : public FSoundModulationPatchBase
{
	GENERATED_USTRUCT_BODY()

	/** Modulation inputs */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Inputs)
	TArray<FSoundPitchModulationInput> Inputs;

	/** Final modulation parameters to apply */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Output, meta = (ShowOnlyInnerProperties))
	FSoundModulationOutputFixedOperator Output;

	virtual TArray<const FSoundModulationInputBase*> GetInputs() const override;
	virtual FSoundModulationOutputBase* GetOutput() override { return static_cast<FSoundModulationOutputBase*>(&Output); }
	virtual const FSoundModulationOutputBase* GetOutput() const override { return static_cast<const FSoundModulationOutputBase*>(&Output); }

#if WITH_EDITOR
	virtual void Clamp() override;
#endif // WITH_EDITOR
};

USTRUCT(BlueprintType)
struct FSoundLPFModulationPatch : public FSoundModulationPatchBase
{
	GENERATED_USTRUCT_BODY()

	/** Modulation inputs */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Inputs)
	TArray<FSoundLPFModulationInput> Inputs;

	/** Final modulation parameters to apply */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Output, meta = (ShowOnlyInnerProperties))
	FSoundModulationOutputFixedOperator Output;

	virtual TArray<const FSoundModulationInputBase*> GetInputs() const override;
	virtual FSoundModulationOutputBase* GetOutput() override { return static_cast<FSoundModulationOutputBase*>(&Output); }
	virtual const FSoundModulationOutputBase* GetOutput() const override { return static_cast<const FSoundModulationOutputBase*>(&Output); }

#if WITH_EDITOR
	virtual void Clamp() override;
#endif // WITH_EDITOR
};

USTRUCT(BlueprintType)
struct FSoundHPFModulationPatch : public FSoundModulationPatchBase
{
	GENERATED_USTRUCT_BODY()

	/** Modulation inputs */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Inputs)
	TArray<FSoundHPFModulationInput> Inputs;

	/** Final modulation parameters to apply */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Output, meta = (ShowOnlyInnerProperties))
	FSoundModulationOutputFixedOperator Output;

	virtual TArray<const FSoundModulationInputBase*> GetInputs() const override;
	virtual FSoundModulationOutputBase* GetOutput() override { return static_cast<FSoundModulationOutputBase*>(&Output); }
	virtual const FSoundModulationOutputBase* GetOutput() const override { return static_cast<const FSoundModulationOutputBase*>(&Output); }

#if WITH_EDITOR
	virtual void Clamp() override;
#endif // WITH_EDITOR
};

USTRUCT(BlueprintType)
struct FSoundControlModulationPatch : public FSoundModulationPatchBase
{
	GENERATED_USTRUCT_BODY()

	/** Name of modulation control for sounds referencing this ModulationSettings asset. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Inputs)
	FName Control;

	/** Modulation inputs */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Inputs)
	TArray<FSoundControlModulationInput> Inputs;

	/** Final modulation parameters to apply */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Output, meta = (ShowOnlyInnerProperties))
	FSoundModulationOutput Output;

	virtual TArray<const FSoundModulationInputBase*> GetInputs() const override;
	virtual FSoundModulationOutputBase* GetOutput() override { return static_cast<FSoundModulationOutputBase*>(&Output); }
	virtual const FSoundModulationOutputBase* GetOutput() const override { return static_cast<const FSoundModulationOutputBase*>(&Output); }
};

UCLASS(config = Engine, editinlinenew, BlueprintType, MinimalAPI, autoExpandCategories = (Volume, Pitch, Highpass, Lowpass))
class USoundModulationSettings : public USoundModulationPluginSourceSettingsBase
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

	// Array of named generic controls for use with modulateable parameters on source effects
	// Properties hidden as Generic Control Modulation is still in development
	UPROPERTY()
	TArray<FSoundControlModulationPatch> Controls;

	// Mixes that will applied and removed when sounds utilizing settings
	// play and stop respectively. If mix has already been applied manually,
	// mix will be removed once all sound settings referencing mix stop. Manual
	// mix activation is ignored if already activated by means of modulation settings.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Mixes)
	TArray<USoundControlBusMix*> Mixes;

#if WITH_EDITOR
	AUDIOMODULATION_API void OnPostEditChange(UWorld* World);

	virtual void PostDuplicate(EDuplicateMode::Type DuplicateMode) override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent) override;
#endif // WITH_EDITOR
};
