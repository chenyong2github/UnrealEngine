// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PCGElement.h"
#include "PCGSettings.h"
#include "Data/PCGIntersectionData.h"

#include "PCGIntersectionElement.generated.h"

UCLASS(BlueprintType, ClassGroup = (Procedural))
class UPCGIntersectionSettings : public UPCGSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings interface
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return FName(TEXT("IntersectionNode")); }
#endif

protected:
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings interface

public:
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	EPCGIntersectionDensityFunction DensityFunction = EPCGIntersectionDensityFunction::Multiply;

#if WITH_EDITORONLY_DATA
	UPROPERTY(Transient, BlueprintReadWrite, EditAnywhere, Category = Debug)
	bool bKeepZeroDensityPoints = false;
#endif
};

class FPCGIntersectionElement : public FSimpleTypedPCGElement<UPCGIntersectionSettings>
{
protected:
	virtual bool ExecuteInternal(FPCGContext* Context) const;
};