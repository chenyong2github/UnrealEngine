// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Engine/Texture.h"
#include "LensData.h"

#include "CameraCalibrationEditorCommon.generated.h"


/** Container of distortion data to use instanced customization to show parameter struct defined by the model */
USTRUCT()
struct FDistortionInfoContainer
{
	GENERATED_BODY()

public:

	/** Distortion parameters */
	UPROPERTY(EditAnywhere, Category = "Distortion", meta = (ShowOnlyInnerProperties))
	FDistortionInfo DistortionInfo;
};

/** Holds the last FIZ data that was evaluated */
struct FCachedFIZData
{
	TOptional<float> NormalizedFocus;
	TOptional<float> NormalizedIris;
	TOptional<float> NormalizedZoom;

	TOptional<float> Focus;
	TOptional<float> Iris;
	TOptional<float> Zoom;
};

