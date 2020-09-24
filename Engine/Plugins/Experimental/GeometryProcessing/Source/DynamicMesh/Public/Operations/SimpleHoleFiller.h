// Copyright Epic Games, Inc. All Rights Reserved.

// Port of geometry3Sharp SimpleHoleFiller

#pragma once

#include "HoleFiller.h"
#include "MathUtil.h"
#include "VectorTypes.h"
#include "GeometryTypes.h"
#include "MeshRegionBoundaryLoops.h"


class FDynamicMesh3;


/**
 * Fill an EdgeLoop hole with triangles.
 * Supports two fill modes, either a fan connected to a new central vertex, or a triangulation of the boundary polygon
 */
class DYNAMICMESH_API FSimpleHoleFiller : public IHoleFiller
{
public:
	enum class EFillType
	{
		TriangleFan,
		PolygonEarClipping
	};

	//
	// Inputs
	//
	FDynamicMesh3 *Mesh;
	FEdgeLoop Loop;
	EFillType FillType = EFillType::TriangleFan;

	//
	// Outputs
	//
	int32 NewVertex = IndexConstants::InvalidID;

public:
	/**
	 *  Construct simple hole filler (just adds a central vertex and a triangle fan)
	 */
	FSimpleHoleFiller(FDynamicMesh3* Mesh, FEdgeLoop Loop, EFillType InFillType = EFillType::TriangleFan) : 
		Mesh(Mesh), 
		Loop(Loop), 
		FillType(InFillType)
	{
	}

	virtual ~FSimpleHoleFiller() {}
	
	
	/**
	 * @return EOperationValidationResult::Ok if we can apply operation, or error code if we cannot
	 */
	virtual EOperationValidationResult Validate()
	{
		if (!Loop.IsBoundaryLoop(Mesh))
		{
			return EOperationValidationResult::Failed_UnknownReason;
		}

		return EOperationValidationResult::Ok;
	}

	bool Fill(int32 GroupID = -1) override;	

	/**
	 * Updates the normals and UV's of NewTriangles. UV's are taken from VidUVMaps,
	 * which is an array of maps (1:1 with UV layers) that map vid's of vertices on the
	 * boundary to their UV elements and values. If the UV element for a vertex does not
	 * yet exist in the overlay, the corresponding element ID should be InvalidID. The 
	 * function will update it to point to the new element once it inserts it.

	 * Normals are shared among NewTriangles but not with the surrounding portions of the mesh.
	 *
	 * @param VidsToUVsMap A map from vertex ID's of the boundary vertices to UV element ID's
	 *  and/or their values. When the element ID is invalid, a new element is generated using
	 *  the value, and the map is updated accordingly.
	 *
	 * @returns false if there is an error, usually if VidsToUVsMap did not have an entry
	 *  for a needed vertex ID.
	 */
	bool UpdateAttributes(TArray<FMeshRegionBoundaryLoops::VidOverlayMap<FVector2f>>& VidUVMaps);


protected:
	bool Fill_Fan(int32 NewGroupID);
	bool Fill_EarClip(int32 NewGroupID);
};
