// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.


#include "Bevel/Data.h"
#include "Bevel/Part.h"


FData::FData(TText3DMeshList* MeshesIn, const float ExpandTotalIn, const float HorizontalOffsetIn, const float VerticalOffsetIn, const float FontInverseScaleIn, const FVector& ScaleIn)
	: Meshes(MeshesIn)

	, ExpandTotal(ExpandTotalIn / FontInverseScaleIn)

	, HorizontalOffset(HorizontalOffsetIn)
	, VerticalOffset(VerticalOffsetIn)

	, FontInverseScale(FontInverseScaleIn)
	, Scale(ScaleIn)

    , DoneExtrude(0)
{

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
	VertexCountBeforeAdd = CurrentMesh->Vertices.AddUninitialized(Count);
	AddVertexIndex = 0;
	return VertexCountBeforeAdd;
}

void FData::AddVertex(const FPart* const Point, const FVector2D TangentX, const FVector& TangentZ)
{
	CurrentMesh->Vertices[VertexCountBeforeAdd + AddVertexIndex++] = { GetVector(Point->Position, DoneExtrude + CurrentExtrudeHeight), FVector(0, TangentX.X, TangentX.Y), FVector(TangentZ.Z, TangentZ.X, TangentZ.Y), FVector2D(0, 0), FColor(255, 255, 255) };
}

void FData::AddTriangles(const int32 Count)
{
	if (Count > 0)
	{
		IndicesCountBeforeAdd = CurrentMesh->Indices.AddUninitialized(Count * 3);
		AddTriangleIndex = 0;
	}
}

void FData::AddTriangle(const int32 A, const int32 B, const int32 C)
{
	auto AddIndex = [this](const int32 Index)
	{
		CurrentMesh->Indices[IndicesCountBeforeAdd + AddTriangleIndex++] = Index;
	};

	AddIndex(A);
	AddIndex(B);
	AddIndex(C);
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

float FData::GetExpandTarget() const
{
	return ExpandTarget;
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

FVector FData::ComputeTangentZ(FPart* const Edge, float DoneExpand)
{
	const FVector2D TangentX = Edge->TangentX;

	const float t = Expand == 0 ? 0 : DoneExpand / Expand;
	const FVector2D Normal = NormalStart * (1 - t) + NormalEnd * t;

	return FVector(FVector2D(TangentX.Y, -TangentX.X) * Normal.X, Normal.Y);
}

void FData::SetCurrentMesh(EText3DMeshType Type)
{
	CurrentMesh = &((*Meshes)[static_cast<int32>(Type)]);
}

FVector FData::GetVector(const FVector2D Position, const float Height) const
{
	return (FVector(0, Position.X, Position.Y) * FontInverseScale + FVector(Height, HorizontalOffset, VerticalOffset)) * Scale;
}
