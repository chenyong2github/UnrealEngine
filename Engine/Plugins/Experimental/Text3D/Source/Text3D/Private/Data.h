// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once


#include "Bevel/Mesh.h"
#include "CoreMinimal.h"
#include "DynamicMeshBuilder.h"

struct FPart;


/** Used to add vertices and triangles from different classes */
class FData final
{
public:
	/**
	 * Contructor.
	 * @param MeshesIn - Vertices and indices to which bevel should be added (contains front cap).
	 * @param ExpandTotalIn - Total expand value (for all ArcSegments from Bevel.cpp).
	 * @param FontInverseScaleIn - Documented in Bevel.h.
	 * @param ScaleIn - Documented in Bevel.h.
	 */
	FData(TSharedPtr<TText3DMeshList> MeshesIn, const float ExpandTotalIn, const float FontInverseScaleIn, const FVector& ScaleIn);


	void SetHorizontalOffset(const float HorizontalOffsetIn);
	void SetVerticalOffset(const float VertricalOffsetIn);

	/**
	 * Set offset once instead of specifying it for every vertex.
	 * @param ExpandTargetIn - Offset value.
	 */
	void SetExpandTarget(const float ExpandTargetIn);
	void SetMinBevelTarget();
	void SetMaxBevelTarget();

	int32 AddVertices(const int32 Count);
	void AddVertex(const FPart* const Point, const FVector2D TangentX, const FVector& TangentZ, const FVector2D TextureCoordinates = {0, 0});
	void AddVertex(const FVector2D Position, const FVector2D TangentX, const FVector& TangentZ, const FVector2D TextureCoordinates = {0, 0});

	void AddTriangles(const int32 Count);
	void AddTriangle(const int32 A, const int32 B, const int32 C);


	float GetExpandTotal() const;

	float GetExtrude() const;
	void SetExtrude(const float ExtrudeIn);

	float GetExpand() const;
	void SetExpand(const float ExpandIn);

	float GetFontInverseScale() const;

	float GetExpandTarget() const;

	void ResetDoneExtrude();
	void IncreaseDoneExtrude();

	void SetNormals(FVector2D Start, FVector2D End);
	FVector ComputeTangentZ(const FPart* const Edge, const float DoneExpand);
	void SetCurrentMesh(EText3DMeshType Type);

private:
	TSharedPtr<TText3DMeshList> Meshes;
	FText3DDynamicData* CurrentMesh;

	const float ExpandTotal;

	float Extrude;
	float Expand;

	float HorizontalOffset;
	float VerticalOffset;

	const float FontInverseScale;
	const FVector Scale;

	int32 VertexCountBeforeAdd;
	int32 AddVertexIndex;
	float CurrentExtrudeHeight;
	float ExpandTarget;

	int32 IndicesCountBeforeAdd;
	int32 AddTriangleIndex;

	float DoneExtrude;

	FVector2D NormalStart;
	FVector2D NormalEnd;


	/**
	 * Transform position from glyph coordinate system to 3d.
	 * @param Position - Point position.
	 * @param Height - Offset in direction orthogonal to surface of front cap.
	 * @return 3d coordinate.
	 */
	FVector GetVector(const FVector2D Position, const float Height) const;
};
