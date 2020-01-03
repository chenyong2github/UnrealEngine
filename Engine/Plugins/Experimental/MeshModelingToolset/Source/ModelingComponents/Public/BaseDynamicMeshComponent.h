// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Components/MeshComponent.h"
#include "InteractiveToolObjects.h"
#include "Changes/MeshVertexChange.h"
#include "Changes/MeshChange.h"

#include "DynamicMesh3.h"

#include "BaseDynamicMeshComponent.generated.h"


// predecl
class FMeshVertexChange;
class FMeshChange;




/**
 * Tangent calculation modes
 */
UENUM()
enum class EDynamicMeshTangentCalcType : uint8
{
	/** Tangents are not used/available, proceed accordingly (eg generate arbitrary orthogonal basis) */
	NoTangents,
	/** Tangents should be automatically calculated on demand */
	AutoCalculated,
	/** Tangents are externally calculated (behavior undefined if they are not actually externally calculated!) */
	ExternallyCalculated
};


/**
 * UBaseDynamicMeshComponent is a base interface for a UMeshComponent based on a FDynamicMesh.
 * Currently no functionality lives here, only some interface functions are defined that various subclasses implement.
 */
UCLASS(hidecategories = (LOD, Physics, Collision), editinlinenew, ClassGroup = Rendering)
class MODELINGCOMPONENTS_API UBaseDynamicMeshComponent : public UMeshComponent, public IToolFrameworkComponent, public IMeshVertexCommandChangeTarget, public IMeshCommandChangeTarget
{
	GENERATED_UCLASS_BODY()

public:

	/**
	 * Call this if you update the mesh via GetMesh()
	 * @todo should provide a function that calls a lambda to modify the mesh, and only return const mesh pointer
	 */
	virtual void NotifyMeshUpdated()
	{
		unimplemented();
	}

	/**
	 * Apply a vertex deformation change to the internal mesh  (implements IMeshVertexCommandChangeTarget)
	 */
	virtual void ApplyChange(const FMeshVertexChange* Change, bool bRevert) override
	{
		unimplemented();
	}

	/**
	 * Apply a general mesh change to the internal mesh  (implements IMeshCommandChangeTarget)
	 */
	virtual void ApplyChange(const FMeshChange* Change, bool bRevert) override
	{
		unimplemented();
	}

};
