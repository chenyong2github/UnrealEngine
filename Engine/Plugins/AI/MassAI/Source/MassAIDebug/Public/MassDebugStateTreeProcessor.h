// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassProcessor.h"
#include "MassDebugStateTreeProcessor.generated.h"

class UMassEntitySubsystem;
struct FMassStateTreeFragment;
struct FMassEntityQuery;
struct FMassExecutionContext;

UCLASS()
class MASSAIDEBUG_API UMassDebugStateTreeProcessor : public UMassProcessor
{
	GENERATED_BODY()

protected:
	UMassDebugStateTreeProcessor();

	virtual void ConfigureQueries() override;
	virtual void Execute(UMassEntitySubsystem& EntitySubsystem, FMassExecutionContext& Context) override;

	FMassEntityQuery EntityQuery;
};
