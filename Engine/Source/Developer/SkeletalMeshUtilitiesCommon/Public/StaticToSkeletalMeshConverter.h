// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_EDITOR

#include "Math/MathFwd.h"
#include "UObject/NameTypes.h"
#include "UObject/ObjectMacros.h"


class UObject;
class USkeleton;
class UStaticMesh;
class USkeletalMesh;
struct FReferenceSkeleton;


struct SKELETALMESHUTILITIESCOMMON_API FStaticToSkeletalMeshConverter
{
	/** Creates a skeleton from a static mesh with a single root bone.
	 *
	 *  @param InStaticMesh The static mesh whose bounding box will be used as a reference.
	 *  @param InRelativeRootPosition The relative root position in a unit cube that gets scaled up to match the
	 *    bbox of the static mesh. For example, given FVector(0.5, 0.5, 0.0), the root bone location will be placed
	 *    at the center of the bottom of the static mesh's bbox.
	 *  NOTE: The owner upon return will be the transient package, re-outer to the correct package as needed. 
	 */
	static USkeleton* CreateSkeletonFromStaticMesh(
		UObject *InOuter,
		const FName InName,
		const EObjectFlags InFlags,
		const UStaticMesh* InStaticMesh,
		const FVector& InRelativeRootPosition 
		);

	/** Creates a skeleton from a static mesh with a bone chain going from root to end effector, where the intermediary
	 *  bones are distributed evenly along a line between the two.
	 *
	 *  @param InStaticMesh The static mesh whose bounding box will be used as a reference.
	 *  @param InRelativeRootPosition The relative root position in a unit cube that gets scaled up to match the
	 *    bbox of the static mesh. For example, given FVector(0.5, 0.5, 0.0), the root bone location will be placed
	 *    at the center of the bottom of the static mesh's bbox.
	 *  @param InRelativeEndEffectorPosition The end effector position, positioned in the same manner as the root
	 *    position. If the end effector is in the same location as the root, only the root bone is created.
	 *  @param InIntermediaryJointCount Number of joints to create between the root and the end effector.  
	 *  NOTE: The owner upon return will be the transient package, re-outer to the correct package as needed. 
	 */
	static USkeleton* CreateSkeletonFromStaticMesh(
		UObject *InOuter,
		const FName InName,
		const EObjectFlags InFlags,
		const UStaticMesh* InStaticMesh,
		const FVector& InRelativeRootPosition,
		const FVector& InRelativeEndEffectorPosition,
		const int32 InIntermediaryJointCount
		);

	/** Create a skeletal mesh from the given static mesh and a skeleton. The mesh will initially be created with a
	 *  rigid binding on the root bone. Use SetRigidBinding or SetSmoothBinding to override this given binding.
	 *  @param InOuter The skeletal mesh's outer/parent object.
	 *  @param InName The name to give the newly created skeletal mesh.
	 *  @param InFlags The object creation flags to use.
	 *  @param InStaticMesh The static mesh to convert from.
	 *  @param InReferenceSkeleton The reference skeleton to use.
	 *  @param InBindBone The bone to bind to. If no bone name is given, the binding defaults to the root bone.
	 */
	static USkeletalMesh* CreateSkeletalMeshFromStaticMesh(
		UObject *InOuter,
		const FName InName,
		const EObjectFlags InFlags,
		const UStaticMesh* InStaticMesh,
		const FReferenceSkeleton& InReferenceSkeleton,
		const FName InBindBone = NAME_None 
		);
};

#endif
