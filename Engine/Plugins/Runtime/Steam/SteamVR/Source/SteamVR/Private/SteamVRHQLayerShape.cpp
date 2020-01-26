// Copyright Epic Games, Inc. All Rights Reserved.

#include "SteamVRHQLayerShape.h"
#include "SteamVRStereoLayers.h"
#include "IStereoLayers.h"

void USteamVRHQStereoLayerShape::ApplyShape(IStereoLayers::FLayerDesc& LayerDesc)
{
	LayerDesc.SetShape<FSteamVRHQLayer>(bCurved, bAntiAlias, AutoCurveMinDistance, AutoCurveMaxDistance);
}

void USteamVRHQStereoLayerShape::SetCurved(bool InCurved)
{
	if (InCurved == bCurved)
	{
		return;
	}
	bCurved = InCurved;
	MarkStereoLayerDirty();
}

void USteamVRHQStereoLayerShape::SetAntiAlias(bool InAntiAlias)
{
	if (InAntiAlias == bAntiAlias)
	{
		return;
	}
	bAntiAlias = InAntiAlias;
	MarkStereoLayerDirty();
}

void USteamVRHQStereoLayerShape::SetAutoCurveMinDistance(float InDistance)
{
	if (InDistance == AutoCurveMinDistance)
	{
		return;
	}
	AutoCurveMinDistance = InDistance;
	MarkStereoLayerDirty();
}

void USteamVRHQStereoLayerShape::SetAutoCurveMaxDistance(float InDistance)
{
	if (InDistance == AutoCurveMaxDistance)
	{
		return;
	}
	AutoCurveMaxDistance = InDistance;
	MarkStereoLayerDirty();
}

