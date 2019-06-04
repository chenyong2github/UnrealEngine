// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DynamicMesh3.h"
#include "MeshDescription.h"

/**
 * Convert FDynamicMesh3 to FMeshDescription
 *
 */
class MESHCONVERSION_API FDynamicMeshToMeshDescription
{
public:
	/** If true, will print some possibly-helpful debugging spew to output log */
	bool bPrintDebugMessages = false;

	/** Should DynamicMesh triangle groups be transfered to MeshDescription via custom PolyTriGroups attribute */
	bool bSetPolyGroups = true;

	/**
	 * Default conversion of DynamicMesh to MeshDescription. Calls functions below depending on mesh state
	 */
	void Convert(const FDynamicMesh3* MeshIn, FMeshDescription& MeshOut);


	/**
	 * Update existing MeshDescription based on DynamicMesh. Assumes mesh topology has not changed.
	 * Copies positions, recalculates MeshDescription normals.
	 */
	void Update(const FDynamicMesh3* MeshIn, FMeshDescription& MeshOut);


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