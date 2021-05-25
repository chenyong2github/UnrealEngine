// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/SceneComponent.h"

#include "CalibrationPointComponent.generated.h"

/**
 * One or more instances of this component can be added to an actor (e.g. a static mesh actor blueprint), 
 * and should be placed at geometrically and visually distinct landmarks of the object.
 * These 3d points will then be optionally used by any given nodal offset tool implementation to
 * make a 3d-2d correspondence with the 2d points detected in the live action media.
 */
UCLASS(ClassGroup = (Calibration), meta = (BlueprintSpawnableComponent), meta = (DisplayName = "Calibration Point"))
class CAMERACALIBRATION_API UCalibrationPointComponent : public USceneComponent
{
	GENERATED_BODY()
};