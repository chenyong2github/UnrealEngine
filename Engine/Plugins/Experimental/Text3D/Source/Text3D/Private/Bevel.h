// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once


#include "Bevel/BevelType.h"
#include "Bevel/Mesh.h"

#include "CoreMinimal.h"
#include "DynamicMeshBuilder.h"


class FTVectoriser;
class FData;

/**
 * Bevel contours and add back cap.
 * @param Data - Meshes data.
 * @param Vectoriser - FTGL object that contains contours.
 * @param Extrude - Orthogonal (to front cap) offset value.
 * @param Bevel - Bevel value (bevel happens before extrude).
 * @param Type - Defines shape of beveled part.
 * @param HalfCircleSegments - Segments count for Type == EText3DBevelType::HalfCircle.
 * @param IterationsIn - Debug variable, amount of intersections to bevel till.
 * @param bHidePreviousIn - Debug variable, hides all iterations except the last one.
 * @param MarkedVertex - Debug variable, index of vertex to mark (it's number in contour).
 * @param Segments - Debug variable, needed for debugging IntersectionNear (case when normals of more then 2 points intersect). Amount of segments that will bevel to one point.
 * @param VisibleFaceIn - Debug variable.
 */
void BevelContours(const TSharedPtr<FData> Data, const FTVectoriser& Vectoriser, const float Extrude, const float Bevel, EText3DBevelType Type, const int32 HalfCircleSegments, const int32 IterationsIn, const bool bHidePreviousIn, const int32 MarkedVertex, const int32 Segments, const int32 VisibleFaceIn);
