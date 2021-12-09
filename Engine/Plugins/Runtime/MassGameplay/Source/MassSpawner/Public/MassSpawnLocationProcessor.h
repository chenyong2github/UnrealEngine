// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once 

#include "MassProcessor.h"
#include "MassSpawnLocationProcessor.generated.h"

UCLASS()
class MASSSPAWNER_API UMassSpawnLocationProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	UMassSpawnLocationProcessor();

protected:
	virtual void ConfigureQueries() override;
	virtual void Execute(UMassEntitySubsystem& EntitySubsystem, FMassExecutionContext& Context) override;

	FMassEntityQuery EntityQuery;
};
