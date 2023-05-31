// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PCGContext.h"
#include "PCGElement.h"
#include "PCGPin.h"
#include "PCGSettings.h"
#include "Metadata/PCGAttributePropertySelector.h"

#include "PCGAttributeNoise.generated.h"

class IPCGAttributeAccessor;
class IPCGAttributeAccessorKeys;

UENUM()
enum class EPCGAttributeNoiseMode : uint8
{
	Set,
	Minimum,
	Maximum,
	Add,
	Multiply
};

/**
* Apply some noise to an attribute/property. You can select the mode you want and a noise range.
* Support all numerical types and vectors/rotators.
*/
UCLASS(BlueprintType, ClassGroup = (Procedural))
class PCG_API UPCGAttributeNoiseSettings : public UPCGSettings
{
	GENERATED_BODY()

public:
	UPCGAttributeNoiseSettings();

	// ~Begin UObject interface
	virtual void PostLoad() override;
	// ~End UObject interface

	// ~Begin UPCGSettings interface
#if WITH_EDITOR
	virtual void ApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins);
	virtual FName GetDefaultNodeName() const override { return FName(TEXT("AttributeNoise")); }
	virtual FText GetDefaultNodeTitle() const override { return NSLOCTEXT("PCGAttributeNoiseSettings", "NodeTitle", "Attribute Noise"); }
	virtual EPCGSettingsType GetType() const override { return EPCGSettingsType::Metadata; }
#endif

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override { return Super::DefaultPointInputPinProperties(); }
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override { return Super::DefaultPointOutputPinProperties(); }
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings interface

public:
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	FPCGAttributePropertySelector InputSource;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	bool bOutputTargetDifferentFromInputSource = false;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (EditCondition = "bOutputTargetDifferentFromInputSource", EditConditionHides))
	FPCGAttributePropertySelector OutputTarget;

	/** Attribute = (Original op Noise), Noise in [NoiseMin, NoiseMax] */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	EPCGAttributeNoiseMode Mode = EPCGAttributeNoiseMode::Set;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	float NoiseMin = 0.f;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	float NoiseMax = 1.f;

	/** Attribute = 1 - Attribute before applying the operation */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	bool bInvertSource = false;

	// Clamp the result between 0 and 1. Always applied if we apply noise to the density.
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	bool bClampResult = false;

	// Previous names
#if WITH_EDITORONLY_DATA
	UPROPERTY()
	EPCGAttributeNoiseMode DensityMode_DEPRECATED = EPCGAttributeNoiseMode::Set;

	UPROPERTY()
	float DensityNoiseMin_DEPRECATED = 0.f;

	UPROPERTY()
	float DensityNoiseMax_DEPRECATED = 1.f;

	UPROPERTY()
	bool bInvertSourceDensity_DEPRECATED = false;
#endif // WITH_EDITORDATA_ONLY
};

struct FPCGAttributeNoiseContext : public FPCGContext
{
	int32 CurrentInput = 0;
	bool bDataPreparedForCurrentInput = false;
	TUniquePtr<IPCGAttributeAccessor> InputAccessor;
	TUniquePtr<IPCGAttributeAccessor> OptionalOutputAccessor;
	TUniquePtr<IPCGAttributeAccessorKeys> Keys;

	TArray<uint8> TempValuesBuffer;
};

class FPCGAttributeNoiseElement : public IPCGElement
{
public:
	virtual FPCGContext* Initialize(const FPCGDataCollection& InputData, TWeakObjectPtr<UPCGComponent> SourceComponent, const UPCGNode* Node) override;
protected:
	virtual bool ExecuteInternal(FPCGContext* Context) const;
};

#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_2
#include "CoreMinimal.h"
#include "PCGNode.h"
#endif
