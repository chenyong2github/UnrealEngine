// Copyright Epic Games, Inc. All Rights Reserved.


#include "Data.h"
#include "Part.h"


FData::FData(TSharedRef<TText3DMeshList> MeshesIn, const float ExpandTotalIn, const float FontInverseScaleIn, const FVector& ScaleIn)
	: Meshes(MeshesIn)

	, ExpandTotal(ExpandTotalIn / FontInverseScaleIn)

	, FontInverseScale(FontInverseScaleIn)
	, Scale(ScaleIn)
{
	CurrentMesh = nullptr;
	Extrude = 0.0f;
	Expand = 0.0f;
	HorizontalOffset = 0.0f;
	VerticalOffset = 0.0f;

	CurrentExtrudeHeight = 0.0f;
	ExpandTarget = 0.0f;
	DoneExtrude = 0.0f;

	VertexCountBeforeAdd = 0;
	AddVertexIndex = 0;

	IndicesCountBeforeAdd = 0;
	AddTriangleIndex = 0;
}

void FData::SetHorizontalOffset(const float HorizontalOffsetIn)
{
	HorizontalOffset = HorizontalOffsetIn;
}

void FData::SetVerticalOffset(const float VerticalOffsetIn)
{
	VerticalOffset = VerticalOffsetIn;
}

void FData::SetExpandTarget(const float ExpandTargetIn)
{
	ExpandTarget = ExpandTargetIn;
	CurrentExtrudeHeight = Extrude * ExpandTarget / Expand;
}

void FData::SetMinBevelTarget()
{
	ExpandTarget = 0;
	CurrentExtrudeHeight = 0;
}

void FData::SetMaxBevelTarget()
{
	ExpandTarget = Expand;
	CurrentExtrudeHeight = Extrude;
}

int32 FData::AddVertices(const int32 Count)
{
	check(CurrentMesh);

	VertexCountBeforeAdd = CurrentMesh->Vertices.AddUninitialized(Count);
	AddVertexIndex = 0;
	return VertexCountBeforeAdd;
}

void FData::AddVertex(const FPart* const Point, const FVector2D TangentX, const FVector& TangentZ, const FVector2D TextureCoordinates)
{
	AddVertex(Point->Position, TangentX, TangentZ, TextureCoordinates);
}

void FData::AddVertex(const FVector2D Position, const FVector2D TangentX, const FVector& TangentZ, const FVector2D TextureCoordinates)
{
	check(CurrentMesh);

	CurrentMesh->Vertices[VertexCountBeforeAdd + AddVertexIndex++] = { GetVector(Position, DoneExtrude + CurrentExtrudeHeight), FVector(0, TangentX.X, TangentX.Y), FVector(TangentZ.Z, TangentZ.X, TangentZ.Y), TextureCoordinates, FColor(255, 255, 255) };
}

void FData::AddTriangles(const int32 Count)
{
	check(CurrentMesh);

	if (Count > 0)
	{
		IndicesCountBeforeAdd = CurrentMesh->Indices.AddUninitialized(Count * 3);
		AddTriangleIndex = 0;
	}
}

void FData::AddTriangle(const int32 A, const int32 B, const int32 C)
{
	check(CurrentMesh);

	CurrentMesh->Indices[IndicesCountBeforeAdd + AddTriangleIndex++] = A;
	CurrentMesh->Indices[IndicesCountBeforeAdd + AddTriangleIndex++] = B;
	CurrentMesh->Indices[IndicesCountBeforeAdd + AddTriangleIndex++] = C;
}

float FData::GetExpandTotal() const
{
	return ExpandTotal;
}

float FData::GetExtrude() const
{
	return Extrude;
}

void FData::SetExtrude(const float ExtrudeIn)
{
	Extrude = ExtrudeIn;
}

float FData::GetExpand() const
{
	return Expand;
}

void FData::SetExpand(const float ExpandIn)
{
	Expand = ExpandIn / FontInverseScale;
}

float FData::GetFontInverseScale() const
{
	return FontInverseScale;
}

void FData::ResetDoneExtrude()
{
	DoneExtrude = 0;
}

void FData::IncreaseDoneExtrude()
{
	DoneExtrude += Extrude;
}

void FData::SetNormals(FVector2D Start, FVector2D End)
{
	NormalStart = Start;
	NormalEnd = End;
}

FVector FData::ComputeTangentZ(const FPart* const Edge, const float DoneExpand)
{
	const FVector2D TangentX = Edge->TangentX;

	const float t = FMath::IsNearlyZero(Expand) ? 0.0f : DoneExpand / Expand;
	const FVector2D Normal = NormalStart * (1 - t) + NormalEnd * t;

	return FVector(FVector2D(TangentX.Y, -TangentX.X) * Normal.X, Normal.Y);
}

void FData::SetCurrentMesh(EText3DMeshType Type)
{
	CurrentMesh = &(Meshes.Get()[static_cast<int32>(Type)]);

	check(CurrentMesh);
	CurrentMesh->GlyphStartVertices.Add(CurrentMesh->Vertices.Num());
}

FVector2D FData::Expanded(const FPart* const Point) const
{
	// Needed expand value is difference of total expand and point's done expand
	return Point->Expanded(ExpandTarget - Point->DoneExpand);
}

void FData::ExpandPoint(FPart* const Point, const int32 Count)
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


	int32 v = AddVertices(1);
	FPart* p = Prev->Next;

	auto PushNext = [&p, &v]()
	{
		// If point is smooth, only one vertex is needed
		if (!p->bSmooth)
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
	FVector TangentZ = ComputeTangentZ(Prev, Point->DoneExpand);

	for (p = Prev->Next; p != Next; p = p->Next)
	{
		if (p->bSmooth)
		{
			TangentX += p->TangentX;
			TangentZ += ComputeTangentZ(p, Point->DoneExpand);
		}
		else
		{
			AddVertex(Point, TangentX.GetSafeNormal(), TangentZ.GetSafeNormal());
			AddVertices(1);

			TangentX = p->TangentX;
			TangentZ = ComputeTangentZ(p, Point->DoneExpand);
		}
	}

	AddVertex(Point, TangentX.GetSafeNormal(), TangentZ.GetSafeNormal());
}

void FData::FillEdge(FPart* const Edge, const bool bSkipLastTriangle)
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

	// Write done expand
	EdgeA->DoneExpand = ExpandTarget;
	EdgeB->DoneExpand = ExpandTarget;
}

FVector FData::GetVector(const FVector2D Position, const float Height) const
{
	return (FVector(0, Position.X, Position.Y) * FontInverseScale + FVector(Height, HorizontalOffset, VerticalOffset)) * Scale;
}

void FData::MakeTriangleFanAlongNormal(const FPart* const Cap, FPart* const Normal, const bool bNormalIsCapNext, const bool bSkipLastTriangle)
{
	TArray<int32>& Path = bNormalIsCapNext ? Normal->PathPrev : Normal->PathNext;
	const int32 Count = Path.Num() - (bSkipLastTriangle ? 2 : 1);

	// Create triangles
	AddTriangles(Count);

	for (int32 Index = 0; Index < Count; Index++)
	{
		AddTriangle((bNormalIsCapNext ? Cap->PathNext : Cap->PathPrev)[0],
				Path[bNormalIsCapNext ? Index + 1 : Index],
				Path[bNormalIsCapNext ? Index : Index + 1]
			);
	}

	// Remove covered vertices from path
	Path.RemoveAt(0, Count);
}
