// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"

#include "SoundModulatorLFO.h"
#include "SoundModulationValue.h"

#include "SoundModulatorBus.generated.h"


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
class USoundModulatorBusBase : public UObject
{
	GENERATED_UCLASS_BODY()

public:
	/** Automatically activates/deactivates bus when sounds referencing asset are playing. */
	UPROPERTY(EditAnywhere, Category = Modulation, BlueprintReadOnly)
	uint8 bAutoActivate : 1;

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
	TArray<USoundModulatorBase*> Modulators;

	virtual void BeginDestroy() override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif // WITH_EDITOR

	virtual ESoundModulatorOperator GetOperator() const { return ESoundModulatorOperator::Multiply; }
};

UCLASS(BlueprintType, hidecategories = Object, editinlinenew, MinimalAPI)
class USoundVolumeModulatorBus : public USoundModulatorBusBase
{
	GENERATED_UCLASS_BODY()
};

UCLASS(BlueprintType, hidecategories = Object, editinlinenew, MinimalAPI)
class USoundPitchModulatorBus : public USoundModulatorBusBase
{
	GENERATED_UCLASS_BODY()

	virtual ESoundModulatorOperator GetOperator() const { return ESoundModulatorOperator::Multiply; }
};

UCLASS(BlueprintType, hidecategories = Object, editinlinenew, MinimalAPI)
class USoundHPFModulatorBus : public USoundModulatorBusBase
{
	GENERATED_UCLASS_BODY()

	virtual ESoundModulatorOperator GetOperator() const { return ESoundModulatorOperator::Max; }
};

UCLASS(BlueprintType, hidecategories = Object, editinlinenew, MinimalAPI)
class USoundLPFModulatorBus : public USoundModulatorBusBase
{
	GENERATED_UCLASS_BODY()

	virtual ESoundModulatorOperator GetOperator() const { return ESoundModulatorOperator::Min; }
};

namespace AudioModulation
{
	class FModulatorBusProxy
	{
	public:
		FModulatorBusProxy();
		FModulatorBusProxy(const USoundModulatorBusBase& Bus);

		bool GetAutoActivate() const;
		AudioModulation::BusId GetBusId() const;
		float GetDefaultValue() const;
		float GetLFOValue() const;
		float GetMixValue() const;
		FVector2D GetRange() const;
		float GetValue() const;
		void MixIn(const float InValue);
		void MixLFO(LFOProxyMap& LFOMap);
		void Reset();

		void SetDefaultValue(const float Value);
		void SetRange(const FVector2D& Range);

		int32 IncRefSound();
		int32 DecRefSound();

#if !UE_BUILD_SHIPPING
		const FString& GetName() const;
#endif // !UE_BUILD_SHIPPING

	private:
		float Mix(float ValueA) const;
		float Mix(float ValueA, float ValueB) const;

		BusId BusId;

#if !UE_BUILD_SHIPPING
		FString Name;
#endif // !UE_BUILD_SHIPPING

		float DefaultValue;

		// Cached values
		float LFOValue;
		float MixValue;

		TArray<LFOId> LFOIds;
		ESoundModulatorOperator Operator;
		FVector2D Range;

		uint8 bAutoActivate : 1;
		int32 SoundRefCount;
	};
	using BusProxyMap = TMap<BusId, FModulatorBusProxy>;
} // namespace AudioModulation
