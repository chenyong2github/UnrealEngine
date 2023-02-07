// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Elements/PCGDataFromActor.h"

#include "PCGSplineFromActor.generated.h"

/** Builds a collection of spline data from the selected actors. */
UCLASS(BlueprintType, ClassGroup = (Procedural))
class PCG_API UPCGSplineFromActorSettings : public UPCGDataFromActorSettings
{
	GENERATED_BODY()

public:
	UPCGSplineFromActorSettings();

	//~Begin UPCGSettings interface
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return FName(TEXT("Spline From Actor")); }
	virtual FText GetNodeTooltipText() const override;
#endif

protected:
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;

	/** Override this to filter what kinds of data should be retrieved from the actor(s). */
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings

public:
	//~Begin UPCGDataFromActorSettings interface
	virtual bool DataFilter(EPCGDataType InDataType) const { return !!(InDataType & EPCGDataType::PolyLine); }
	//~End UPCGDataFromActorSettings
};
