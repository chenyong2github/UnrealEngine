// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassProcessor.h"
#include "DebugVisLocationProcessor.generated.h"

class UMassDebugVisualizationComponent;
struct FSimDebugVisComponent;

UCLASS()
class MASSGAMEPLAYDEBUG_API UDebugVisLocationProcessor : public UPipeProcessor
{
	GENERATED_BODY()

public:
	UDebugVisLocationProcessor();
	
protected:
	virtual void ConfigureQueries() override;
	virtual void Execute(UEntitySubsystem& EntitySubsystem, FLWComponentSystemExecutionContext& Context) override;
	virtual void Initialize(UObject& InOwner) override;

	TWeakObjectPtr<UMassDebugVisualizationComponent> WeakVisualizer;
	FLWComponentQuery EntityQuery;
};

//----------------------------------------------------------------------//
// new one 
//----------------------------------------------------------------------//
//class UMassDebugger;

UCLASS()
class MASSGAMEPLAYDEBUG_API UMassProcessor_UpdateDebugVis : public UPipeProcessor
{
	GENERATED_BODY()
public:
	UMassProcessor_UpdateDebugVis();
	virtual void ConfigureQueries() override;
	virtual void Execute(UEntitySubsystem& EntitySubsystem, FLWComponentSystemExecutionContext& Context) override;

protected:
	FLWComponentQuery EntityQuery;
};

