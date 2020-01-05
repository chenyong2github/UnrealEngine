// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"

#include "SoundModulatorLFO.h"
#include "SoundModulationValue.h"

#include "SoundControlBus.generated.h"


UENUM()
enum class ESoundModulatorOperator : uint8
{
	/** Multiply all mix values together */
	Multiply,

	/** Take the lowest mix value */
	Min,

	/** Take the highest mix value */
	Max,

	Count UMETA(Hidden),
};


UCLASS(BlueprintType, hidecategories = Object, abstract, MinimalAPI)
class USoundControlBusBase : public USoundModulatorBase
{
	GENERATED_UCLASS_BODY()

public:
	/** If true, bypasses control bus from being mixed. */
	UPROPERTY(EditAnywhere, Category = General, BlueprintReadWrite)
	bool bBypass;

	/** Default value of modulator when no mix is applied. Value that is also returned to when mix is released. */
	UPROPERTY(EditAnywhere, Category = General, BlueprintReadWrite, meta = (UIMin = "0", UIMax = "1"))
	float DefaultValue;

	/** Minimum value the bus can achieve (applied post mix phase, pre patch output) */
	UPROPERTY(EditAnywhere, Category = General, BlueprintReadWrite, meta = (UIMin = "0", UIMax = "1"))
	float Min;

	/** Maximum value the bus can achieve (applied post mix phase, pre patch output) */
	UPROPERTY(EditAnywhere, Category = General, BlueprintReadWrite, meta = (UIMin = "0", UIMax = "1"))
	float Max;

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
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PostInitProperties() override;
	virtual void PostRename(UObject* OldOuter, const FName OldName) override;
#endif // WITH_EDITOR

	virtual void BeginDestroy() override;
	virtual ESoundModulatorOperator GetOperator() const { return ESoundModulatorOperator::Multiply; }
};

UCLASS(BlueprintType, hidecategories = Object, editinlinenew, MinimalAPI)
class USoundVolumeControlBus : public USoundControlBusBase
{
	GENERATED_UCLASS_BODY()
};

UCLASS(BlueprintType, hidecategories = Object, editinlinenew, MinimalAPI)
class USoundPitchControlBus : public USoundControlBusBase
{
	GENERATED_UCLASS_BODY()

	virtual ESoundModulatorOperator GetOperator() const { return ESoundModulatorOperator::Multiply; }
};

UCLASS(BlueprintType, hidecategories = Object, editinlinenew, MinimalAPI)
class USoundHPFControlBus : public USoundControlBusBase
{
	GENERATED_UCLASS_BODY()

	virtual ESoundModulatorOperator GetOperator() const { return ESoundModulatorOperator::Max; }
};

UCLASS(BlueprintType, hidecategories = Object, editinlinenew, MinimalAPI)
class USoundLPFControlBus : public USoundControlBusBase
{
	GENERATED_UCLASS_BODY()

	virtual ESoundModulatorOperator GetOperator() const { return ESoundModulatorOperator::Min; }
};

UCLASS(BlueprintType, hidecategories = Object, editinlinenew, MinimalAPI)
class USoundControlBus : public USoundControlBusBase
{
	GENERATED_UCLASS_BODY()

	virtual ESoundModulatorOperator GetOperator() const { return ESoundModulatorOperator::Multiply; }
};
