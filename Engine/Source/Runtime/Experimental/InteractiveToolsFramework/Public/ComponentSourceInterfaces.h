// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Math/UnrealMath.h"
#include "Misc/Optional.h"
#include "Templates/Function.h"
#include "Engine/EngineTypes.h"    // FHitResult

// predeclarations
class AActor;
class UActorComponent;
class UPrimitiveComponent;
struct FMeshDescription;
class UMaterialInterface;

/**
 * FComponentMaterialSet is the set of materials assigned to a component (ie Material Slots on a StaticMesh)
 */
struct INTERACTIVETOOLSFRAMEWORK_API FComponentMaterialSet
{
	TArray<UMaterialInterface*> Materials;
	bool operator!=(const FComponentMaterialSet& Other) const;
};


/**
 * Wrapper around a UObject Component that can provide a MeshDescription, and
 * (optionally) bake a modified MeshDescription back to this Component.
 * An example of a Source might be a StaticMeshComponent. How a modified
 * MeshDescription is committed back is context-dependent (in Editor vs PIE vs Runtime, etc).
 *
 * (Conceivably this doesn't have to be backed by a Component, but most usage will assume there is an Actor)
 */
class INTERACTIVETOOLSFRAMEWORK_API FPrimitiveComponentTarget
{
public:
	virtual ~FPrimitiveComponentTarget(){}

	/** Constructor UPrimitivecomponent*
	 *  @param Component the UPrimitiveComponent* to target
	 */
	FPrimitiveComponentTarget( UPrimitiveComponent* Component ): Component( Component ){}

	/** @return the Actor that owns this Component */
	AActor* GetOwnerActor() const;

	/** @return the Component this is a Source for */
	UPrimitiveComponent* GetOwnerComponent() const;

	/** @return number of material indices in use by this Component */
	int32 GetNumMaterials() const;

	/**
	 * Get pointer to a Material provided by this Source
	 * @param MaterialIndex index of the material
	 * @return MaterialInterface pointer, or null if MaterialIndex is invalid
	 */
	UMaterialInterface* GetMaterial(int32 MaterialIndex) const;

	/**
	 * Get material set provided by this source
	 * @param MaterialSetOut returned material set
	 * @param bAssetMaterials if an underlying asset exists, return the Asset-level material assignment instead of the component materials
	 */
	virtual void GetMaterialSet(FComponentMaterialSet& MaterialSetOut, bool bAssetMaterials = false) const;

	/**
	 * @return the transform on this component
	 * @todo Do we need to return a list of transforms here?
	 */
	FTransform GetWorldTransform() const;

	/**
	 * Compute ray intersection with the MeshDescription this Source is providing
	 * @param WorldRay ray in world space
	 * @param OutHit hit test data
	 * @return true if ray intersected Component
	 */
	bool HitTest(const FRay& WorldRay, FHitResult& OutHit) const;

	/**
	 * Set the visibility of the Component associated with this Source (ie to hide during Tool usage)
	 * @param bVisible desired visibility
	 */
	void SetOwnerVisibility(bool bVisible) const;


	/**
	 * Commit an update to the material set. This may generate a transaction.
	 * @param MaterialSet new list of materials
	 * @param bApplyToAsset if true, materials of Asset are updated (if Asset exists), rather than Component
	 */
	virtual void CommitMaterialSetUpdate(const FComponentMaterialSet& MaterialSet, bool bApplyToAsset);


	struct FCommitParams
	{
		FMeshDescription* MeshDescription{ nullptr };
	};
	using  FCommitter  = TFunction< void( const FCommitParams& ) >;
	virtual FMeshDescription* GetMesh() = 0;
	virtual void CommitMesh( const FCommitter& ) = 0;

	UPrimitiveComponent* Component{};
};

class INTERACTIVETOOLSFRAMEWORK_API FComponentTargetFactory
{
public:
	virtual ~FComponentTargetFactory(){}
	virtual bool CanBuild( UActorComponent* Candidate ) = 0;
	virtual TUniquePtr<FPrimitiveComponentTarget> Build( UPrimitiveComponent* PrimitiveComponent ) = 0;
};

/**
 * Add a factory method to make ComponentTarget from UPrimitiveComponent*
 * @param Factory The ComponentTargetFactory
 * @return void
 */
INTERACTIVETOOLSFRAMEWORK_API void AddComponentTargetFactory( TUniquePtr<FComponentTargetFactory> Factory );


/**
 * Create a TargetComponent for the given Component
 * @param Component A UObject that we would like to use as tool target. This must presently descend from
 * UPrimitiveComponent
 * @return An FComponentTarget instance. Must not return null, though the MeshSource and MeshSink in it's MeshBridge may
 * be
 */
INTERACTIVETOOLSFRAMEWORK_API TUniquePtr<FPrimitiveComponentTarget> MakeComponentTarget(UPrimitiveComponent* Component);


/**
 * Determine whether a TargetComponent can be created for the given Component
 * @param Component A UObject that we would like to use as tool target.
 * @return bool signifying whether or not a ComponentTarget can be built
 */
INTERACTIVETOOLSFRAMEWORK_API bool CanMakeComponentTarget(UActorComponent* Component);
