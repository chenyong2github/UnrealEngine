// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RHIDefinitions.h"

class FViewInfo;
class FScene;

#if RHI_RAYTRACING

int32 GetRayTracingReflectionsSamplesPerPixel(const FViewInfo& View);

bool ShouldRenderRayTracingReflections(const FViewInfo& View);
bool ShouldRayTracedReflectionsUseHybridReflections();
bool ShouldRayTracedReflectionsSortMaterials(const FViewInfo& View);
bool ShouldRayTracedReflectionsRayTraceSkyLightContribution(const FScene& Scene);

#endif // RHI_RAYTRACING
