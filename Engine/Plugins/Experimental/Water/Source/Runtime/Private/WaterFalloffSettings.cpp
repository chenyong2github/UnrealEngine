// Copyright Epic Games, Inc. All Rights Reserved.

#include "WaterFalloffSettings.h"

FWaterFalloffSettings::FWaterFalloffSettings()
	: FalloffMode(EWaterBrushFalloffMode::Angle)
	, FalloffAngle(45.0f)
	, FalloffWidth(1024.0f)
	, EdgeOffset(0)
	, ZOffset(0)
{
}
