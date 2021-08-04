// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

#include "Math/IntVector.h"
#include "RHIDefinitions.h"

class FRHIShaderResourceView;
class FRHIUnorderedAccessView;

class FROSTYSENSORGRIDSHADERS_API FFrostySensorGridHelper
{
public:
	FFrostySensorGridHelper(ERHIFeatureLevel::Type InFeatureLevel, const FIntVector& InSensorGridDimensions);

	void BuildBounds(FRHICommandList& RHICmdList, FRHIUnorderedAccessView* HierarchyUAv);
	void FindNearestSensors(FRHICommandList& RHICmdList, FRHIShaderResourceView* HierarchySrv, const FVector2D& GlobalSensorRange, FRHIUnorderedAccessView* ResultsUav);

private:
	const ERHIFeatureLevel::Type FeatureLevel;
	const FIntVector SensorGridDimensions;
};