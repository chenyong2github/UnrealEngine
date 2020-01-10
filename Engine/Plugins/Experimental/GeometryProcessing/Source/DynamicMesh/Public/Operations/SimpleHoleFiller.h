// Copyright Epic Games, Inc. All Rights Reserved.

// Port of geometry3Sharp SimpleHoleFiller

#pragma once

#include "MathUtil.h"
#include "VectorTypes.h"
#include "GeometryTypes.h"
#include "MeshBoundaryLoops.h"


class FDynamicMesh3;


/**
 * Fill an EdgeLoop hole with a triangle fan connected to a new vertex at the centroid
 */
class DYNAMICMESH_API FSimpleHoleFiller
{
public:
	//
	// Inputs
	//
	FDynamicMesh3 *Mesh;
	FEdgeLoop Loop;

	//
	// Outputs
	//
	int NewVertex;
	TArray<int> NewTriangles;

public:
	/**
	 *  Construct simple hole filler (just adds a central vertex and a triangle fan)
	 */
	FSimpleHoleFiller(FDynamicMesh3* Mesh, FEdgeLoop Loop) : Mesh(Mesh), Loop(Loop)
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

	virtual bool Fill(int GroupID = -1);	
};
