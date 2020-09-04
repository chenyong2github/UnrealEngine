// Copyright Epic Games, Inc. All Rights Reserved.

#include "MagicLeapImageTrackerTypes.h"

FMagicLeapImageTargetSettings::FMagicLeapImageTargetSettings()
: ImageTexture(nullptr)
, Name(TEXT("Undefined"))
, LongerDimension(0.0f)
, bIsStationary(false)
, bIsEnabled(true)
{}
