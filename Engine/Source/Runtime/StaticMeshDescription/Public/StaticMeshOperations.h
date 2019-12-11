// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MeshTypes.h"

struct FMeshDescription;

enum class EComputeNTBsFlags : uint32
{
	None = 0x00000000,	// No flags
	Normals = 0x00000001, //Compute the normals
	Tangents = 0x00000002, //Compute the tangents
	WeightedNTBs = 0x00000004, //Use weight angle when computing NTBs to proportionally distribute the vertex instance contribution to the normal/tangent/binormal in a smooth group.    i.e. Weight solve the cylinder problem
};
ENUM_CLASS_FLAGS(EComputeNTBsFlags);


class STATICMESHDESCRIPTION_API FStaticMeshOperations
{
public:

	/** Set the polygon tangent and normal only for the specified polygonIDs */
	static void ComputePolygonTangentsAndNormals(FMeshDescription& MeshDescription, TArrayView<const FPolygonID> PolygonIDs, float ComparisonThreshold = 0.0f);

	/** Set the polygon tangent and normal for all polygons in the mesh description. */
	static void ComputePolygonTangentsAndNormals(FMeshDescription& MeshDescription, float ComparisonThreshold = 0.0f);
	
	/** Set the vertex instance tangent and normal only for the specified VertexInstanceIDs */
	static void ComputeTangentsAndNormals(FMeshDescription& MeshDescription, TArrayView<const FVertexInstanceID> VertexInstanceIDs, EComputeNTBsFlags ComputeNTBsOptions);

	/** Set the vertex instance tangent and normal for all vertex instances in the mesh description. */
	static void ComputeTangentsAndNormals(FMeshDescription& MeshDescription, EComputeNTBsFlags ComputeNTBsOptions);

	/** Determine the edge hardnesses from existing normals */
	static void DetermineEdgeHardnessesFromVertexInstanceNormals(FMeshDescription& MeshDescription, float Tolerance = KINDA_SMALL_NUMBER);
};
