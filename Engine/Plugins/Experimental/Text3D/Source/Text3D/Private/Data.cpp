// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.


#include "Data.h"
#include "Bevel/Part.h"


FData::FData(TSharedPtr<TText3DMeshList> MeshesIn, const float ExpandTotalIn, const float FontInverseScaleIn, const FVector& ScaleIn)
	: Meshes(MeshesIn)

	, ExpandTotal(ExpandTotalIn / FontInverseScaleIn)

	, FontInverseScale(FontInverseScaleIn)
	, Scale(ScaleIn)
{
	check(Meshes.Get());
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

float FData::GetExpandTarget() const
{
	return ExpandTarget;
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

	const float t = Expand == 0 ? 0 : DoneExpand / Expand;
	const FVector2D Normal = NormalStart * (1 - t) + NormalEnd * t;

	return FVector(FVector2D(TangentX.Y, -TangentX.X) * Normal.X, Normal.Y);
}

void FData::SetCurrentMesh(EText3DMeshType Type)
{
	CurrentMesh = &((*Meshes.Get())[static_cast<int32>(Type)]);

	check(CurrentMesh);
	CurrentMesh->GlyphStartVertices.Add(CurrentMesh->Vertices.Num());
}

FVector FData::GetVector(const FVector2D Position, const float Height) const
{
	return (FVector(0, Position.X, Position.Y) * FontInverseScale + FVector(Height, HorizontalOffset, VerticalOffset)) * Scale;
}
