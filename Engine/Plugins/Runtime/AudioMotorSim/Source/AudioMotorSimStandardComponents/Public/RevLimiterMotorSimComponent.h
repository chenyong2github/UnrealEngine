// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "AudioMotorSimTypes.h"
#include "IAudioMotorSim.h"
#include "RevLimiterMotorSimComponent.generated.h"

// Temporarily cuts throttle and reduces RPM when drifting or in the air
UCLASS(ClassGroup = "AudioMotorSim", meta = (BlueprintSpawnableComponent))
class URevLimiterMotorSimComponent : public UAudioMotorSimComponent
{
	GENERATED_BODY()
public:

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RevLimiter")
	float LimitTime = 0.f;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RevLimiter")
	float DecelScale = 0.f;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RevLimiter")
	float AirMaxThrottleTime = 0.f;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RevLimiter")
	float SideSpeedThreshold = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RevLimiter")
	float LimiterMaxRpm = 0.f;

private:
	// Time remaining where the limiter is forcing throttle down
	float TimeRemaining = 0.f;
	float TimeInAir = 0.f;

public:
	virtual void Update(FAudioMotorSimInputContext& Input, FAudioMotorSimRuntimeContext& RuntimeInfo) override;
	virtual void Reset() override;
};