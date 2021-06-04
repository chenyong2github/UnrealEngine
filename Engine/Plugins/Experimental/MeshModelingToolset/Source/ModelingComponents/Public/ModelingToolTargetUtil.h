// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "TargetInterfaces/MaterialProvider.h" // FComponentMaterialSet

class UToolTarget;
class UPrimitiveComponent;
class AActor;
struct FMeshDescription;
struct FCreateMeshObjectParams;

//
// UE::ToolTarget:: namespace contains utility/helper functions for interacting with UToolTargets.
// Generally these are meant to be used by UInteractiveTools to handle standard tasks that would
// otherwise require each Tool to figure out things like which ToolTargetInterface to cast to, etc.
// Using these functions ideally avoids all the boilerplate inherent in the ToolTarget system.
// 
// However, the cost is that it is not necessarily the most efficient, as each one of these functions
// may potentially do many repeated Cast<>'s internally. So, use sparingly, or cache the outputs.
//
namespace UE
{
namespace ToolTarget
{

/**
 * @return the AActor backing a ToolTarget, or nullptr if there is no such Actor
 */
MODELINGCOMPONENTS_API AActor* GetTargetActor(UToolTarget* Target);

/**
 * @return the UPrimitiveComponent backing a ToolTarget, or nullptr if there is no such Component
 */
MODELINGCOMPONENTS_API UPrimitiveComponent* GetTargetComponent(UToolTarget* Target);

/**
 * Hide the "Source Object" (eg PrimitiveComponent, Actor, etc) backing a ToolTarget
 * @return true on success
 */
MODELINGCOMPONENTS_API bool HideSourceObject(UToolTarget* Target);

/**
 * Show the "Source Object" (eg PrimitiveComponent, Actor, etc) backing a ToolTarget
 * @return true on success
 */
MODELINGCOMPONENTS_API bool ShowSourceObject(UToolTarget* Target);

/**
 * @return the local-to-world Transform underlying a ToolTarget, eg the Component or Actor transform
 */
MODELINGCOMPONENTS_API UE::Geometry::FTransform3d GetLocalToWorldTransform(UToolTarget* Target);

/**
 * Fetch the Material Set on the object underlying a ToolTarget. In cases where there are (eg) 
 * separate Component and Asset material sets, prefers the Component material set
 * @param bPreferAssetMaterials if true, prefer an Asset material set, if available
 * @return a valid MaterialSet
 */
MODELINGCOMPONENTS_API FComponentMaterialSet GetMaterialSet(UToolTarget* Target, bool bPreferAssetMaterials = false);


/**
 * @return the MeshDescription underlying a ToolTarget, if it has such a mesh. May be generated internally by the ToolTarget. May be nullptr if the Target does not have a mesh.
 */
MODELINGCOMPONENTS_API const FMeshDescription* GetMeshDescription(UToolTarget* Target);

/**
 * Fetch a DynamicMesh3 representing the given ToolTarget. This may be a conersion of the output of GetMeshDescription().
 * This function returns a copy, so the caller can take ownership of this Mesh.
 * @return a created DynamicMesh3, which may be empty if the Target doesn't have a mesh 
 */
MODELINGCOMPONENTS_API UE::Geometry::FDynamicMesh3 GetDynamicMeshCopy(UToolTarget* Target);


/**
 * EDynamicMeshUpdateResult is returned by functions below that update a ToolTarget with a new Mesh
 */
enum class EDynamicMeshUpdateResult
{
	/** Update was successful */
	Ok = 0,
	/** Update failed */
	Failed = 1,
	/** Update was successful, but required that the entire target mesh be replaced, instead of a (requested) partial update */
	Ok_ForcedFullUpdate = 2
};


/**
 * Update the UV sets of the ToolTarget's mesh (assuming it has one) based on the provided UpdatedMesh.
 * @todo: support updating a specific UV set/index, rather than all sets
 * @return EDynamicMeshUpdateResult::Ok on success, or Ok_ForcedFullUpdate if any dependent mesh topology was modified
 */
MODELINGCOMPONENTS_API EDynamicMeshUpdateResult CommitDynamicMeshUVUpdate(UToolTarget* Target, const UE::Geometry::FDynamicMesh3* UpdatedMesh);


/**
 * FCreateMeshObjectParams::TypeHint is used by the ModelingObjectsCreationAPI to suggest what type of mesh object to create
 * inside various Tools. This should often be derived from the input mesh object type (eg if you plane-cut a Volume, the output
 * should be Volumes). This function interrogates the ToolTarget to try to determine this information
 * @return true if a known type was detected and configured in FCreateMeshObjectParams::TypeHint (and possibly FCreateMeshObjectParams::TypeHintClass)
 */
MODELINGCOMPONENTS_API bool ConfigureCreateMeshObjectParams(UToolTarget* SourceTarget, FCreateMeshObjectParams& DerivedParamsOut);




}  // end namespace ToolTarget
}  // end namespace UE