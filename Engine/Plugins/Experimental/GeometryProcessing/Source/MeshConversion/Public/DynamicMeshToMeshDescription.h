// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DynamicMesh3.h"
#include "MeshDescription.h"
#include "MeshConversionOptions.h"

// predeclare tangents template
template<typename RealType> class TMeshTangents;

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
	 * Checks if element counts match. If false then Update can't be called -- you must call Convert
	 *
	 * @param DynamicMesh The dynamic mesh with updated vertices or attributes
	 * @param MeshDescription The corresponding mesh description
	 * @param bVerticesOnly If true, only check vertex counts match
	 * @param bAttributesOnly If true, only check what needs to be checked for UpdateAttributes
							 (will check vertices or triangles depending on whether attributes are per vertex or in overlays)
	 */
	static bool HaveMatchingElementCounts(const FDynamicMesh3* DynamicMesh, const FMeshDescription* MeshDescription, bool bVerticesOnly, bool bAttributesOnly)
	{
		bool bVerticesMatch = DynamicMesh->IsCompactV() && DynamicMesh->VertexCount() == MeshDescription->Vertices().Num();
		bool bTrianglesMatch = DynamicMesh->IsCompactT() && DynamicMesh->TriangleCount() == MeshDescription->Triangles().Num();
		if (bVerticesOnly || (bAttributesOnly && !DynamicMesh->HasAttributes()))
		{
			return bVerticesMatch;
		}
		else if (bAttributesOnly && DynamicMesh->HasAttributes())
		{
			return bTrianglesMatch;
		}
		return bVerticesMatch && bTrianglesMatch;
	}

	/**
	 * Checks if element counts match. If false then Update can't be called -- you must call Convert
	 * Result is based on the current ConversionOptions (e.g. if you are only updating vertices, mismatched triangles are ok)
	 *
	 * @param DynamicMesh The dynamic mesh with updated vertices or attributes
	 * @param MeshDescription The corresponding mesh description
	 */
	bool HaveMatchingElementCounts(const FDynamicMesh3* DynamicMesh, const FMeshDescription* MeshDescription)
	{
		bool bUpdateAttributes = ConversionOptions.bUpdateNormals || ConversionOptions.bUpdateUVs;
		return HaveMatchingElementCounts(DynamicMesh, MeshDescription, !bUpdateAttributes, !ConversionOptions.bUpdatePositions);
	}

	/**
	 * Default conversion of DynamicMesh to MeshDescription. Calls functions below depending on mesh state
	 */
	void Convert(const FDynamicMesh3* MeshIn, FMeshDescription& MeshOut);


	/**
	 * Update existing MeshDescription based on DynamicMesh. Assumes mesh topology has not changed.
	 * Copies positions and normals; does not update UVs
	 */
	void Update(const FDynamicMesh3* MeshIn, FMeshDescription& MeshOut, bool bUpdateNormals = true, bool bUpdateUVs = false);


	/**
	 * Update only attributes, assuming the mesh topology has not changed.  Does not touch positions.
	 *	NOTE: assumes the order of triangles in the MeshIn correspond to the ordering you'd get by iterating over polygons, then tris-in-polygons, on MeshOut
	 *		  This matches conversion currently used in MeshDescriptionToDynamicMesh.cpp, but if that changes we will need to change this function to match!
	 */
	void UpdateAttributes(const FDynamicMesh3* MeshIn, FMeshDescription& MeshOut, bool bUpdateNormals, bool bUpdateUVs);

	/**
	 * Update the Tangent and BinormalSign attributes of the MeshDescription, assuming mesh topology has not changed. Does not modify any other attributes.
	 *	NOTE: assumes the order of triangles in the MeshIn correspond to the ordering you'd get by iterating over polygons, then tris-in-polygons, on MeshOut
	 *		  This matches conversion currently used in MeshDescriptionToDynamicMesh.cpp, but if that changes we will need to change this function to match!
	 */
	void UpdateTangents(const FDynamicMesh3* MeshIn, FMeshDescription& MeshOut, const TMeshTangents<double>* Tangents);

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