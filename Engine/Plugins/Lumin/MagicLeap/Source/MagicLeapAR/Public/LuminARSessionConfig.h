// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"
#include "LuminARTypes.h"
#include "ARSessionConfig.h"
#include "MagicLeapPlanesTypes.h"

#include "LuminARSessionConfig.generated.h"


UCLASS(BlueprintType, Category = "AR AugmentedReality")
class MAGICLEAPAR_API ULuminARSessionConfig : public UARSessionConfig
{
	GENERATED_BODY()

public:
	ULuminARSessionConfig()
		: UARSessionConfig()
	{
		PlanesQuery.Flags.Add(EMagicLeapPlaneQueryFlags::Polygons);
		PlanesQuery.MaxResults = 200;
		PlanesQuery.MinHoleLength = 50.0f;
		PlanesQuery.MinPlaneArea = 400.0f;
		PlanesQuery.SearchVolumeExtents = { 10000.0f, 10000.0f, 10000.0f };
		PlanesQuery.SimilarityThreshold = 1.0f;
		PlanesQuery.bResultTrackingSpace = true;
		PlanesQuery.bSearchVolumeTrackingSpace = true;

		LightEstimationMode = EARLightEstimationMode::None;
	}

	/** The planes information that the AR session uses when generating a query. */
	UPROPERTY(EditAnywhere, Category = "Lumin AR Settings")
	FMagicLeapPlanesQuery PlanesQuery;

	/** The maximum number of plane results that will be returned. */
	UPROPERTY(meta=(DeprecatedProperty, DeprecationMessage="Use PlanesQuery instead."))
	int32 MaxPlaneQueryResults_DEPRECATED = 200;

	/** The minimum area (in square cm) of planes to be returned. This value cannot be lower than 400 (lower values will be capped to this minimum). A good default value is 2500. */
	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "Use PlanesQuery instead."))
	int32 MinPlaneArea_DEPRECATED = 400;

	/** Should we detect planes with any orientation (ie not just horizontal or vertical). */
	UPROPERTY(EditAnywhere, Category = "Lumin AR Settings")
	bool bArbitraryOrientationPlaneDetection = false; // default to false, for now anyway, because some other platforms do not support this.

	/** The dimensions of the box within which plane results will be returned.  The box center and rotation are those of the tracking to world transform origin. */
	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "Use PlanesQuery instead."))
	FVector PlaneSearchExtents_DEPRECATED;

	/** Additional Flags to apply to the plane queries. Note: the plane orientation detection settings also cause flags to be set.  It is ok to duplicate those here.*/
	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "Use PlanesQuery instead."))
	TArray<EMagicLeapPlaneQueryFlags> PlaneQueryFlags_DEPRECATED;

	/** If true discard any 'plane' objects that come through with zero extents and only polygon edge data.*/
	UPROPERTY(EditAnywhere, Category = "Lumin AR Settings")
	bool bDiscardZeroExtentPlanes = true;

	/**
	 For image tracking, Candidate Images may contain both AR Candidate Images and
	 Lumin AR Candidate Images.  The former does not contain info about whether to 
	 update the pose when tracking is unreliable.  In that case, this value is used
	 to determine whether to update the pose, instead.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Lumin AR Settings|Image Tracking")
	bool bDefaultUseUnreliablePose = false;
};
