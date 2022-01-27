// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassProcessor.h"
#include "MassObserverProcessor.h"
#include "MassSteeringProcessors.generated.h"

/** 
* Processor for updating steering towards MoveTarget.
*/
UCLASS()
class MASSNAVIGATION_API UMassSteerToMoveTargetProcessor : public UMassProcessor
{
	GENERATED_BODY()

protected:
	UMassSteerToMoveTargetProcessor();
	
	virtual void ConfigureQueries() override;
	virtual void Execute(UMassEntitySubsystem& EntitySubsystem, FMassExecutionContext& Context) override;

	FMassEntityQuery EntityQuery;
};
