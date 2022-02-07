// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassObserverProcessor.h"
#include "AssignDebugVisProcessor.generated.h"


class UMassDebugVisualizationComponent;
struct FSimDebugVisFragment;

UCLASS()
class MASSGAMEPLAYDEBUG_API UAssignDebugVisProcessor : public UMassObserverProcessor
{
	GENERATED_BODY()
public:
	UAssignDebugVisProcessor();
	virtual void ConfigureQueries() override;
	virtual void Execute(UMassEntitySubsystem& EntitySubsystem, FMassExecutionContext& Context) override;
	virtual void Initialize(UObject& InOwner) override;

protected:
	TWeakObjectPtr<UMassDebugVisualizationComponent> WeakVisualizer;

	FMassEntityQuery EntityQuery;
};
