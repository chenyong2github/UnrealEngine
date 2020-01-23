// Copyright Epic Games, Inc. All Rights Reserved.

#include "Data.h"
#include "Part.h"
#include "Text3DPrivate.h"

FData::FData() 
{
	CurrentGroup = EText3DGroupType::Front;
	Extrude = 0.0f;
	Expand = 0.0f;
	ExpandTotal = 0.0f;

	CurrentExtrudeHeight = 0.0f;
	ExpandTarget = 0.0f;
	DoneExtrude = 0.0f;

	VertexCountBeforeAdd = 0;
	AddVertexIndex = 0;

	TriangleCountBeforeAdd = 0;
	AddTriangleIndex = 0;
}

void FData::SetGlyph(TSharedPtr<FText3DGlyph> GlyphIn)
{
	Glyph = GlyphIn;
}

void FData::SetExpandTarget(const float ExpandTargetIn)
{
	ExpandTarget = ExpandTargetIn;
	CurrentExtrudeHeight = Extrude * ExpandTarget / Expand;
}

void FData::SetMinBevelTarget()
{
	ExpandTarget = 0.f;
	CurrentExtrudeHeight = 0.f;
}

void FData::SetMaxBevelTarget()
{
	ExpandTarget = Expand;
	CurrentExtrudeHeight = Extrude;
}

int32 FData::AddVertices(const int32 Count)
{
	check(Glyph.Get());
	if (Count <= 0)
	{
		return 0;
	}

	FMeshDescription& MeshDescription = Glyph->GetMeshDescription();
	FStaticMeshAttributes& MeshAttributes = Glyph->GetStaticMeshAttributes();
	VertexCountBeforeAdd = MeshDescription.Vertices().Num();

	MeshDescription.ReserveNewVertices(Count);
	MeshDescription.ReserveNewVertexInstances(Count);

	for (int32 Index = 0; Index < Count; Index++)
	{
		const FVertexID Vertex = MeshDescription.CreateVertex();
		const FVertexInstanceID VertexInstance = MeshDescription.CreateVertexInstance(Vertex);
		MeshAttributes.GetVertexInstanceColors()[VertexInstance] = FVector4(1, 0, 0, 1);
	}

	AddVertexIndex = 0;
	return VertexCountBeforeAdd;
}

void FData::AddVertex(const FPartConstPtr Point, const FVector2D TangentX, const FVector& TangentZ, const FVector2D TextureCoordinates)
{
	AddVertex(Point->Position, TangentX, TangentZ, TextureCoordinates);
}

void FData::AddVertex(const FVector2D Position, const FVector2D TangentX, const FVector& TangentZ, const FVector2D TextureCoordinates)
{
	AddVertex(GetVector(Position, DoneExtrude + CurrentExtrudeHeight), {0.f, TangentX.X, TangentX.Y}, TangentZ, TextureCoordinates);
}

void FData::AddVertex(const FVector& Position, const FVector& TangentX, const FVector& TangentZ, const FVector2D TextureCoordinates)
{
	check(Glyph.Get());
	FStaticMeshAttributes& StaticMeshAttributes = Glyph->GetStaticMeshAttributes();
	const int32 VertexIndex = VertexCountBeforeAdd + AddVertexIndex++;
	StaticMeshAttributes.GetVertexPositions()[FVertexID(VertexIndex)] = Position;
	const FVertexInstanceID Instance(static_cast<uint32>(VertexIndex));

	StaticMeshAttributes.GetVertexInstanceUVs()[Instance] = TextureCoordinates;
	StaticMeshAttributes.GetVertexInstanceNormals()[Instance] = TangentZ;
	StaticMeshAttributes.GetVertexInstanceTangents()[Instance] = TangentX;
}

void FData::AddTriangles(const int32 Count)
{
	check(Glyph.Get());
	if (Count <= 0)
	{
		return;
	}

	FMeshDescription& MeshDescription = Glyph->GetMeshDescription();
	TriangleCountBeforeAdd = MeshDescription.Triangles().Num();
	MeshDescription.ReserveNewTriangles(Count);
	AddTriangleIndex = 0;
}

void FData::AddTriangle(const int32 A, const int32 B, const int32 C)
{
	check(Glyph.Get());
	Glyph->GetMeshDescription().CreateTriangle(FPolygonGroupID(static_cast<int32>(CurrentGroup)), TArray<FVertexInstanceID>({FVertexInstanceID(A), FVertexInstanceID(B), FVertexInstanceID(C)}));
	AddTriangleIndex++;
}

void FData::SetExpandTotal(float ExpandTotalIn)
{
	ExpandTotal = ExpandTotalIn;
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

void FData::ResetDoneExtrude()
{
	DoneExtrude = 0.f;
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

FVector FData::ComputeTangentZ(const FPartConstPtr Edge, const float DoneExpand)
{
	const FVector2D TangentX = Edge->TangentX;

	const float t = FMath::IsNearlyZero(Expand) ? 0.0f : DoneExpand / Expand;
	const FVector2D Normal = NormalStart * (1.f - t) + NormalEnd * t;

	const FVector2D TangentZ_YZ = FVector2D(TangentX.Y, -TangentX.X) * Normal.X;
	return FVector(Normal.Y, TangentZ_YZ.X, TangentZ_YZ.Y);
}

void FData::SetCurrentGroup(EText3DGroupType Type)
{
	CurrentGroup = Type;
	check(Glyph.Get());

	FText3DPolygonGroup& Group = Glyph->GetGroups()[static_cast<int32>(Type)];
	FMeshDescription& MeshDescription = Glyph->GetMeshDescription();

	Group.FirstVertex = MeshDescription.Vertices().Num();
	Group.FirstTriangle = MeshDescription.Triangles().Num();
}

FVector2D FData::Expanded(const FPartConstPtr Point) const
{
	// Needed expand value is difference of total expand and point's done expand
	return Point->Expanded(ExpandTarget - Point->DoneExpand);
}

void FData::FillEdge(const FPartPtr Edge, const bool bSkipLastTriangle)
{
	const FPartPtr EdgeA = Edge;
	const FPartPtr EdgeB = Edge->Next;

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
	return (FVector(0.f, Position.X, Position.Y) * FontInverseScale + FVector(Height, 0.0f, 0.0f));
}

void FData::MakeTriangleFanAlongNormal(const FPartConstPtr Cap, const FPartPtr Normal, const bool bNormalIsCapNext, const bool bSkipLastTriangle)
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
