// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GenlockedCustomTimeStep.h"

#include "GenlockedFixedRateCustomTimeStep.generated.h"

class UEngine;

/**
 * Control the Engine TimeStep via a fixed frame rate.
 * 
 * Philosophy:
 * 
 *   * Quantized increments but keeping up with platform time.
 * 
 *   * FApp::GetDeltaTime 
 *       - Forced to a multiple of the desired FrameTime.
 * 
 *   * FApp::GetCurrentTime
 *       - Incremented in multiples of the desired FrameTime.
 *       - Corresponds to platform time minus any fractional FrameTime.
 * 
 */
UCLASS(Blueprintable, editinlinenew, meta = (DisplayName = "Genlocked Fixed Rate"))
class TIMEMANAGEMENT_API UGenlockedFixedRateCustomTimeStep : public UGenlockedCustomTimeStep
{
	GENERATED_UCLASS_BODY()

public:
	//~ UFixedFrameRateCustomTimeStep interface
	virtual bool Initialize(UEngine* InEngine) override;
	virtual void Shutdown(UEngine* InEngine) override;
	virtual bool UpdateTimeStep(UEngine* InEngine) override;
	virtual ECustomTimeStepSynchronizationState GetSynchronizationState() const override;
	virtual FFrameRate GetFixedFrameRate() const override;

	//~ UGenlockedCustomTimeStep interface
	virtual uint32 GetLastSyncCountDelta() const override;
	virtual bool IsLastSyncDataValid() const override;
	virtual bool WaitForSync() override;

public:

	/** Desired frame rate */
	UPROPERTY(EditAnywhere, Category = "Timing")
	FFrameRate FrameRate;

private:
	uint32 LastSyncCountDelta;
	double QuantizedCurrentTime;
	double LastIdleTime;
};
