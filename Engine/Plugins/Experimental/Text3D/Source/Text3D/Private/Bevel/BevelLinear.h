// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"

#include "Bevel/Contour.h"
#include "Bevel/Data.h"

struct FPart;

class FTVectoriser;

/** Makes actual bevel. */
class FBevelLinear final
{
public:
	//TODO this need to be way better
	static int32 Iteration;
	static int32 Iterations;
	static int32 VisibleFace;
	static bool bHidePrevious;


	/**
	 * Contructor.
	 * @param MeshesIn - Documented in Bevel.h.
	 * @param ExpandTotal - Total expand value (for all ArcSegments from Bevel.cpp).
	 * @param HorizontalOffset - Documented in Bevel.h.
	 * @param VerticalOffset - Documented in Bevel.h.
	 * @param FontInverseScale - Documented in Bevel.h.
	 * @param Scale - Documented in Bevel.h.
	 * @param Vectoriser - Documented in Bevel.h.
	 * @param IterationsIn - Documented in Bevel.h.
	 * @param bHidePreviousIn - Documented in Bevel.h.
	 * @param Segments - Documented in Bevel.h.
	 * @param VisibleFaceIn - Documented in Bevel.h.
	 */
	FBevelLinear(TText3DMeshList* MeshesIn, const float ExpandTotal, const float HorizontalOffset, const float VerticalOffset, const float FontInverseScale, const FVector& Scale, const FTVectoriser& Vectoriser, const int32 IterationsIn, const bool bHidePreviousIn, const int32 Segments, const int32 VisibleFaceIn);

	/**
	 * Invoke bevel.
	 * @param Extrude - Documented in Bevel.h.
	 * @param Expand - Documented in Bevel.h.
	 * @param NormalStart - Normal at start of segment (minimum DoneExpand).
	 * @param NormalEnd - Normal at end of segment (maximum DoneExpand).
	 * @param bSmooth - Is angle between start of this segment and end of previous segment smooth?
	 * @param MarkedVertex - Documented in Bevel.h.
	 */
	void BevelContours(const float Extrude, const float Expand, FVector2D NormalStart, FVector2D NormalEnd, const bool bSmooth, const int32 MarkedVertex);

	/**
	 * Create back cap.
	 */
	void CreateBackCap();

	/**
	 * Create contour.
	 * @return Reference to created contour.
	 */
	FContour& AddContour();

	/**
	 * Remove contour.
	 * @param Contour - Const reference to contour that should be removed.
	 */
	void RemoveContour(const FContour& Contour);

	/**
	 * Get meshes data.
	 */
	FData* GetData();

	/**
	 * FPart::Expanded for total expand value Data::ExpandTarget.
	 * @param Point - Point for which position should be computed.
	 * @return Computed position.
	 */
	FVector2D Expanded(const FPart* const Point) const;
	/**
	 * Similar to FBevelLinear::Expanded but actually creates vertices and writes indices to paths.
	 * @param Point - Point that should be expanded.
	 * @param Count - Amount of edges to which result point will belong. 2 for case without intersections, (n + 1) for case when (n) normals intersect in result point.
	 */
	void ExpandPoint(FPart* const Point, const int32 Count);

	/**
	 * Make triangulation of edge along paths of it's vertices (from end of previous triangulation to result of points' expansion). Removes covered points' indices from paths.
	 * @param Edge - Edge that has to be filled.
	 * @param bSkipLastTriangle - Do not create last triangle (furthest from end of previous triangulation).
	 */
	void FillEdge(FPart* const Edge, const bool bSkipLastTriangle);

private:
	TUniquePtr<FData> Data;
	TDoubleLinkedList<FContour> Contours;


	/**
	 * Write contours from Vectorizer to list that is used for Bevel.
	 * @param Vectoriser - Documented in Bevel.h.
	 */
	void CreateContours(const FTVectoriser& Vectoriser);
	/**
	 * Create contour that has IntersectionNear with more then 2 normals.
	 * @param Segments - Documented in Bevel.h.
	 */
	void CreateDebugSegmentsContour(const int32 Segments);
	/**
	 * Create contour that changes (DoneExpand) of (Edge->Next) and then forces recompute of (AvailableExpandFar) from (Point) to (Edge) with IntersectionNear in (Point). This happens while Expand is increased. Also part of contour can be moved with changing Extrude to debug different cases.
	 */
	void CreateDebugIntersectionFarContour();

	/**
	 * Initialize contours
	 */
	void InitContours();

	/**
	 * Duplicate contour vertices (used to make sharp angle between bevel steps)
	 */
	void DuplicateContourVertices();

	/**
	 * Prepare contours for beveling (is executed before each step).
	 */
	void ResetContours(const float Extrude, const float Expand, FVector2D NormalStart, FVector2D NormalEnd);

	/**
	 * Make bevel only for non-trivial places.
	 */
	void BevelPartsWithIntersectingNormals();

	/**
	 * Continue with trivial bevel till FData::Expand.
	 */
	void BevelPartsWithoutIntersectingNormals();

	/**
	 * Add triangle near specified vertex (needed to find out it's index).
	 * @param MarkedVertex - Documented in Bevel.h.
	 */
	void MarkVertex(const int32 MarkedVertex);

	/**
	 * Make triangle fan, called from FBevelLinear::FillEdge.
	 * @param Cap - Cap of triangle fan.
	 * @param Normal - Point, fan will be created along it's normal.
	 * @param bNormalIsCapNext - Normal is next point after cap or vice versa.
	 * @param bSkipLastTriangle - See FBevelLinear::FillEdge.
	 */
	void MakeTriangleFanAlongNormal(const FPart* const Cap, FPart* const Normal, const bool bNormalIsCapNext, const bool bSkipLastTriangle);
};
