// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MeshDescription.h"

class FDynamicMesh3;


/**
 * These attributes are used to store custom modeling tools data on a MeshDescription
 */
namespace ExtendedMeshAttribute
{
	extern MESHCONVERSION_API const FName PolyTriGroups;
}


/**
 * Utility class to construct MeshDescription instances
 */
class MESHCONVERSION_API FMeshDescriptionBuilder
{
public:
	void SetMeshDescription(FMeshDescription* Description);

	/** Append vertex and return new vertex ID */
	FVertexID AppendVertex(const FVector& Position);

	/** Return position of vertex */
	FVector GetPosition(const FVertexID& VertexID);

	/** Return position of vertex parent of instance */
	FVector GetPosition(const FVertexInstanceID& InstanceID);

	/** Set the position of a vertex */
	void SetPosition(const FVertexID& VertexID, const FVector& NewPosition);



	/** Append new vertex instance and return ID */
	FVertexInstanceID AppendInstance(const FVertexID& VertexID);

	/** Set the UV and Normal of a vertex instance*/
	void SetInstance(const FVertexInstanceID& InstanceID, const FVector2D& InstanceUV, const FVector& InstanceNormal);

	/** Set the Normal of a vertex instance*/
	void SetInstanceNormal(const FVertexInstanceID& InstanceID, const FVector& Normal);

	/** Set the UV of a vertex instance */
	void SetInstanceUV(const FVertexInstanceID& InstanceID, const FVector2D& InstanceUV, int32 UVLayerIndex = 0);

	/** Set the number of UV layers */
	void SetNumUVLayers(int32 NumUVLayers);

	/** Set the Color of a vertex instance*/
	void SetInstanceColor(const FVertexInstanceID& InstanceID, const FVector4& Color);

	/** Enable per-triangle integer attribute named PolyTriGroups */
	void EnablePolyGroups();

	/** Create a new polygon group and return it's ID */
	FPolygonGroupID AppendPolygonGroup();

	/** Set the PolyTriGroups attribute value to a specific GroupID for a Polygon */
	void SetPolyGroupID(const FPolygonID& PolygonID, int GroupID);



	/** Append a triangle to the mesh with the given PolygonGroup ID */
	FPolygonID AppendTriangle(const FVertexID& Vertex0, const FVertexID& Vertex1, const FVertexID& Vertex2, const FPolygonGroupID& PolygonGroup);

	/** Append a triangle to the mesh with the given PolygonGroup ID, and optionally with triangle-vertex UVs and Normals */
	FPolygonID AppendTriangle(const FVertexID* Triangle, const FPolygonGroupID& PolygonGroup, 
		const FVector2D* VertexUVs = nullptr, const FVector* VertexNormals = nullptr);

	/** 
	 * Append an arbitrary polygon to the mesh with the given PolygonGroup ID, and optionally with polygon-vertex UVs and Normals
	 * Unique Vertex instances will be created for each polygon-vertex.
	 */
	FPolygonID AppendPolygon(const TArray<FVertexID>& Vertices, const FPolygonGroupID& PolygonGroup, 
		const TArray<FVector2D>* VertexUVs = nullptr, const TArray<FVector>* VertexNormals = nullptr);

	/**
	 * Append a triangle to the mesh using the given vertex instances and PolygonGroup ID
	 */
	FPolygonID AppendTriangle(const FVertexInstanceID& Instance0, const FVertexInstanceID& Instance1, const FVertexInstanceID& Instance2, const FPolygonGroupID& PolygonGroup);



	/** Set MeshAttribute::Edge::IsHard to true for all edges */
	void SetAllEdgesHardness(bool bHard);

	/** Translate the MeshDescription vertex positions */
	void Translate(const FVector& Translation);


	/** Return the current bounding box of the mesh */
	FBox ComputeBoundingBox() const;

protected:
	FMeshDescription* MeshDescription;

	TVertexAttributesRef<FVector> VertexPositions;
	TVertexInstanceAttributesRef<FVector2D> InstanceUVs;
	TVertexInstanceAttributesRef<FVector> InstanceNormals;
	TVertexInstanceAttributesRef<FVector4> InstanceColors;
	TArray<FVertexID> TempBuffer;
	TArray<FVector2D> UVBuffer;
	TArray<FVector> NormalBuffer;

	TPolygonAttributesRef<int> PolyGroups;
};