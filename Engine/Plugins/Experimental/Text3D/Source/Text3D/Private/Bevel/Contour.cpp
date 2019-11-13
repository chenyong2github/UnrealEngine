// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.


#include "Bevel/Contour.h"
#include "Bevel/BevelLinear.h"
#include "Bevel/Intersection.h"
#include "Bevel/Part.h"
#include "Bevel/Util.h"
#include "Data.h"


FContour::FContour(FBevelLinear* const BevelIn)
	: Bevel(BevelIn)
{
	ResetContour();
}

FContour::~FContour()
{
	for (const FPart* const Part : *this)
	{
		delete Part;
	}
}

template<class T>
FIntersection& Upcast(T& Intersection)
{
	return static_cast<FIntersection&>(Intersection);
}

bool FContour::BevelTillClosestIntersection()
{
	if (!bHasIntersections)
	{
		return false;
	}

	FIntersectionNear Near(Bevel, this);
	FIntersectionFar Far(Bevel, this);

	FIntersection& Closest = Near.GetValue() <= Far.GetValue() ? Upcast(Near) : Upcast(Far);
	const float Value = Closest.GetValue();

	// If intersection will happen further from front cap then is needed to bevel to, skip
	if (Value > Bevel->GetData()->GetExpand())
	{
		bHasIntersections = false;
		return false;
	}

	Bevel->GetData()->SetExpandTarget(Value);
	Closest.BevelTillThis();
	return true;
}

int32 FContour::GetPrev(const int32 Index) const
{
	return (Index + Num() - 1) % Num();
}

int32 FContour::GetNext(const int32 Index) const
{
	return (Index + 1) % Num();
}

// p_1 ~ Point->InitialPosition
// p_2 ~ Point->Next->InitialPosition
// e   ~ Expand
// d   ~ Point->DoneExpand
// t   ~ Point->TangentX
// a = e - d
// (b_i = p_i + n_i * a) is a position to which point will be expanded
// (b_2 - b_1) has same direction as (t) if no intersection happened and opposite one if it did
// if intersection happened, needed value can be received from (b_2 - b_1 = 0)
void FContour::ComputeAvailableExpandNear(FPart* const Point)
{
	FPart* Next = Point->Next;

	const FVector2D dp = Next->InitialPosition - Point->InitialPosition;
	const FVector2D dn = Next->Normal - Point->Normal;

	// ExpandTotal is used instead of Expand to compute AvailableExpandNear once for all segments, without recomputing them at every BevelLinear call.
	const float Expand = Bevel->GetData()->GetExpandTotal();

	// (2 * Expand) is used to mark this intersection invalid ((Expand) is total needed expand)
	Point->AvailableExpandNear = FVector2D::DotProduct(Point->TangentX, dp + dn * Expand) < 0 ? dp.Size() / dn.Size() - Point->DoneExpand : 2 * Expand;
}

void FContour::ComputeAvailableExpandsFarFrom(FPart* const Point)
{
	Point->AvailableExpandsFar.Empty();

	// IntersectionFar requires at leat 1 part distance (between point and edge) if edge if further counterclockwise then point and at least 2 parts distance if further clockwise (because FPart is point and it's _next_ edge)
	for (const FPart* Edge = Point->Next->Next; Edge != Point->Prev->Prev; Edge = Edge->Next)
	{
		ComputeAvailableExpandFar(Point, Edge);
	}
}

void FContour::ComputeAvailableExpandsFarTo(const FPart* const Edge)
{
	// See comment in ComputeAvailableExpandsFarFrom
	for (FPart* Point = Edge->Next->Next->Next; Point != Edge->Prev; Point = Point->Next)
	{
		Point->AvailableExpandsFar.Remove(Edge);
		ComputeAvailableExpandFar(Point, Edge);
	}
}

void FContour::RemoveRange(const FPart* const Start, const FPart* const End)
{
	const int32 StartIndex = Find(Start);
	const int32 EndIndex = Find(End);

	if (EndIndex < StartIndex)
	{
		RemoveAt(StartIndex, Num() - StartIndex);
		RemoveAt(0, EndIndex);
	}
	else
	{
		RemoveAt(StartIndex, EndIndex - StartIndex);
	}
}

void FContour::ResetContour()
{
	bHasIntersections = true;
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
void FContour::ComputeAvailableExpandFar(FPart* const Point, const FPart* const Edge)
{
	const FPart* const EdgeA = Edge;
	const FPart* const EdgeB = Edge->Next;

	const FVector2D dp = EdgeA->TangentX;
	const float dpXPointNormal = FVector2D::CrossProduct(dp, Point->Normal);

	if (dpXPointNormal <= 0)
	{
		return;
	}

	const float DoneExpandDiff = Point->DoneExpand - EdgeB->DoneExpand;
	const float AvailableExpandPoint = FVector2D::CrossProduct(dp, DoneExpandDiff * EdgeB->Normal - Point->Position + EdgeB->Position) / (dpXPointNormal - FVector2D::CrossProduct(dp, EdgeB->Normal));

	if (AvailableExpandPoint <= 0)
	{
		return;
	}

	const float AvailableExpandEdgeB = DoneExpandDiff + AvailableExpandPoint;
	const FVector2D PointExpanded = Point->Expanded(AvailableExpandPoint) - EdgeB->Position;

	if (FVector2D::CrossProduct(PointExpanded, EdgeB->Expanded(AvailableExpandEdgeB) - EdgeB->Position) < 0)
	{
		return;
	}

	if (FVector2D::CrossProduct(PointExpanded, EdgeA->Expanded(EdgeB->DoneExpand + AvailableExpandEdgeB - EdgeA->DoneExpand) - EdgeB->Position) > 0)
	{
		return;
	}

	Point->AvailableExpandsFar.Add(EdgeA, AvailableExpandPoint);
}
