// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "MassAvoidanceSettings.generated.h"

USTRUCT()
struct MASSAIMOVEMENT_API FMassAvoidanceVelocityFilter
{
	GENERATED_BODY()

	/** Delay between changes. (seconds) */
	UPROPERTY(Category = Zone, EditAnywhere, meta = (ClampMin = "0"))
	double Delay = 0.4;

	/** Minimum speed of the range (cm/s). */
	UPROPERTY(Category = Zone, EditAnywhere, meta = (ClampMin = "0"))
	float LowSpeed = 10.f;

	/** Maximum speed of the range (cm/s). */
	UPROPERTY(Category = Zone, EditAnywhere, meta = (ClampMin = "0"))
	float HighSpeed = 50.f;
};

UCLASS(config = Mass, defaultconfig, meta = (DisplayName = "Mass Avoidance"))
class MASSAIMOVEMENT_API UMassAvoidanceSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	static const UMassAvoidanceSettings* Get()
	{
		return GetDefault<UMassAvoidanceSettings>();
	}

	/** 1..3 Too high value makes close agent search slow, too small removes avoidance effect. Indoor humans 1.4, outdoor humans 2.4 (seconds). */
	UPROPERTY(config, EditAnywhere, Category = "General", meta = (ClampMin = "0.1"))
	float TimeHorizon = 2.5f;


	/** 0..10 How far inside the circle the smooth collisions starts (distance). */
	UPROPERTY(config, EditAnywhere, Category = "Collision", meta = (ClampMin = "0"))
	float AgentCollisionInset = 5.f;

	/** 0..10 How far inside the circle the smooth collisions starts (distance). */
	UPROPERTY(config, EditAnywhere, Category = "Collision", meta = (ClampMin = "0"))
	float ObstacleCollisionInset = 5.f;


	/** 100..500 Separation force, even a little bit of separation can help to smooth out deadlocks in dense crowds. */
	UPROPERTY(config, EditAnywhere, Category = "Separation", meta = (ClampMin = "0"))
	float AgentSeparation = 200.f;

	/** 100..1000 Separation force for obstacles growing near edges. */
	UPROPERTY(config, EditAnywhere, Category = "Separation", meta = (ClampMin = "0"))
	float AgentSeparationForExtendingColliders = 800.f;
	
	/** 0..100 How big the decay/buffer is for separation (distance). */
	UPROPERTY(config, EditAnywhere, Category = "Separation", meta = (ClampMin = "0"))
	float AgentSeparationBuffer = 75.f;

	/** 0..100 Agent separation buffer near target location (distance). */
	UPROPERTY(config, EditAnywhere, Category = "Separation", meta = (ClampMin = "0"))
	float AgentSeparationBufferAtEnd = 15.f;
	
	/** 0..150 Separation buffer for obstacles growing near edges (distance). */
	UPROPERTY(config, EditAnywhere, Category = "Separation", meta = (ClampMin = "0"))
	float AgentSeparationBufferForExtendingColliders = 150.f;
	
	/** Distance threshold where the agent is considered near it's target location and removing agent separation. */
	UPROPERTY(config, EditAnywhere, Category = "Separation", meta = (ClampMin = "0"))
	float NearTargetLocationDistance = 150.f;

	/** 200..1000 Prevents getting stuck obstacles, keeps space to take over close to obstacles (distance). */
	UPROPERTY(config, EditAnywhere, Category = "Separation", meta = (ClampMin = "0"))
	float ObstacleSeparation = 600.f;

	/** 0..200 How big the decay/buffer is for separation (distance). */
	UPROPERTY(config, EditAnywhere, Category = "Separation", meta = (ClampMin = "0"))
	float ObstacleSeparationBuffer = 200.f;


	/** 0..25 Allowed penetration, subtracted from the total of compared agents radius (distance). */
	UPROPERTY(config, EditAnywhere, Category = "Predictive Avoidance", meta = (ClampMin = "0"))
	float AvoidanceInset = 20.f;
	
	/** 0..150 Buffer added when computing distance to agents and obstacles (distance). */
	UPROPERTY(config, EditAnywhere, Category = "Predictive Avoidance", meta = (ClampMin = "0"))
	float AvoidanceBuffer = 100.f;

	/** 0..150 Buffer added when computing distance to agents and obstacles growing near edges (distance). */
	UPROPERTY(config, EditAnywhere, Category = "Predictive Avoidance", meta = (ClampMin = "0"))
	float AvoidanceBufferForExtendingColliders = 100.f;
	
	/** 0..150 Avoidance buffer near target location (distance). */
	UPROPERTY(config, EditAnywhere, Category = "Predictive Avoidance", meta = (ClampMin = "0"))
	float AvoidanceBufferAtEnd = 10.f;

	/** 400..1000 Multiplier on the agent-agent avoidance force (distance). */
	UPROPERTY(config, EditAnywhere, Category = "Predictive Avoidance", meta = (ClampMin = "0"))
	float AgentAvoidanceStiffness = 700.f;

	/** 400..1000 Multiplier on the agent-obstacle avoidance force (distance). */
	UPROPERTY(config, EditAnywhere, Category = "Predictive Avoidance", meta = (ClampMin = "0"))
	float ObstacleAvoidanceStiffness = 400.f;

	/** Time (in seconds) it takes a new heading direction to completely blend in. */
	UPROPERTY(config, EditAnywhere, Category = "Orientation", meta = (ClampMin = "0"))
	float OrientationSmoothingTime = 0.7f;
	
	/** Distance from end of the path when we start to blend into the desired orientation. */
	UPROPERTY(config, EditAnywhere, Category = "Orientation", meta = (ClampMin = "0"))
	float OrientationEndOfPathHeadingAnticipation = 100.0f;
};
