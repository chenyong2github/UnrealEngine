// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DynamicMesh3.h"
#include "DynamicMeshAttributeSet.h"


namespace UE
{
namespace Geometry
{

/**
 * Find the first FDynamicMeshPolygroupAttribute with the given FName in a the AttributeSet of a Mesh.
 * @return nullptr if no Polygroup layer is found
 */
DYNAMICMESH_API FDynamicMeshPolygroupAttribute* FindPolygroupLayerByName(FDynamicMesh3& Mesh, FName Name);

/**
 * @return index of Layer in Mesh AttributeSet, or -1 if not found
 */
DYNAMICMESH_API int32 FindPolygroupLayerIndex(FDynamicMesh3& Mesh, FDynamicMeshPolygroupAttribute* Layer);


}	// end namespace Geometry
}	// end namespace UE