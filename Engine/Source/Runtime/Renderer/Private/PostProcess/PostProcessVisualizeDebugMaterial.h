// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ScreenPass.h"

// Returns whether any debug material pass is enabled.
bool IsPostProcessVisualizeDebugMaterialEnabled(const FViewInfo& View);

// Returns whether any debug material pass is enabled.
const UMaterialInterface* GetPostProcessVisualizeDebugMaterialInterface(const FViewInfo& View);
