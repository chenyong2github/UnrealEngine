// Copyright Epic Games, Inc. All Rights Reserved.


#include "Bevel/Intersection.h"
#include "Bevel/BevelLinear.h"
#include "Bevel/Contour.h"
#include "Bevel/Part.h"
#include "Bevel/Util.h"
#include "Data.h"


FIntersection::FIntersection(FBevelLinear* const BevelIn, FContour* const ContourIn)
	: Bevel(BevelIn)
	, Contour(*ContourIn)

	, Vertex(nullptr)
	// Make Value more then max expand to invalidate intersection
	, Value(Bevel->GetData()->GetExpand() * 2)
{

}

FIntersection::~FIntersection()
{

}

float FIntersection::GetValue() const
{
	return Value;
}

FPart* FIntersection::GetVertex() const
{
	return Vertex;
}

bool FIntersection::ContourHasCloserIntersectionAt(FPart* const Point, const float Expand)
{
	const float AvailableExpand = Point->DoneExpand + Expand;

	if (AvailableExpand >= Value)
	{
		return false;
	}

	Value = AvailableExpand;
	Vertex = Point;

	return true;
}


FIntersectionNear::FIntersectionNear(FBevelLinear* const Bevel, FContour* const ContourIn)
	: FIntersection(Bevel, ContourIn)
{
	for (FPart* Point : Contour)
	{
		ContourHasCloserIntersectionAt(Point, Point->AvailableExpandNear);
	}
}

void FIntersectionNear::BevelTillThis()
{
	FPart* Curr = GetVertex();
	const FVector2D Intersection = Bevel->Expanded(Curr);
	int32 Count = 1;

	const FBevelLinear* const BevelLocal = Bevel;
	auto ExpandsToSamePoint = [BevelLocal, Intersection](const FPart* const Point)
	{
		return FMath::IsNearlyZero(FVector2D::DistSquared(BevelLocal->Expanded(Point), Intersection), 10);
	};

	// Check previous points till a point that does not expand to same position is found
	FPart* Prev = Curr->Prev;
	for (; ExpandsToSamePoint(Prev); Prev = Prev->Prev, Count++)
	{
		if (Prev == Curr)
		{
			Bevel->RemoveContour(Contour);
			return;
		}
	}

	// Check next points
	Count += 2;
	FPart* Next = Curr->Next->Next;
	for (; ExpandsToSamePoint(Next); Next = Next->Next, Count++)
	{

	}

	// Curr is the last one of points that expand to same position
	Curr = Next->Prev;


	// Create vertices
	Bevel->ExpandPoint(Prev, 2);
	Bevel->ExpandPoint(Curr, Count);
	Bevel->ExpandPoint(Next, 2);

	const float PrevDelta = GetValue() - Prev->DoneExpand;
	const float NextDelta = GetValue() - Next->DoneExpand;

	// Create triangles
	int32 Index = 0;
	if (FBevelLinear::VisibleFace== -1 || FBevelLinear::VisibleFace== Index)
	{
		Bevel->FillEdge(Prev, false);
	}

	Index++;
	for (FPart* Edge = Prev->Next; Edge != Curr; Edge = Edge->Next, Index++)
	{
		if (FBevelLinear::VisibleFace == -1 || FBevelLinear::VisibleFace == Index)
		{
			Bevel->FillEdge(Edge, true);
		}
	}

	if (FBevelLinear::VisibleFace == -1 || FBevelLinear::VisibleFace == Index)
	{
		Bevel->FillEdge(Curr, false);
	}


	// Stick together parts of contour (it will be broken with removing parts)
	Curr->PathPrev.Last(0) = Prev->Next->PathPrev.Last(0);


	// Remove parts
	for (FPart* Point = Curr; Point != Prev->Next; Point = Point->Next)
	{
		for (const FPart* Edge = Prev->Next; Edge != Curr; Edge = Edge->Next)
		{
			Point->AvailableExpandsFar.Remove(Edge);
		}
	}

	for (const FPart* Part = Prev->Next->Next; Part != Curr->Next; Part = Part->Next)
	{
		delete Part->Prev;
	}

	Contour.RemoveRange(Prev->Next, Curr);


	// Finish sticking together
	Prev->Next = Curr;
	Curr->Prev = Prev;


	// If vertices were not welded, using simplier implementation
	const bool WeldedVertices = Count > 2;
	if (WeldedVertices)
	{
		Prev->ComputeTangentX();
		Curr->ComputeTangentX();

		Prev->ComputeNormalAndSmooth();
		Next->ComputeNormalAndSmooth();
	}

	Curr->ComputeNormalAndSmooth();

	if (WeldedVertices)
	{
		Prev->ComputeInitialPosition();
		Next->ComputeInitialPosition();
	}
	Curr->ComputeInitialPosition();

	// Update AvailableExpandsNear
	if (WeldedVertices)
	{
		Contour.ComputeAvailableExpandNear(Prev->Prev);
	}
	Contour.ComputeAvailableExpandNear(Prev);
	Contour.ComputeAvailableExpandNear(Curr);

	if (WeldedVertices)
	{
		Contour.ComputeAvailableExpandNear(Next);
	}
	else
	{
		Next->AvailableExpandNear -= NextDelta;
	}


	// Update AvailableExpandsFar
	if (WeldedVertices)
	{
		Contour.ComputeAvailableExpandsFarFrom(Prev);
		Contour.ComputeAvailableExpandsFarFrom(Next);
	}
	else
	{
		Prev->AvailableExpandsFar.Remove(Curr);
		Next->AvailableExpandsFar.Remove(Prev);

		Prev->DecreaseExpandsFar(PrevDelta);
		Next->DecreaseExpandsFar(NextDelta);
	}
	Contour.ComputeAvailableExpandsFarFrom(Curr);

	if (WeldedVertices)
	{
		Contour.ComputeAvailableExpandsFarTo(Prev->Prev);
		Contour.ComputeAvailableExpandsFarTo(Next);
	}
	Contour.ComputeAvailableExpandsFarTo(Prev);
	Contour.ComputeAvailableExpandsFarTo(Curr);
}


FIntersectionFar::FIntersectionFar(FBevelLinear* const Bevel, FContour* const ContourIn)
	: FIntersection(Bevel, ContourIn)
	, SplitEdge(nullptr)
{
	for (FPart* Point : Contour)
	{
		for (TPair<FPart *, float>& ExpandFar : Point->AvailableExpandsFar)
		{
			if (ContourHasCloserIntersectionAt(Point, ExpandFar.Value))
			{
				// Also store edge
				SplitEdge = ExpandFar.Key;
			}
		}
	}
}

void FIntersectionFar::BevelTillThis()
{
	FPart* const Curr = GetVertex();
	FPart* const Prev = Curr->Prev;
	FPart* const Next = Curr->Next;

	// Previous and next points of edge
	FPart* const EdgeA = SplitEdge;
	FPart* const EdgeB = EdgeA->Next;

	const TSharedPtr<FData> Data = Bevel->GetData();
	// Record last index, it will be removed in FBevelLinear::ExpandPoint and will be needed later
	const int32 EdgeALast = EdgeA->PathNext.Last(0);


	// Create vertices
	Bevel->ExpandPoint(Prev, 2);
	Bevel->ExpandPoint(Curr, 2);
	Bevel->ExpandPoint(Next, 2);
	Bevel->ExpandPoint(EdgeA, 2);
	Bevel->ExpandPoint(EdgeB, 2);

	const int32 Intersection = Data->AddVertices(1);
	Data->AddVertex(Curr, EdgeA->TangentX, Data->ComputeTangentZ(EdgeA, Curr->DoneExpand));


	const float PrevDelta = GetValue() - Prev->DoneExpand;
	const float NextDelta = GetValue() - Next->DoneExpand;
	const float EdgeADelta = GetValue() - EdgeA->DoneExpand;
	const float EdgeBDelta = GetValue() - EdgeB->DoneExpand;


	// Create triangles
	Bevel->FillEdge(Prev, false);
	Bevel->FillEdge(Curr, false);
	Bevel->FillEdge(EdgeA, true);

	Data->AddTriangles(2);

	Data->AddTriangle(EdgeALast, EdgeA->PathNext.Last(0), Intersection);
	Data->AddTriangle(EdgeALast, Intersection, EdgeB->PathPrev.Last(0));


	// Split contour to 2 contours
	FContour& Initial = Contour;
	FContour& Added = Bevel->AddContour();


	// Copy parts from initial to added contour
	for (FPart* Part = EdgeB; Part != Curr; Part = Part->Next)
	{
		Added.Add(Part);
	}

	// Make a copy of point that split initial contour, it's needed in added contour
	FPart* const Copy = new FPart;
	Added.Add(Copy);
	// Remove copied parts
	Initial.RemoveRange(EdgeB, Curr);

	Copy->Position = Curr->Position;


	// Stick together initial contour
	EdgeA->Next = Curr;
	Curr->Prev = EdgeA;

	// Stick together added contour
	Prev->Next = Copy;
	Copy->Prev = Prev;
	Copy->Next = EdgeB;
	EdgeB->Prev = Copy;


	Copy->DoneExpand = GetValue();
	Copy->TangentX = EdgeA->TangentX;

	Curr->ComputeNormalAndSmooth();
	Copy->ComputeNormalAndSmooth();

	Curr->ComputeInitialPosition();
	Copy->ComputeInitialPosition();


	// Update AvailableExpandsNear
	Initial.ComputeAvailableExpandNear(EdgeA);
	Initial.ComputeAvailableExpandNear(Curr);
	Next->AvailableExpandNear -= NextDelta;

	Added.ComputeAvailableExpandNear(Prev);
	Added.ComputeAvailableExpandNear(Copy);
	EdgeB->AvailableExpandNear -= EdgeBDelta;


	// Finish sticking together added contour
	Copy->PathPrev.Add(Curr->PathPrev.Last(0));
	Copy->PathNext.Add(Intersection);

	Curr->PathPrev.Last(0) = Intersection;


	// Update AvailableExpandsFar
	UpdateExpandsFar(&Initial, Added, Curr, EdgeA, EdgeADelta, NextDelta);
	UpdateExpandsFar(&Added, Initial, Copy, Copy, PrevDelta, EdgeBDelta);
}

void FIntersectionFar::UpdateExpandsFar(FContour* const UpdatedContour, const FContour& OtherContour, FPart* const Curr, const FPart* const SplitEdgePart, const float PrevDelta, const float NextDelta)
{
	// If contour shrinked too much, IntersectionFar is not possible
	if (UpdatedContour->Num() < MinContourSizeForIntersectionFar)
	{
		for (FPart* const Point : *UpdatedContour)
		{
			Point->AvailableExpandsFar.Empty();
		}
	}
	else
	{
		// Remove references to edges of OtherContour from points of UpdatedContour
		for (FPart* const Point : *UpdatedContour)
		{
			for (const FPart* const Edge : OtherContour)
			{
				Point->AvailableExpandsFar.Remove(Edge);
			}
		}

		FPart* const Prev = Curr->Prev;
		FPart* const Next = Curr->Next;

		Prev->AvailableExpandsFar.Remove(Curr);
		Next->AvailableExpandsFar.Remove(Prev);

		UpdatedContour->ComputeAvailableExpandsFarFrom(Curr);
		UpdatedContour->ComputeAvailableExpandsFarTo(SplitEdgePart);

		Prev->DecreaseExpandsFar(PrevDelta);
		Next->DecreaseExpandsFar(NextDelta);
	}
}
