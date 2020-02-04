// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "BevelType.h"
#include "Util.h"
#include "Mesh.h"


/** Makes actual bevel. */
class FMeshCreator final
{
public:
	FMeshCreator();

	/**
	 * Create meshes.
	 * @param ContoursIn - List of contours.
	 * @param Extrude - Orthogonal (to front cap) offset value.
	 * @param Bevel - Bevel value (bevel happens before extrude).
	 * @param Type - Defines shape of beveled part.
	 * @param BevelSegments - Segments count for Type == EText3DBevelType::HalfCircle.
	 */
	void CreateMeshes(const TSharedPtr<class FContourList> ContoursIn, const float Extrude, const float Bevel, const EText3DBevelType Type, const int32 BevelSegments);
	void SetFrontAndBevelTextureCoordinates(const float Bevel);
	void MirrorGroups(const float Extrude);
	void BuildMesh(UStaticMesh* StaticMesh, class UMaterial* DefaultMaterial);

private:
	TSharedRef<class FData> Data;
	TSharedPtr<class FText3DGlyph> Glyph;
	TSharedPtr<class FContourList> Contours;


	/**
	 * Create 'Front' part of glyph.
	 */
	void CreateFrontMesh();
	/**
	 * Create 'Bevel' part of glyph (actually half of it, will be mirrored later).
	 * @param Bevel - Bevel value (bevel happens before extrude).
	 * @param Type - Defines shape of beveled part.
	 * @param BevelSegments - Segments count for Type == EText3DBevelType::HalfCircle.
	 */
	void CreateBevelMesh(const float Bevel, const EText3DBevelType Type, const int32 BevelSegments);
	/**
	 * Create 'Extrude' part of glyph.
	 * @param Extrude - Extrude value.
	 * @param Bevel - Bevel value (bevel happens before extrude).
	 */
	void CreateExtrudeMesh(float Extrude, const float Bevel);

	void MirrorGroup(const EText3DGroupType TypeIn, const EText3DGroupType TypeOut, const float Extrude);


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
	 * Continue with trivial bevel till FData::Expand.
	 */
	void BevelPartsWithoutIntersectingNormals();

	/**
	 * Clear PathPrev and PathNext.
	 * @param Point - Point which paths should be cleared.
	 */
	void EmptyPaths(const FPartPtr Point) const;

	/**
	 * Same as previous function but does not cover the intersection-case.
	 * @param Point - Point that should be expanded.
	 * @param TextureCoordinates - Texcture coordinates of added vertices.
	 */
	void ExpandPoint(const FPartPtr Point, const FVector2D TextureCoordinates = FVector2D(0.f, 0.f));
	/**
	 * Common code for expanding, vertices are added uninitialized.
	 * @param Point - Expanded point.
	 */
	void ExpandPointWithoutAddingVertices(const FPartPtr Point) const;

	/**
	 * Add vertex for smooth point.
	 * @param Point - Expanded point.
	 * @param TextureCoordinates - Texture coordinates of added vertex.
	 */
	void AddVertexSmooth(const FPartConstPtr Point, const FVector2D TextureCoordinates);
	/**
	 * Add vertex for sharp point.
	 * @param Point - Expanded point.
	 * @param Edge - Edge from which TangentX and TangentZ will be assigned.
	 * @param TextureCoordinates - Texture coordinates of added vertex.
	 */
	void AddVertexSharp(const FPartConstPtr Point, const FPartConstPtr Edge, const FVector2D TextureCoordinates);
};
