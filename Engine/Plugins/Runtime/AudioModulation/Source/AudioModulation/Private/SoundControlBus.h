// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
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
	/** Default value of modulator (when no mix is applied). */
	UPROPERTY(EditAnywhere, Category = Modulation, BlueprintReadWrite, meta = (UIMin = "0", UIMax = "1"))
	float DefaultValue;

	/** Minimum value the bus can achieve (applied post mix phase, pre patch output) */
	UPROPERTY(EditAnywhere, Category = Modulation, BlueprintReadWrite, meta = (UIMin = "0", UIMax = "1"))
	float Min;

	/** Maximum value the bus can achieve (applied post mix phase, pre patch output) */
	UPROPERTY(EditAnywhere, Category = Modulation, BlueprintReadWrite, meta = (UIMin = "0", UIMax = "1"))
	float Max;

	UPROPERTY(EditAnywhere, Category = Modulation, BlueprintReadWrite)
	TArray<USoundBusModulatorBase*> Modulators;

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

namespace AudioModulation
{
	class FControlBusProxy : public TModulatorProxyRefBase<FBusId>
	{
	public:
		FControlBusProxy();
		FControlBusProxy(const USoundControlBusBase& Bus);

		void OnUpdateProxy(const USoundModulatorBase& InModulatorArchetype) override;

		float GetDefaultValue() const;
		const TArray<FLFOId>& GetLFOIds() const;
		float GetLFOValue() const;
		float GetMixValue() const;
		FVector2D GetRange() const;
		float GetValue() const;
		void MixIn(const float InValue);
		void MixLFO(LFOProxyMap& LFOMap);
		void Reset();

	private:
		float Mix(float ValueA) const;
		float Mix(float ValueA, float ValueB) const;

		float DefaultValue;

		// Cached values
		float LFOValue;
		float MixValue;

		TArray<FLFOId> LFOIds;
		ESoundModulatorOperator Operator;
		FVector2D Range;
	};
	using BusProxyMap = TMap<FBusId, FControlBusProxy>;
} // namespace AudioModulation
