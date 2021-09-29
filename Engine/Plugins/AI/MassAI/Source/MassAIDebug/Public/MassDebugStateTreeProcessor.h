// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassProcessor.h"
#include "MassDebugStateTreeProcessor.generated.h"

class UEntitySubsystem;
struct FMassStateTreeFragment;
struct FLWComponentQuery;
struct FLWComponentSystemExecutionContext;

UCLASS()
class MASSAIDEBUG_API UMassDebugStateTreeProcessor : public UPipeProcessor
{
	GENERATED_BODY()

protected:
	UMassDebugStateTreeProcessor();

	virtual void ConfigureQueries() override;
	virtual void Execute(UEntitySubsystem& EntitySubsystem, FLWComponentSystemExecutionContext& Context) override;

	FLWComponentQuery EntityQuery;
};
