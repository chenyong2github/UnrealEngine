// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class FRHICommandListImmediate;

extern RENDERCORE_API void IssueScalableLongGPUTask(FRHICommandListImmediate& RHICmdList, int32 NumIteration = -1);
extern RENDERCORE_API void MeasureLongGPUTaskExecutionTime(FRHICommandListImmediate& RHICmdList);
