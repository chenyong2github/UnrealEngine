// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PCGElement.h"
#include "PCGSettings.h"
#include "Data/PCGDifferenceData.h"

#include "PCGDifferenceElement.generated.h"

UCLASS(BlueprintType, ClassGroup = (Procedural))
class UPCGDifferenceSettings : public UPCGSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings interface
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return FName(TEXT("DifferenceNode")); }
#endif

protected:
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings interface

public:
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	EPCGDifferenceDensityFunction DensityFunction = EPCGDifferenceDensityFunction::Minimum;

#if WITH_EDITORONLY_DATA
	UPROPERTY(Transient, BlueprintReadWrite, EditAnywhere, Category = Debug)
	bool bKeepZeroDensityPoints = false;
#endif
};

class FPCGDifferenceElement : public FSimpleTypedPCGElement<UPCGDifferenceSettings>
{
protected:
	virtual bool ExecuteInternal(FPCGContext* Context) const;
};