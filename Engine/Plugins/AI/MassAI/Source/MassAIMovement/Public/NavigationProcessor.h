// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassProcessor.h"
#include "NavigationProcessor.generated.h"

struct FMassDynamicObstacleFragment;
class UMassSignalSubsystem;
class UMassMovementSubsystem;

/**
 * Update agent's transform fragments from post avoidance velocity.
 * Sets agents Z position based on MoveTarget fragment.
 * @todo: separate apply velocity and height adjustment
 */
UCLASS()
class MASSAIMOVEMENT_API UMassApplyVelocityMoveTargetProcessor : public UPipeProcessor
{
	GENERATED_BODY()

public:
	UMassApplyVelocityMoveTargetProcessor();

protected:
	virtual void ConfigureQueries() override;
	virtual void Execute(UEntitySubsystem& EntitySubsystem, FLWComponentSystemExecutionContext& Context) override;

private:
	FLWComponentQuery HighResEntityQuery;
	FLWComponentQuery LowResEntityQuery_Conditional;
};

/** Handle dynamic obstacles. */
UCLASS()
class MASSAIMOVEMENT_API UMassDynamicObstacleProcessor : public UPipeProcessor
{
	GENERATED_BODY()

public:
	UMassDynamicObstacleProcessor();

	/** Delay before sending the stop notification once the entity has stop moving. */
	UPROPERTY(Category = Settings, EditAnywhere, meta = (ClampMin = "0"))
	float DelayBeforeStopNotification = 0.3f;

	/** Distance within which the obstacle is considered not moving. */
	UPROPERTY(Category = Settings, EditAnywhere, meta = (ClampMin = "0"))
	float DistanceBuffer = 10.f;
	
protected:
	virtual void ConfigureQueries() override;
	virtual void Execute(UEntitySubsystem& EntitySubsystem, FLWComponentSystemExecutionContext& Context) override;
	virtual void OnStop(FMassDynamicObstacleFragment& OutObstacle, const float BlockingRadius) {}
	virtual void OnMove(FMassDynamicObstacleFragment& OutObstacle) {}

private:
	FLWComponentQuery EntityQuery_Conditional;
};
