// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DynamicMesh/DynamicMesh3.h"

struct FToolBuilderState;
class UInteractiveToolStorableSelection;
class UToolTarget;


//
// Utility functions for Tool implementations to use to work with Stored Selections
//
namespace UE
{
namespace Geometry
{

	/**
	 * Given a FToolBuilderState, determine if a Stored Selection exists that is applicable to the ToolTarget
	 * @return Stored Selection if found, or nullptr if not
	 */
	MODELINGCOMPONENTS_API const UInteractiveToolStorableSelection* GetCurrentToolInputSelection(const FToolBuilderState& SceneState, UToolTarget* Target);

	/**
	 * Convert the given Seletion to a list of Triangles of the specified Mesh. 
	 * This will check the known variants of UInteractiveToolStorableSelection and convert their various selection types
	 * (eg group topology, UV island, etc) to triangle indices
	 * @return true if a list of triangles could be created.
	 */
	MODELINGCOMPONENTS_API bool GetStoredSelectionAsTriangles(
		const UInteractiveToolStorableSelection* Selection, 
		const FDynamicMesh3& Mesh,
		TArray<int32>& TrianglesOut );


}
}