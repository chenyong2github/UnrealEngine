// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Util.h"
#include "Mesh.h"
#include "Glyph.h"
#include "CoreMinimal.h"
#include "DynamicMeshBuilder.h"

/** Used to add vertices and triangles from different classes */
class FData final
{
public:
	FData();

	void SetGlyph(TSharedPtr<FText3DGlyph> GlyphIn);

	/**
	 * Set offset once instead of specifying it for every vertex.
	 * @param ExpandTargetIn - Offset value.
	 */
	void SetExpandTarget(const float ExpandTargetIn);
	void SetMinBevelTarget();
	void SetMaxBevelTarget();

	int32 AddVertices(const int32 Count);
	void AddVertex(const FPartConstPtr Point, const FVector2D TangentX, const FVector& TangentZ, const FVector2D TextureCoordinates = {0.f, 0.f});
	void AddVertex(const FVector2D Position, const FVector2D TangentX, const FVector& TangentZ, const FVector2D TextureCoordinates = {0.f, 0.f});
	void AddVertex(const FVector& Position, const FVector& TangentX, const FVector& TangentZ, const FVector2D TextureCoordinates);

	void AddTriangles(const int32 Count);
	void AddTriangle(const int32 A, const int32 B, const int32 C);

	void SetExpandTotal(float ExpandTotal);
	float GetExpandTotal() const;

	float GetExtrude() const;
	void SetExtrude(const float ExtrudeIn);

	float GetExpand() const;
	void SetExpand(const float ExpandIn);

	void ResetDoneExtrude();
	void IncreaseDoneExtrude();

	void SetNormals(FVector2D Start, FVector2D End);
	FVector ComputeTangentZ(const FPartConstPtr Edge, const float DoneExpand);
	void SetCurrentGroup(EText3DGroupType Type);


	/**
	 * FPart::Expanded for total expand value Data::ExpandTarget.
	 * @param Point - Point for which position should be computed.
	 * @return Computed position.
	 */
	FVector2D Expanded(const FPartConstPtr Point) const;

	/**
	 * Make triangulation of edge along paths of it's vertices (from end of previous triangulation to result of points' expansion). Removes covered points' indices from paths.
	 * @param Edge - Edge that has to be filled.
	 * @param bSkipLastTriangle - Do not create last triangle (furthest from end of previous triangulation).
	 */
	void FillEdge(const FPartPtr Edge, const bool bSkipLastTriangle);

private:
	TSharedPtr<FText3DGlyph> Glyph;
	EText3DGroupType CurrentGroup;

	float Extrude;
	float Expand;
	float ExpandTotal;

	float CurrentExtrudeHeight;
	float ExpandTarget;
	float DoneExtrude;


	int32 VertexCountBeforeAdd;
	int32 AddVertexIndex;

	int32 TriangleCountBeforeAdd;
	int32 AddTriangleIndex;


	FVector2D NormalStart;
	FVector2D NormalEnd;


	/**
	 * Transform position from glyph coordinate system to 3d.
	 * @param Position - Point position.
	 * @param Height - Offset in direction orthogonal to surface of front cap.
	 * @return 3d coordinate.
	 */
	FVector GetVector(const FVector2D Position, const float Height) const;
	/**
	 * Make triangle fan, called from FData::FillEdge.
	 * @param Cap - Cap of triangle fan.
	 * @param Normal - Point, fan will be created along it's normal.
	 * @param bNormalIsCapNext - Normal is next point after cap or vice versa.
	 * @param bSkipLastTriangle - See FData::FillEdge.
	 */
	void MakeTriangleFanAlongNormal(const FPartConstPtr Cap, const FPartPtr Normal, const bool bNormalIsCapNext, const bool bSkipLastTriangle);
};
