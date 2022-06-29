// Copyright Epic Games, Inc. All Rights Reserved.

#include "RevLimiterMotorSimComponent.h"

void URevLimiterMotorSimComponent::Update(FAudioMotorSimInputContext& Input, FAudioMotorSimRuntimeContext& RuntimeInfo)
{
	if (Input.bDriving && Input.bGrounded && Input.SideSpeed < SideSpeedThreshold)
	{
		TimeRemaining = 0.f;
		TimeInAir = 0.0f;
		return;
	}

	Input.bCanShift = false;

	// We've hit the limiter
	if (RuntimeInfo.Rpm >= LimiterMaxRpm)
	{
		TimeRemaining = LimitTime;
		RuntimeInfo.Rpm = LimiterMaxRpm;
	}

	if (TimeRemaining > 0.0f)
	{
		Input.Throttle = 0.0f;

		TimeRemaining -= Input.DeltaTime;
		Input.bClutchEngaged = true;
	}

	if (Input.bGrounded)
	{
		TimeInAir = 0.0f;
		return;
	}

	Input.bClutchEngaged = true;

	if (TimeRemaining > 0.0f)
	{
		TimeInAir += Input.DeltaTime;
	}

	if (TimeInAir >= AirMaxThrottleTime)
	{
		Input.Throttle = 0.0f;
	}
}

void URevLimiterMotorSimComponent::Reset()
{
	TimeRemaining = 0.f;
	TimeInAir = 0.f;
}
