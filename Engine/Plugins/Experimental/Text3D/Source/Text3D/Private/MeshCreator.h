// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "BevelType.h"
#include "Mesh.h"

struct FPart;
class FData;
class FContourList;

/** Makes actual bevel. */
class FMeshCreator final
{
public:
	FMeshCreator(TSharedRef<TText3DMeshList> MeshesIn, const TSharedPtr<FData> DataIn);

	/**
	 * Create meshes.
	 * @param ContoursIn - List of contours.
	 * @param Extrude - Orthogonal (to front cap) offset value.
	 * @param Bevel - Bevel value (bevel happens before extrude).
	 * @param Type - Defines shape of beveled part.
	 * @param HalfCircleSegments - Segments count for Type == EText3DBevelType::HalfCircle.
	 */
	void CreateMeshes(const TSharedPtr<FContourList> ContoursIn, const float Extrude, const float Bevel, const EText3DBevelType Type, const int32 HalfCircleSegments);
	void SetFrontAndBevelTextureCoordinates(const float Bevel);
	void MirrorMeshes(const float Extrude, const float ScaleX);

private:
	TSharedRef<TText3DMeshList> Meshes;
	const TSharedPtr<FData> Data;
	TSharedPtr<FContourList> Contours;


	/**
	 * Create 'Front' part of glyph.
	 */
	void CreateFrontMesh();
	/**
	 *
	 */
	/**
	 * Create 'Bevel' part of glyph (actually half of it, will be mirrored later).
	 * @param Bevel - Bevel value (bevel happens before extrude).
	 * @param Type - Defines shape of beveled part.
	 * @param HalfCircleSegments - Segments count for Type == EText3DBevelType::HalfCircle.
	 */
	void CreateBevelMesh(const float Bevel, const EText3DBevelType Type, const int32 HalfCircleSegments);
	/**
	 * Create 'Extrude' part of glyph.
	 * @param Extrude - Extrude value.
	 * @param Bevel - Bevel value (bevel happens before extrude).
	 */
	void CreateExtrudeMesh(float Extrude, const float Bevel);

	void MirrorMesh(const EText3DMeshType TypeIn, const EText3DMeshType TypeOut, const float Extrude, const float ScaleX);


	/**
	 * Bevel one segment.
	 * @param Extrude - Documented in ContourList.h.
	 * @param Expand - Documented in ContourList.h.
	 * @param NormalStart - Normal at start of segment (minimum DoneExpand).
	 * @param NormalEnd - Normal at end of segment (maximum DoneExpand).
	 * @param bSmooth - Is angle between start of this segment and end of previous segment smooth?
	 */
	void BevelLinear(const float Extrude, const float Expand, FVector2D NormalStart, FVector2D NormalEnd, const bool bSmooth);

	/**
	 * Duplicate contour vertices (used to make sharp angle between bevel steps)
	 */
	void DuplicateContourVertices();

	/**
	 * Prepare for beveling (is executed before each step).
	 */
	void Reset(const float Extrude, const float Expand, FVector2D NormalStart, FVector2D NormalEnd);

	/**
	 * Make bevel only for non-trivial places.
	 */
	void BevelPartsWithIntersectingNormals();

	/**
	 * Continue with trivial bevel till FData::Expand.
	 */
	void BevelPartsWithoutIntersectingNormals();

	/**
	 * Clear PathPrev and PathNext.
	 * @param Point - Point which paths should be cleared.
	 */
	void EmptyPaths(FPart* const Point) const;

	/**
	 * Same as previous function but does not cover the intersection-case.
	 * @param Point - Point that should be expanded.
	 * @param TextureCoordinates - Texcture coordinates of added vertices.
	 */
	void ExpandPoint(FPart* const Point, const FVector2D TextureCoordinates = FVector2D(0, 0));
	/**
	 * Common code for expanding, vertices are added uninitialized.
	 * @param Point - Expanded point.
	 */
	void ExpandPointWithoutAddingVertices(FPart* const Point) const;

	/**
	 * Add vertex for smooth point.
	 * @param Point - Expanded point.
	 * @param TextureCoordinates - Texture coordinates of added vertex.
	 */
	void AddVertexSmooth(const FPart* const Point, const FVector2D TextureCoordinates);
	/**
	 * Add vertex for sharp point.
	 * @param Point - Expanded point.
	 * @param Edge - Edge from which TangentX and TangentZ will be assigned.
	 * @param TextureCoordinates - Texture coordinates of added vertex.
	 */
	void AddVertexSharp(const FPart* const Point, const FPart* const Edge, const FVector2D TextureCoordinates);

};
