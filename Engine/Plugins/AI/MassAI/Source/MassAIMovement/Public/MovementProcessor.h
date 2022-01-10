// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassProcessor.h"
#include "MassObserverProcessor.h"
#include "MassMovementSubsystem.h"
#include "MovementProcessor.generated.h"

/** Move and orient */ 
UCLASS()
class MASSAIMOVEMENT_API UMassProcessor_Movement : public UMassProcessor
{
	GENERATED_BODY()

public:
	UMassProcessor_Movement();

protected:
	virtual void ConfigureQueries() override;
	virtual void Execute(UMassEntitySubsystem& EntitySubsystem, FMassExecutionContext& Context) override;

	FMassEntityQuery EntityQuery;
};

/** Base movement processor for 'grid localized circular agent' */
UCLASS()
class MASSAIMOVEMENT_API UMassProcessor_AgentMovement : public UMassProcessor_Movement
{
	GENERATED_BODY()
public:
	UMassProcessor_AgentMovement();

protected:
	virtual void ConfigureQueries() override;
};

/** Destructor processor to remove avoidance obstacles from the avoidance obstacle grid */
UCLASS()
class MASSAIMOVEMENT_API UMassAvoidanceObstacleRemoverFragmentDestructor : public UMassFragmentDeinitializer
{
	GENERATED_BODY()

	UMassAvoidanceObstacleRemoverFragmentDestructor();
	TWeakObjectPtr<UMassMovementSubsystem> WeakMovementSubsystem;

protected:
	virtual void ConfigureQueries() override;
	virtual void Initialize(UObject& Owner) override;
	virtual void Execute(UMassEntitySubsystem& EntitySubsystem, FMassExecutionContext& Context) override;

	FMassEntityQuery EntityQuery;
};
