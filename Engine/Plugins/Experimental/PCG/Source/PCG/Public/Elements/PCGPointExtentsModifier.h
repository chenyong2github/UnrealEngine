// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PCGSettings.h"
#include "PCGElement.h"
#include "PCGNode.h"

#include "Elements/PCGPointProcessingElementBase.h"

#include "PCGPointExtentsModifier.generated.h"

UENUM()
enum class EPCGPointExtentsModifierMode : uint8
{
	Set,
	Minimum,
	Maximum,
	Add,
	Multiply
};

UCLASS(BlueprintType, ClassGroup = (Procedural))
class PCG_API UPCGPointExtentsModifierSettings : public UPCGSettings
{
	GENERATED_BODY()

public:

	// ~Begin UPCGSettings interface
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return FName(TEXT("ExtentsModifier")); }
	virtual EPCGSettingsType GetType() const override { return EPCGSettingsType::Spatial; }
#endif

	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override { return Super::DefaultPointOutputPinProperties(); }

protected:
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings interface

public:
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	FVector Extents = FVector::One();

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	EPCGPointExtentsModifierMode Mode = EPCGPointExtentsModifierMode::Set;
};

class FPCGPointExtentsModifier : public FPCGPointProcessingElementBase
{
protected:
	virtual bool ExecuteInternal(FPCGContext* Context) const;
};
