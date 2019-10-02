// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "FalloffSettings.h"

FLandmassFalloffSettings::FLandmassFalloffSettings()
	: FalloffMode(EBrushFalloffMode::Angle)
	, FalloffAngle(45.0f)
	, FalloffWidth(1024.0f)
	, EdgeOffset(0)
	, ZOffset(0)
{

}
