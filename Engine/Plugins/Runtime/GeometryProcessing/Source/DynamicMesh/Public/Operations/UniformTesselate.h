// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GeometryTypes.h"

class FProgressCancel;

namespace UE
{
namespace Geometry
{

class FDynamicMesh3;

/**
 * Given an input mesh and a tesselation level, this operator generates a new tesselated mesh where every triangle in 
 * the input mesh is uniformly subtriangulated into (TesselationNum + 1)^2 triangles. Per-vertex normals, uvs, 
 * colors and extended per-vertex attributes are linearly interpolated to the new verticies. Per-triangle group 
 * identifiers/materials and extended triangle attributes for the new triangles are inherited from the corresponding 
 * input mesh triangles they replaced. 
 * 
 * Note: Currently does not support interpolation of the GenericAttributes besides Skin Weights.
 *   
 *            o                                o         
 *           / \     TesselationNum = 2       / \        
 *          /   \                            /   \       
 *         /     \                          x-----x      
 *        /       \    FUniformTesselate   / \   / \     
 *       /         \    ------------->    /   \ /   \    
 *      /           \                    x-----*-----x   
 *     /             \                  / \   / \   / \  
 *    /               \                /   \ /   \ /   \ 
 *   o-----------------o              o-----x-----x-----o 
 */

class DYNAMICMESH_API FUniformTesselate
{
public:
	//
	// Inputs
	//
	
	/** The input mesh to be tesselated. */
	const FDynamicMesh3* Mesh = nullptr;

	/** Set this to be able to cancel running operation. */
	FProgressCancel* Progress = nullptr;

	/** Determines the number of triangles we generate. */
	int TesselationNum = 0;

	/** Should multi-threading be enabled. */
	bool bUseParallel = true;

	//
	// Output
	//

	/** The tesselated mesh. */
	FDynamicMesh3* ResultMesh;

public:
	FUniformTesselate(const FDynamicMesh3* Mesh, FDynamicMesh3* OutMesh) 
	: 
	Mesh(Mesh), ResultMesh(OutMesh)
	{
	}
	
	virtual ~FUniformTesselate() 
	{
	}

	/**
	 * @return EOperationValidationResult::Ok if we can apply operation, or error code if we cannot.
	 */
	virtual EOperationValidationResult Validate()
	{
		if (TesselationNum < 0 || Mesh == nullptr || ResultMesh == nullptr) 
		{
			return EOperationValidationResult::Failed_UnknownReason;    
		}

		return EOperationValidationResult::Ok;
	}

	/**
	 * Generate tesselated geometry.
	 * 
	 * @return true if the algorithm succeeds, false if it failed or was cancelled by the user.
	 */
	virtual bool Compute();

protected:

	/** If this returns true, abort computation. */
	virtual bool Cancelled();
};

} // end namespace UE::Geometry
} // end namespace UE
