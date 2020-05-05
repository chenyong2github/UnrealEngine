// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ScreenPass.h"

// Returns whether any calibration material pass is enabled.
bool IsPostProcessVisualizeCalibrationMaterialEnabled(const FViewInfo& View);

// Returns whether any calibration material pass is enabled.
const UMaterialInterface* GetPostProcessVisualizeCalibrationMaterialInterface(const FViewInfo& View);
