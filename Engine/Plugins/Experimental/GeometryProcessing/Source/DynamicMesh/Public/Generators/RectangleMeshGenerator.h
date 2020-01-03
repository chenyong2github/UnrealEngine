// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MeshShapeGenerator.h"

/**
 * Generate planar rectangular mesh with variable number of subdivisions along width and height.
 * By default, center of rectangle is centered at (0,0,0) origin
 */
class DYNAMICMESH_API FRectangleMeshGenerator : public FMeshShapeGenerator
{
public:
	/** Rectangle will be translated so that center is at this point */
	FVector3d Origin;
	/** Normal vector of all vertices will be set to this value. Default is +Z axis. */
	FVector3f Normal;

	/** Width of rectangle */
	double Width;
	/** Height of rectangle */
	double Height;

	/** Number of vertices along Width axis */
	int WidthVertexCount;
	/** Number of vertices along Height axis */
	int HeightVertexCount;

	/** If true (default), UVs are scaled so that there is no stretching. If false, UVs are scaled to fill unit square */
	bool bScaleUVByAspectRatio = true;

	/** Specifices how 2D indices are mapped to 3D points. Default is (0,1) = (x,y,0). */ 
	FIndex2i IndicesMap;

public:
	FRectangleMeshGenerator();

	/** Generate the mesh */
	virtual FMeshShapeGenerator& Generate() override;

	/** Create vertex at position under IndicesMap, shifted to Origin*/
	virtual FVector3d MakeVertex(double x, double y)
	{
		FVector3d v(0, 0, 0);
		v[IndicesMap.A] = x;
		v[IndicesMap.B] = y;
		return Origin + v;
	}



};