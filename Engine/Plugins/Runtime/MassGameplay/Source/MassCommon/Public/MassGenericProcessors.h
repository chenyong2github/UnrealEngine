// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassProcessor.h"
#include "MassCommonTypes.h"
#include "MassGenericProcessors.generated.h"


/** 
 *  An experimental processor that can be configured to work on any LWComponent type as long as it's size matches
 *  FVectorComponent (effectively the size of FVector). Meant mostly for prototyping.
 *  @todo currently the processor is randomizing it as if it was a fixed-max-length 2d velocity vector. If we keep this
 *  idea around we can add more properties to the processor that would configure the behavior.
 */
UCLASS()
class MASSCOMMON_API URandomizeVectorProcessor : public UPipeProcessor
{
	GENERATED_BODY()

protected:
	TArrayView<FVector> ComponentList;
	// note that only types having same size as FVector and extending FLWComponentData will be accepted
	UPROPERTY(EditAnywhere, Category=Processor, NoClear)
	UScriptStruct* ComponentType;

public:
	URandomizeVectorProcessor();

	virtual void ConfigureQueries() override
	{
		if (ComponentType)
		{
			EntityQuery.AddRequirement(ComponentType, ELWComponentAccess::ReadWrite);
		}
	}

	virtual void Execute(UEntitySubsystem& EntitySubsystem, FLWComponentSystemExecutionContext& Context) override;

	UPROPERTY(EditAnywhere, Category = "Mass", meta = (ClampMin = 1, UIMin = 1))
	float MinMagnitude = 1.f;

	UPROPERTY(EditAnywhere, Category = "Mass", meta = (ClampMin = 1, UIMin = 1))
	float MaxMagnitude = 1.f;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif // WITH_EDITOR

protected:
	FLWComponentQuery EntityQuery;
};
