// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"    // FHitResult
#include "UObject/Interface.h"

#include "PrimitiveComponentBackedTarget.generated.h"

class UMaterialInterface;
class UPrimitiveComponent;
class AActor;
struct FTransform;

/**
 * FComponentMaterialSet is the set of materials assigned to a component (ie Material Slots on a StaticMesh)
 */
struct INTERACTIVETOOLSFRAMEWORK_API FComponentMaterialSet
{
	TArray<UMaterialInterface*> Materials;

	bool operator!=(const FComponentMaterialSet& Other) const
	{
		return Materials != Other.Materials;
	}
};

UINTERFACE()
class INTERACTIVETOOLSFRAMEWORK_API UPrimitiveComponentBackedTarget : public UInterface
{
	GENERATED_BODY()
};

class INTERACTIVETOOLSFRAMEWORK_API IPrimitiveComponentBackedTarget
{
	GENERATED_BODY()

public:

	/** @return the Component this is a Source for */
	virtual UPrimitiveComponent* GetOwnerComponent() const = 0;

	/** @return the Actor that owns this Component */
	virtual AActor* GetOwnerActor() const = 0;

	/**
	 * Set the visibility of the Component associated with this Source (ie to hide during Tool usage)
	 * @param bVisible desired visibility
	 */
	virtual void SetOwnerVisibility(bool bVisible) const = 0;

	/** @return number of material indices in use by this Component */
	virtual int32 GetNumMaterials() const = 0;

	/**
	 * Get pointer to a Material provided by this Source
	 * @param MaterialIndex index of the material
	 * @return MaterialInterface pointer, or null if MaterialIndex is invalid
	 */
	virtual UMaterialInterface* GetMaterial(int32 MaterialIndex) const = 0;

	/**
	 * Get material set provided by this source
	 * @param MaterialSetOut returned material set
	 */
	virtual void GetMaterialSet(FComponentMaterialSet& MaterialSetOut) const = 0;

	/**
	 * @return the transform on this component
	 * @todo Do we need to return a list of transforms here?
	 */
	virtual FTransform GetWorldTransform() const = 0;

	/**
	 * Compute ray intersection with the MeshDescription this Source is providing
	 * @param WorldRay ray in world space
	 * @param OutHit hit test data
	 * @return true if ray intersected Component
	 */
	virtual bool HitTestComponent(const FRay& WorldRay, FHitResult& OutHit) const = 0;


	// TODO: Should this be in a separate interface?
	/**
	 * Commit an update to the material set. This may generate a transaction.
	 * @param MaterialSet new list of materials
	 */
	virtual void CommitMaterialSetUpdate(const FComponentMaterialSet& MaterialSet) = 0;
};