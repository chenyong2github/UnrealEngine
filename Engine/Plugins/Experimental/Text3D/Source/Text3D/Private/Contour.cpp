// Copyright Epic Games, Inc. All Rights Reserved.


#include "Contour.h"
#include "Part.h"


FContour::FContour()
{
}

FContour::~FContour()
{
	for (FPartPtr Part : *this)
	{
		Part->Prev.Reset();
		Part->Next.Reset();
	}
}

int32 FContour::GetPrev(const int32 Index) const
{
	return (Index + Num() - 1) % Num();
}

int32 FContour::GetNext(const int32 Index) const
{
	return (Index + 1) % Num();
}


// p_1 ~ Edge->Position
// p_2 ~ Edge->Next->Position
// p_3 ~ Point->Position
// n_2 ~ Edge->Next->Normal
// n_3 ~ Point->Normal
// d_2 ~ Edge->Next->DoneExpand
// d_3 ~ Point->DoneExpand
// e   ~ total expand
// t   ~ EdgeA->TangentX
// e_2 = e - d_2
// e_3 = e - d_3
// check if point's normal approaches edge from needed side with sign of cross product ([t, n_3])
// b = d_3 - d_2
// if intersection happens, for point of intersection ((p_2 + e_2 * n_2) - (p_3 + e_3 * n_3)) is parallel to (t)
// so their cross-product is zero: ([(p_2 + e_2 * n_2) - (p_3 + e_3 * n_3), t] = 0)
// then get needed value
// if value is (<= 0), intersection will not happen
// available expand for edge can be received from (e_2 + d_2 = e_3 + d_3)
// check if intersection is _on_ expanded edge with cross products (previous operations guarantee only that it's on the line this edge belongs too, not enough to claim an intersection)
void FContour::ComputeAvailableExpandFar(const FPartPtr Point, const FPartConstPtr Edge)
{
	const FPartConstPtr EdgeA = Edge;
	const FPartConstPtr EdgeB = Edge->Next;

	const FVector2D dp = EdgeA->TangentX;
	const float dpXPointNormal = FVector2D::CrossProduct(dp, Point->Normal);

	if (dpXPointNormal <= 0.f)
	{
		return;
	}

	const float DoneExpandDiff = Point->DoneExpand - EdgeB->DoneExpand;
	const float AvailableExpandPoint = FVector2D::CrossProduct(dp, (DoneExpandDiff * EdgeB->Normal - Point->Position + EdgeB->Position)) / (dpXPointNormal - FVector2D::CrossProduct(dp, EdgeB->Normal));

	if (AvailableExpandPoint <= 0.f)
	{
		return;
	}

	const float AvailableExpandEdgeB = DoneExpandDiff + AvailableExpandPoint;
	const FVector2D PointExpanded = Point->Expanded(AvailableExpandPoint) - EdgeB->Position;

	if (FVector2D::CrossProduct(PointExpanded, (EdgeB->Expanded(AvailableExpandEdgeB) - EdgeB->Position)) < 0.f)
	{
		return;
	}

	if (FVector2D::CrossProduct(PointExpanded, (EdgeA->Expanded(EdgeB->DoneExpand + AvailableExpandEdgeB - EdgeA->DoneExpand) - EdgeB->Position)) > 0.f)
	{
		return;
	}

	Point->AvailableExpandsFar.Add(EdgeA, AvailableExpandPoint);
}
