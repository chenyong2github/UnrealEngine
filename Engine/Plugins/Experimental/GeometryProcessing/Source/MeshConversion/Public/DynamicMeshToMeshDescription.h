// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DynamicMesh3.h"
#include "MeshDescription.h"
#include "MeshConversionOptions.h"

/**
 * Convert FDynamicMesh3 to FMeshDescription
 *
 */
class MESHCONVERSION_API FDynamicMeshToMeshDescription
{
public:
	/** If true, will print some possibly-helpful debugging spew to output log */
	bool bPrintDebugMessages = false;

	/** General settings for conversions to mesh description */
	FConversionToMeshDescriptionOptions ConversionOptions;

	FDynamicMeshToMeshDescription()
	{
	}

	FDynamicMeshToMeshDescription(FConversionToMeshDescriptionOptions ConversionOptions) : ConversionOptions(ConversionOptions)
	{
	}

	/**
	 * Default conversion of DynamicMesh to MeshDescription. Calls functions below depending on mesh state
	 */
	void Convert(const FDynamicMesh3* MeshIn, FMeshDescription& MeshOut);


	/**
	 * Update existing MeshDescription based on DynamicMesh. Assumes mesh topology has not changed.
	 * Copies positions and normals; does not update UVs
	 */
	void Update(const FDynamicMesh3* MeshIn, FMeshDescription& MeshOut);


	/**
	 * Update only attributes, assuming the mesh topology has not changed.  Does not touch positions.
	 *	NOTE: assumes the order of triangles in the MeshIn correspond to the ordering you'd get by iterating over polygons, then tris-in-polygons, on MeshOut
	 *		  This matches conversion currently used in MeshDescriptionToDynamicMesh.cpp, but if that changes we will need to change this function to match!
	 */
	void UpdateAttributes(const FDynamicMesh3* MeshIn, FMeshDescription& MeshOut, bool bUpdateNormals, bool bUpdateUVs);

	//
	// Internal functions that you can also call directly
	//

	/**
	 * Ignore any Attributes on input Mesh, calculate per-vertex normals and have MeshDescription compute tangents.
	 * One VertexInstance per input vertex is generated
	 */
	void Convert_NoAttributes(const FDynamicMesh3* MeshIn, FMeshDescription& MeshOut);

	/**
	 * Convert while minimizing VertexInstance count, IE new VertexInstances are only created 
	 * if a unique UV or Normal is required.
	 */
	void Convert_SharedInstances(const FDynamicMesh3* MeshIn, FMeshDescription& MeshOut);

	/**
	 * Convert with no shared VertexInstances. A new VertexInstance is created for
	 * each triangle vertex (ie corner). However vertex positions are shared.
	 */
	void Convert_NoSharedInstances(const FDynamicMesh3* MeshIn, FMeshDescription& MeshOut);
};