// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "AudioDefines.h"
#include "AudioDeviceManager.h"
#include "AudioDevice.h"
#include "CoreMinimal.h"
#include "IAudioModulation.h"
#include "SoundModulationParameter.h"
#include "SoundModulationGeneratorLFO.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"

#include "SoundControlBus.generated.h"


// Forward Declarations
class USoundModulatorBase;

struct FPropertyChangedEvent;


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

	UPROPERTY(EditAnywhere, Category = Generators, BlueprintReadWrite, meta = (DisplayName = "Generators"))
	TArray<USoundModulationGenerator*> Modulators;

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
class USoundControlBus : public USoundControlBusBase
{
	GENERATED_UCLASS_BODY()

public:
	UPROPERTY(EditAnywhere, Category = General, BlueprintReadOnly)
	USoundModulationParameter* Parameter;

	virtual float GetDefaultValue() const { return Parameter ? Parameter->ConvertLinearToUnit(Parameter->Settings.ValueLinear) : 1.0f; }
	virtual float GetMin() const override { return 0.0f; }
	virtual float GetMax() const override { return 1.0f; }

	virtual FName GetOutputParameterName() const override
	{
		if (Parameter)
		{
			return Parameter->GetFName();
		}
		return Super::GetOutputParameterName();
	}

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& InPropertyChangedEvent) override;
#endif // WITH_EDITOR
};
