// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.


#include "Bevel/BevelLinear.h"
#include "Bevel/Contour.h"
#include "Bevel/Part.h"
#include "Bevel/Intersection.h"
#include "Data.h"


THIRD_PARTY_INCLUDES_START
#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#include "Windows/AllowWindowsPlatformTypes.h"
#endif //PLATFORM_WINDOWS

#include "GL/glcorearb.h"
#include "FTVectoriser.h"
extern "C" {
#include <tessellate.h>
}

#if PLATFORM_WINDOWS
#include "Windows/HideWindowsPlatformTypes.h"
#endif //PLATFORM_WINDOWS
THIRD_PARTY_INCLUDES_END


int32 FBevelLinear::Iteration = 0;
int32 FBevelLinear::Iterations = 0;
int32 FBevelLinear::VisibleFace = 0;
bool FBevelLinear::bHidePrevious = false;


FBevelLinear::FBevelLinear(const TSharedPtr<FData> DataIn, const FTVectoriser& Vectoriser, const int32 IterationsIn, const bool bHidePreviousIn, const int32 Segments, const int32 VisibleFaceIn)
	: Data(DataIn)
{
	enum class EDebugContour : uint8
	{
		DebugNothing,
		DebugSegments,
		DebugIntersectionFar
	};

	const EDebugContour DebugContour = EDebugContour::DebugNothing;

	if (DebugContour == EDebugContour::DebugSegments && Segments <= 0)
	{
		return;
	}

	Iterations = IterationsIn;
	bHidePrevious = bHidePreviousIn;
	VisibleFace = VisibleFaceIn;

	Data->ResetDoneExtrude();

	switch (DebugContour)
	{
	case EDebugContour::DebugNothing:
		CreateContours(Vectoriser);
		break;

	case EDebugContour::DebugSegments:
		CreateDebugSegmentsContour(Segments);
		break;

	case EDebugContour::DebugIntersectionFar:
		CreateDebugIntersectionFarContour();
		break;

	default:
		break;
	}

	InitContours();
}

void FBevelLinear::BevelContours(const float Extrude, const float Expand, FVector2D NormalStart, FVector2D NormalEnd, const bool bSmooth, const int32 MarkedVertex)
{
	ResetContours(Extrude, Expand, NormalStart, NormalEnd);

	if (!bSmooth)
	{
		DuplicateContourVertices();
	}

	if(Expand > 0)
	{
		BevelPartsWithIntersectingNormals();
	}

	BevelPartsWithoutIntersectingNormals();

	MarkVertex(MarkedVertex);

	Data->IncreaseDoneExtrude();
}

void FBevelLinear::CreateExtrudeMesh(const float Extrude)
{
	Data->SetExtrude(Extrude);
	Data->SetExpand(0);

	const FVector2D Normal(1, 0);
	Data->SetNormals(Normal, Normal);

	for (FContour& Contour : Contours)
	{
		for (FPart* const Part : Contour)
		{
			Part->ResetDoneExpand();
		}
	}


	TArray<float> TextureCoordinateVs;

	auto EdgeLength = [](const FPart* const Edge)
	{
		return (Edge->Next->Position - Edge->Position).Size();
	};

	for (FContour& Contour : Contours)
	{
		// Compute TexCoord.V-s for each point
		TextureCoordinateVs.Reset(Contour.Num() - 1);
		TextureCoordinateVs.Add(EdgeLength(Contour[0]));

		for(int32 Index = 1; Index < Contour.Num() - 1; Index++)
		{
			TextureCoordinateVs.Add(TextureCoordinateVs[Index - 1] + EdgeLength(Contour[Index]));
		}


		const float ContourLength = TextureCoordinateVs.Last() + EdgeLength(Contour.Last());

		if (FMath::IsNearlyZero(ContourLength))
		{
			continue;
		}


		for (float& PointY : TextureCoordinateVs)
		{
			PointY /= ContourLength;
		}

		// Duplicate contour
		Data->SetMinBevelTarget();

		// First point in contour is processed separately
		{
			FPart* const Point = Contour[0];
			// It's set to sharp because we need 2 vertices with TexCoord.Y values 0 and 1 (for smooth points only one vertex is added)
			Point->bSmooth = false;
			EmptyPaths(Point);
			ExpandPointWithoutAddingVertices(Point);

			const FVector2D TexCoordPrev(0, 0);
			const FVector2D TexCoordCurr(0, 1);

			if (Point->bSmooth)
			{
				AddVertexSmooth(Point, TexCoordPrev);
				AddVertexSmooth(Point, TexCoordCurr);
			}
			else
			{
				AddVertexSharp(Point, Point->Prev, TexCoordPrev);
				AddVertexSharp(Point, Point, TexCoordCurr);
			}
		}

		for (int32 Index = 1; Index < Contour.Num(); Index++)
		{
			FPart* const Point = Contour[Index];
			EmptyPaths(Point);
			ExpandPoint(Point, {0, 1 - TextureCoordinateVs[Index - 1]});
		}


		// Add extruded vertices
		Data->SetMaxBevelTarget();

		// Similarly to duplicating vertices, first point is processed separately
		{
			FPart* const Point = Contour[0];
			ExpandPointWithoutAddingVertices(Point);

			const FVector2D TexCoordPrev(1, 0);
			const FVector2D TexCoordCurr(1, 1);

			if (Point->bSmooth)
			{
				AddVertexSmooth(Point, TexCoordPrev);
				AddVertexSmooth(Point, TexCoordCurr);
			}
			else
			{
				AddVertexSharp(Point, Point->Prev, TexCoordPrev);
				AddVertexSharp(Point, Point, TexCoordCurr);
			}
		}

		for (int32 Index = 1; Index < Contour.Num(); Index++)
		{
			ExpandPoint(Contour[Index], {1, 1 - TextureCoordinateVs[Index - 1]});
		}


		for (FPart* const Edge : Contour)
		{
			FillEdge(Edge, false);
		}
	}
}

// Using GLUtesselator to make triangulation of back cap
void FBevelLinear::CreateBackCap()
{
#if TEXT3D_WITH_INTERSECTION
	if (Contours.Num() == 0)
	{
		return;
	}

	int32 VerticesCount = 0;

	for (const FContour& Contour : Contours)
	{
		VerticesCount += Contour.Num();
	}

	const TUniquePtr<double[]> Vertices = MakeUnique<double[]>(SIZE_T(VerticesCount) * 2);
	Data->SetMinBevelTarget();
	const int FirstAdded = Data->AddVertices(VerticesCount);

	const TUniquePtr<const double* []> ContoursIn = MakeUnique<const double* []>(SIZE_T(Contours.Num()) + 1);
	ContoursIn[0] = Vertices.Get();

	int32 Offset = 0;
	int32 ContourIndex = 0;
	for (const FContour& Contour : Contours)
	{
		for (int32 j = 0; j < Contour.Num(); j++)
		{
			const FPart* const Point = Contour[j];
			double* const Vertex = Vertices.Get() + (Offset + j) * 2;
			const FVector2D Position = Point->Position;

			Vertex[0] = double(Position.X);
			Vertex[1] = double(Position.Y);

			Data->AddVertex(Point, {1, 0}, {0, 0, 1});
		}

		Offset += Contour.Num();
		ContoursIn[SIZE_T(ContourIndex++ + 1)] = Vertices.Get() + Offset * 2;
	}

	double* VerticesOut = nullptr;
	int* IndicesOut = nullptr;
	int VerticesCountOut = 0;
	int TrianglesCountOut = 0;

	// @third party code - BEGIN GLUtesselator tesselate
	// Used to tesselate mesh framed with given contours
	tessellate(&VerticesOut, &VerticesCountOut, &IndicesOut, &TrianglesCountOut, static_cast<const double**>(ContoursIn.Get()), static_cast<const double**>(ContoursIn.Get() + ContourIndex + 1));
	//@third party code - END GLUtesselator tesselate

	Data->AddTriangles(TrianglesCountOut);

	for (int i = 0; i < TrianglesCountOut; i++)
	{
		const int* const t = IndicesOut + i * 3;
		Data->AddTriangle(FirstAdded + t[0], FirstAdded + t[2], FirstAdded + t[1]);
	}

	free(IndicesOut);
	free(VerticesOut);
#endif
}

FContour& FBevelLinear::AddContour()
{
	Contours.AddTail(FContour(this));
	return Contours.GetTail()->GetValue();
}

void FBevelLinear::RemoveContour(const FContour& Contour)
{
	// Search with comparing pointers
	for (TDoubleLinkedList<FContour>::TDoubleLinkedListNode* i = Contours.GetHead(); i; i = i->GetNextNode())
	{
		if (&i->GetValue() == &Contour)
		{
			Contours.RemoveNode(i);
			break;
		}
	}
}

TSharedPtr<FData> FBevelLinear::GetData()
{
	return Data;
}

FVector2D FBevelLinear::Expanded(const FPart* const Point) const
{
	// Needed expand value is difference of total expand and point's done expand
	return Point->Expanded(Data->GetExpandTarget() - Point->DoneExpand);
}

void FBevelLinear::ExpandPoint(FPart* const Point, const int32 Count)
{
	Point->Position = Expanded(Point);


	// Find first previous point that expands to other position
	const FPart* const Curr = Point;

	FPart* Prev = Point;
	for (int32 Index = 0; Index < Count - 1; Index++)
	{
		Prev = Prev->Prev;
	}

	// Find first next point that expands to other position
	const FPart* const Next = Point->Next;


	int32 v = Data->AddVertices(1);
	FPart* p = Prev->Next;

	auto PushNext = [&p, &v]()
	{
		// If point is smooth, only one vertex is needed
		if(!p->bSmooth)
			v++;

		p->PathNext.Add(v);
	};

	// Write indices to paths before creating them
	p->PathPrev.Add(v);

	for (; p != Curr; p = p->Next)
	{
		PushNext();
		p->Next->PathPrev.Add(v);
	}

	PushNext();


	FVector2D TangentX = Prev->TangentX;
	FVector TangentZ = Data->ComputeTangentZ(Prev, Point->DoneExpand);

	const TSharedPtr<FData> DataLocal = Data;
	auto Add = [DataLocal, Point, &TangentX, &TangentZ]()
	{
		// Result tangent is normalized sum of tangents of all surfaces vertex belongs to
		DataLocal->AddVertex(Point, TangentX.GetSafeNormal(), TangentZ.GetSafeNormal());
	};

	for (p = Prev->Next; p != Next; p = p->Next)
	{
		if (p->bSmooth)
		{
			TangentX += p->TangentX;
			TangentZ += Data->ComputeTangentZ(p, Point->DoneExpand);
		}
		else
		{
			Add();
			Data->AddVertices(1);

			TangentX = p->TangentX;
			TangentZ = Data->ComputeTangentZ(p, Point->DoneExpand);
		}
	}

	Add();
}

void FBevelLinear::ExpandPoint(FPart* const Point, const FVector2D TextureCoordinates)
{
	ExpandPointWithoutAddingVertices(Point);

	if (Point->bSmooth)
	{
		AddVertexSmooth(Point, TextureCoordinates);
	}
	else
	{
		AddVertexSharp(Point, Point->Prev, TextureCoordinates);
		AddVertexSharp(Point, Point, TextureCoordinates);
	}
}

void FBevelLinear::FillEdge(FPart* const Edge, const bool bSkipLastTriangle)
{
	FPart* const EdgeA = Edge;
	FPart* const EdgeB = Edge->Next;

	MakeTriangleFanAlongNormal(EdgeB, EdgeA, false, true);
	MakeTriangleFanAlongNormal(EdgeA, EdgeB, true, false);

	if (!bSkipLastTriangle)
	{
		MakeTriangleFanAlongNormal(EdgeB, EdgeA, false, false);
	}
	else
	{
		// Index has to be removed despite last triangle being skipped.
		// For example when normals intersect and result of expansion of EdgeA and EdgeB is one point - 
		// this point was already covered with last MakeTriangleFanAlongNormal call,
		// no need to keep it in neighbour point's path.
		EdgeA->PathNext.RemoveAt(0);
	}

	const float Target = Data->GetExpandTarget();

	// Write done expand
	EdgeA->DoneExpand = Target;
	EdgeB->DoneExpand = Target;
}

void FBevelLinear::CreateContours(const FTVectoriser& Vectoriser)
{
	for (size_t i = 0; i < Vectoriser.ContourCount(); i++)
	{
		const FTContour* const ContourIn = Vectoriser.Contour(i);
		const size_t PointCount = ContourIn->PointCount();

		if (PointCount < 3)
		{
			continue;
		}

		FContour& Contour = AddContour();
		Contour.AddUninitialized(PointCount);

		for (int32 j = 0; j < Contour.Num(); j++)
		{
			FPart* const Point = new FPart();
			Contour[j] = Point;

			// FTGL returns contours with clockwise order, changing it to counterclockwise
			const FTPoint& p = ContourIn->Point(Contour.Num() - 1 - j);
			Point->Position = FVector2D(p.X(), p.Y());
		}
	}
}

// Make square with segments in corner, like boolean difference of square and circle
void FBevelLinear::CreateDebugSegmentsContour(const int32 Segments)
{
	FContour& Contour = AddContour();

	auto Add = [&Contour](const FVector2D Position)
	{
		FPart* const Point = new FPart;
		Point->Position = Position;
		Contour.Add(Point);
	};

	const float Side = 4000;
	const float Gap = 2000;
	const float Corner = 2010;

	Add({Gap, 0});
	Add({Side, 0});
	Add({Side, Side});
	Add({0, Side});
	Add({0, Gap});

	const float Angle = HALF_PI / (Segments - 2);
	const float Alpha = PI - Angle;
	const float t = FMath::Tan(Alpha / 2);
	const float Radius = (Gap - Corner) * t + Corner;

	for (int32 Segment = 0; Segment < Segments - 1; Segment++)
	{
		const float A = HALF_PI - Segment * Angle;

		FVector2D Direction;
		FMath::SinCos(&Direction.Y, &Direction.X, A);

		Add(FVector2D(1.0f, 1.0f) * (Corner - Radius) + Direction * Radius);
	}
}

void FBevelLinear::CreateDebugIntersectionFarContour()
{
	FContour& Contour = AddContour();

	auto Add = [&Contour](const FVector2D Position)
	{
		FPart* const Point = new FPart;
		Point->Position = Position;
		Contour.Add(Point);
	};

	Add({0, 0});

	// Changing Extrude will move part of contour
	const float Offset = Data->GetExtrude() * 100;

	Add({3000 + Offset, 0});
	Add({3000 + Offset, 200});
	Add({2800 + Offset, 400});
	Add({2900 + Offset, 200});
	Add({2400 + Offset, 786.2f});
	Add({1900 + Offset, 200});
	Add({2000 + Offset, 400});
	Add({1800 + Offset, 200});

	Add({200, 200});
	Add({200, 2800});

	Add({3300, 2800});
	Add({3300, 2200});
	Add({4500, 2200});
	Add({4500, 2750});
	Add({4550, 2800});
	Add({4900, 2800});
	Add({4900, 3000});

	Add({0, 3000});
}

void FBevelLinear::InitContours()
{
	TArray<FContour*> BadContours;
	bool bContourIsBad = false;

	auto AddToBadContours = [&bContourIsBad, &BadContours](FContour* const BadContour)
	{
		bContourIsBad = true;
		BadContours.Add(BadContour);
	};

	for (FContour& Contour : Contours)
	{
		for (int32 Index = 0; Index < Contour.Num(); Index++)
		{
			FPart* const Point = Contour[Index];

			Point->Prev = Contour[Contour.GetPrev(Index)];
			Point->Next = Contour[Contour.GetNext(Index)];

			Point->ResetDoneExpand();
		}


		bContourIsBad = false;

		for (FPart* const Point : Contour)
		{
			Point->ComputeTangentX();

			if (Point->TangentX.IsZero())
			{
				AddToBadContours(&Contour);
				break;
			}
		}

		if (bContourIsBad)
		{
			continue;
		}

		for (FPart* const Point : Contour)
		{
			if (!Point->ComputeNormalAndSmooth())
			{
				AddToBadContours(&Contour);
				break;
			}

			Point->ResetInitialPosition();
		}

		if (bContourIsBad)
		{
			continue;
		}


		for (FPart* const Point : Contour)
		{
			Contour.ComputeAvailableExpandNear(Point);
		}

		if (Contour.Num() > FIntersection::MinContourSizeForIntersectionFar)
		{
			for (FPart* const Point : Contour)
			{
				Contour.ComputeAvailableExpandsFarFrom(Point);
			}
		}
	}

	for (FContour* const BadContour : BadContours)
	{
		RemoveContour(*BadContour);
	}
}

void FBevelLinear::DuplicateContourVertices()
{
	Data->SetMinBevelTarget();

	for (FContour& Contour : Contours)
	{
		for (FPart* const Point : Contour)
		{
			EmptyPaths(Point);
			// Duplicate points of contour (expansion with value 0)
			ExpandPoint(Point);
		}
	}
}

void FBevelLinear::ResetContours(const float Extrude, const float Expand, FVector2D NormalStart, FVector2D NormalEnd)
{
	Data->SetExtrude(Extrude);
	Data->SetExpand(Expand);

	Data->SetNormals(NormalStart, NormalEnd);

	for (FContour& Contour : Contours)
	{
		for (FPart* const Part : Contour)
		{
			Part->ResetDoneExpand();
			Part->ResetInitialPosition();
		}

		Contour.ResetContour();
	}
}

void FBevelLinear::BevelPartsWithIntersectingNormals()
{
#if TEXT3D_WITH_INTERSECTION
	for (Iteration = 0; /*Iteration < Iterations*/true; Iteration++)
	{
		// Copy list of contours (but not contours themselves) and iterate this list
		// because contours can be added or removed while beveling till intersections
		TArray<FContour*> ContoursCopy;

		for (FContour& Contour : Contours)
		{
			ContoursCopy.Add(&Contour);
		}

		bool bIntersectionsExisted = false;

		for (FContour* const Contour : ContoursCopy)
		{
			bIntersectionsExisted |= Contour->BevelTillClosestIntersection();
		}

		if (!bIntersectionsExisted)
		{
			break;
		}
	}
#endif
}

void FBevelLinear::BevelPartsWithoutIntersectingNormals()
{
	Data->SetMaxBevelTarget();
	const float MaxExpand = Data->GetExpand();

	for (FContour& Contour : Contours)
	{
		for (FPart* const Point : Contour)
		{
			if(Point->DoneExpand != MaxExpand || MaxExpand == 0)
			{
				ExpandPoint(Point);
			}

			const float Delta = MaxExpand - Point->DoneExpand;

			Point->AvailableExpandNear -= Delta;
			Point->DecreaseExpandsFar(Delta);
		}

		for (FPart* const Edge : Contour)
		{
			FillEdge(Edge, false);
		}
	}
}

void FBevelLinear::MarkVertex(const int32 MarkedVertex)
{
	if (MarkedVertex < 0)
	{
		return;
	}

	for (const FContour& Contour : Contours)
	{
		if (MarkedVertex >= Contour.Num())
		{
			continue;
		}

		const int32 d_firstAdded = Data->AddVertices(3);
		FPart d_point = *Contour[MarkedVertex];
		FVector2D& d_position = d_point.Position;

		Data->AddVertex(&d_point, {1, 0}, {0, 0, -1});

		d_position.X += 50.0f;
		Data->AddVertex(&d_point, {0, 1}, {0, 0, -1});

		d_position.Y += 50.0f;
		Data->AddVertex(&d_point, FVector2D(-1, -1).GetSafeNormal(), {0, 0, -1});

		Data->AddTriangles(1);
		Data->AddTriangle(d_firstAdded + 0, d_firstAdded + 1, d_firstAdded + 2);
	}
}

void FBevelLinear::MakeTriangleFanAlongNormal(const FPart* const Cap, FPart* const Normal, const bool bNormalIsCapNext, const bool bSkipLastTriangle)
{
	TArray<int32>& Path = bNormalIsCapNext ? Normal->PathPrev : Normal->PathNext;
	const int32 Count = Path.Num() - (bSkipLastTriangle ? 2 : 1);

	// Create triangles
	Data->AddTriangles(Count);

	for (int32 Index = 0; Index < Count; Index++)
	{
		Data->AddTriangle(	(bNormalIsCapNext ? Cap->PathNext : Cap->PathPrev)[0], 
							Path[bNormalIsCapNext ? Index + 1 : Index], 
							Path[bNormalIsCapNext ? Index : Index + 1]
						 );
	}

	// Remove covered vertices from path
	Path.RemoveAt(0, Count);
}

void FBevelLinear::EmptyPaths(FPart *const Point) const
{
	Point->PathPrev.Empty();
	Point->PathNext.Empty();
}

void FBevelLinear::ExpandPointWithoutAddingVertices(FPart* const Point) const
{
	Point->Position = Expanded(Point);
	const int32 FirstAdded = Data->AddVertices(Point->bSmooth ? 1 : 2);

	Point->PathPrev.Add(FirstAdded);
	Point->PathNext.Add(Point->bSmooth ? FirstAdded : FirstAdded + 1);
}

void FBevelLinear::AddVertexSmooth(const FPart *const Point, const FVector2D TextureCoordinates)
{
	const FPart* const Curr = Point;
	const FPart* const Prev = Point->Prev;

	Data->AddVertex(Point, (Prev->TangentX + Curr->TangentX).GetSafeNormal(), (Data->ComputeTangentZ(Prev, Point->DoneExpand) + Data->ComputeTangentZ(Curr, Point->DoneExpand)).GetSafeNormal(), TextureCoordinates);
}

void FBevelLinear::AddVertexSharp(const FPart *const Point, const FPart *const Edge, const FVector2D TextureCoordinates)
{
	Data->AddVertex(Point, Edge->TangentX, Data->ComputeTangentZ(Edge, Point->DoneExpand).GetSafeNormal(), TextureCoordinates);
}
