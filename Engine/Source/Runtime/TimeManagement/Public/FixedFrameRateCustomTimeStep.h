// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/EngineCustomTimeStep.h"

#include "Misc/FrameRate.h"

#include "FixedFrameRateCustomTimeStep.generated.h"



/**
 * Class to control the Engine TimeStep via a FixedFrameRate
 */
UCLASS(Abstract)
class TIMEMANAGEMENT_API UFixedFrameRateCustomTimeStep : public UEngineCustomTimeStep
{
	GENERATED_UCLASS_BODY()

public:
	/** Get The fixed FrameRate */
	virtual FFrameRate GetFixedFrameRate() const PURE_VIRTUAL(UFixedFrameRateCustomTimeStep::GetFixedFrameRate, return GetFixedFrameRate_PureVirtual(););

protected:
	/** Default behavior of the engine. Used FixedFrameRate */
	void WaitForFixedFrameRate() const;

private:
	FFrameRate GetFixedFrameRate_PureVirtual() const;
};
