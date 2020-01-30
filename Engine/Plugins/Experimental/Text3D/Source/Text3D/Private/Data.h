// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once


#include "Mesh.h"
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
	 * @param FontInverseScaleIn - Documented in ContourList.h.
	 * @param ScaleIn - Documented in ContourList.h.
	 */
	FData(TSharedRef<TText3DMeshList> MeshesIn, const float ExpandTotalIn, const float FontInverseScaleIn, const FVector& ScaleIn);


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

	void ResetDoneExtrude();
	void IncreaseDoneExtrude();

	void SetNormals(FVector2D Start, FVector2D End);
	FVector ComputeTangentZ(const FPart* const Edge, const float DoneExpand);
	void SetCurrentMesh(EText3DMeshType Type);


	/**
	 * FPart::Expanded for total expand value Data::ExpandTarget.
	 * @param Point - Point for which position should be computed.
	 * @return Computed position.
	 */
	FVector2D Expanded(const FPart* const Point) const;
	/**
	 * Similar to FData::Expanded but actually creates vertices and writes indices to paths.
	 * @param Point - Point that should be expanded.
	 * @param Count - Amount of edges to which result point will belong. 2 for case without intersections, (n + 1) for case when (n) normals intersect in result point.
	 */
	void ExpandPoint(FPart* const Point, const int32 Count);
	/**
	 * Make triangulation of edge along paths of it's vertices (from end of previous triangulation to result of points' expansion). Removes covered points' indices from paths.
	 * @param Edge - Edge that has to be filled.
	 * @param bSkipLastTriangle - Do not create last triangle (furthest from end of previous triangulation).
	 */
	void FillEdge(FPart* const Edge, const bool bSkipLastTriangle);

private:
	TSharedRef<TText3DMeshList> Meshes;
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
	/**
	 * Make triangle fan, called from FData::FillEdge.
	 * @param Cap - Cap of triangle fan.
	 * @param Normal - Point, fan will be created along it's normal.
	 * @param bNormalIsCapNext - Normal is next point after cap or vice versa.
	 * @param bSkipLastTriangle - See FData::FillEdge.
	 */
	void MakeTriangleFanAlongNormal(const FPart* const Cap, FPart* const Normal, const bool bNormalIsCapNext, const bool bSkipLastTriangle);
};
