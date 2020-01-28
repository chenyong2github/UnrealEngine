// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once
#include "StereoLayerShapes.h"

/** Class describing additional settings for high-quality layers. Note there can only be one HQ layer active at any given time. */
class STEAMVR_API FSteamVRHQLayer : public IStereoLayerShape
{
	STEREO_LAYER_SHAPE_BOILERPLATE(FSteamVRHQLayer)
public:

	FSteamVRHQLayer() {}
	FSteamVRHQLayer(bool bInCurved, bool bInAntiAlias, float InAutoCurveMinDistance, float InAutoCurveMaxDistance) :
		bCurved(bInCurved),
		bAntiAlias(bInAntiAlias),
		AutoCurveMinDistance(InAutoCurveMinDistance),
		AutoCurveMaxDistance(InAutoCurveMaxDistance)
	{}

	bool bCurved;
	bool bAntiAlias;
	float AutoCurveMinDistance;
	float AutoCurveMaxDistance;
};