// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "InteractiveTool.h"

#include "ParameterizeMeshProperties.generated.h"

UENUM()
enum class EParameterizeMeshUVMethod
{
	// keep values the same as UE::Geometry::EParamOpBackend!
	/** Compute Automatic UVs using PatchBuilder */
	PatchBuilder = 0,
	/** Compute Automatic UVs using XAtlas */
	XAtlas = 1,
	/** Compute Automatic UVs using UVAtlas */
	UVAtlas = 2,
};


UCLASS()
class MODELINGOPERATORSEDITORONLY_API UParameterizeMeshToolProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()
public:
	/** Automatic UV Generation technique to use */
	UPROPERTY(EditAnywhere, Category = Options)
	EParameterizeMeshUVMethod Method = EParameterizeMeshUVMethod::PatchBuilder;
};


/**
 * Settings for the UVAtlas Automatic UV Generation Method
 */
UCLASS()
class MODELINGOPERATORSEDITORONLY_API UParameterizeMeshToolUVAtlasProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()
public:
	/** Maximum amount of stretch, from none to any.  If zero stretch is specified each triangle will likely be its own chart */
	UPROPERTY(EditAnywhere, Category = UVAtlas, meta = (UIMin = "0", UIMax = "1", ClampMin = "0", ClampMax = "1"))
	float ChartStretch = 0.11f;

	/** Hint at number of Charts. 0 (Default) means UVAtlas will decide */
	UPROPERTY(EditAnywhere, Category = UVAtlas, meta = (UIMin = "0", UIMax = "100", ClampMin = "0", ClampMax = "10000"))
	int NumCharts = 0;
};


UCLASS()
class MODELINGOPERATORSEDITORONLY_API UParameterizeMeshToolXAtlasProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()
public:
	/** Number of solve iterations. Higher values generally result in better charts. */
	UPROPERTY(EditAnywhere, Category = XAtlas, meta = (UIMin = "1", UIMax = "10", ClampMin = "1", ClampMax = "1000"))
	int MaxIterations = 1;
};


UCLASS()
class MODELINGOPERATORSEDITORONLY_API UParameterizeMeshToolPatchBuilderProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()
public:
	/** Number of initial patches mesh will be split into before computing island merging */
	UPROPERTY(EditAnywhere, Category = "PatchBuilder", meta = (UIMin = "1", UIMax = "1000", ClampMin = "1", ClampMax = "99999999"))
	int InitialPatches = 100;

	/** This parameter controls alignment of the initial patches to creases in the mesh */
	UPROPERTY(EditAnywhere, Category = "PatchBuilder", meta = (UIMin = "0.1", UIMax = "2.0", ClampMin = "0.01", ClampMax = "100.0"))
	float CurvatureAlignment = 1.0f;

	/** Distortion/Stretching Threshold for island merging - larger values increase the allowable UV stretching */
	UPROPERTY(EditAnywhere, Category = "PatchBuilder", meta = (UIMin = "1.0", UIMax = "5.0", ClampMin = "1.0"))
	float MergingThreshold = 1.5f;

	/** UV islands will not be merged if their average face normals deviate by larger than this amount */
	UPROPERTY(EditAnywhere, Category = "PatchBuilder", meta = (UIMin = "0.0", UIMax = "90.0", ClampMin = "0.0", ClampMax = "180.0"))
	float MaxAngleDeviation = 45.0f;

	/** Number of smoothing steps to apply in the ExpMap UV Generation method (Smoothing slightly increases distortion but produces more stable results) */
	UPROPERTY(EditAnywhere, Category = "PatchBuilder", meta = (UIMin = "0", UIMax = "25", ClampMin = "0", ClampMax = "1000"))
	int SmoothingSteps = 5;

	/** Smoothing parameter, larger values result in faster smoothing in each step */
	UPROPERTY(EditAnywhere, Category = "PatchBuilder", meta = (UIMin = "0", UIMax = "1.0", ClampMin = "0", ClampMax = "1.0"))
	float SmoothingAlpha = 0.25f;


	/** If enabled, result UVs are automatically packed into the standard UV 0-1 square */
	UPROPERTY(EditAnywhere, Category = "PatchBuilder")
	bool bAutoPack = true;

	/** Target texture resolution used for UV packing, which determines gutter size */
	UPROPERTY(EditAnywhere, Category = "PatchBuilder", meta = (UIMin = "64", UIMax = "2048", ClampMin = "2", ClampMax = "4096", EditCondition = bAutoPack))
	int TextureResolution = 1024;
};
