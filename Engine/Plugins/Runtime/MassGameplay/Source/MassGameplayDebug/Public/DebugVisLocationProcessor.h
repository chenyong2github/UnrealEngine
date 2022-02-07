// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassProcessor.h"
#include "DebugVisLocationProcessor.generated.h"

class UMassDebugVisualizationComponent;
struct FSimDebugVisFragment;

UCLASS()
class MASSGAMEPLAYDEBUG_API UDebugVisLocationProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	UDebugVisLocationProcessor();
	
protected:
	virtual void ConfigureQueries() override;
	virtual void Execute(UMassEntitySubsystem& EntitySubsystem, FMassExecutionContext& Context) override;
	virtual void Initialize(UObject& InOwner) override;

	TWeakObjectPtr<UMassDebugVisualizationComponent> WeakVisualizer;
	FMassEntityQuery EntityQuery;
};

//----------------------------------------------------------------------//
// new one 
//----------------------------------------------------------------------//
//class UMassDebugger;

UCLASS()
class MASSGAMEPLAYDEBUG_API UMassProcessor_UpdateDebugVis : public UMassProcessor
{
	GENERATED_BODY()
public:
	UMassProcessor_UpdateDebugVis();
	virtual void ConfigureQueries() override;
	virtual void Execute(UMassEntitySubsystem& EntitySubsystem, FMassExecutionContext& Context) override;

protected:
	FMassEntityQuery EntityQuery;
};

