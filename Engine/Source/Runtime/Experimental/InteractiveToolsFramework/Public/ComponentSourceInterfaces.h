// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Math/UnrealMath.h"
#include "Engine/EngineTypes.h"    // FHitResult

// predeclarations
class AActor;
class UActorComponent;
struct FMeshDescription;
class UMaterialInterface;



/**
 * Wrapper around a UObject Component that can provide a MeshDescription, and
 * (optionally) bake a modified MeshDescription back to this Component.
 * An example of a Source might be a StaticMeshComponent. How a modified
 * MeshDescription is committed back is context-dependent (in Editor vs PIE vs Runtime, etc).
 * 
 * (Conceivably this doesn't have to be backed by a Component, but most usage will assume there is an Actor)
 */
class IMeshDescriptionSource
{
public:
	virtual ~IMeshDescriptionSource() {}

	
	/** @return the Actor that owns this Component */
	virtual AActor* GetOwnerActor() const = 0;

	/** @return the Component this is a Source for */
	virtual UActorComponent* GetOwnerComponent() const = 0;

	/** @return Pointer to the MeshDescription this Source is providing */
	virtual FMeshDescription* GetMeshDescription() const = 0;

	/**
	 * Get pointer to a Material provided by this Source
	 * @param MaterialIndex index of the material
	 * @return MaterialInterface pointer, or null if MaterialIndex is invalid
	 */
	virtual UMaterialInterface* GetMaterial(int32 MaterialIndex) const = 0;

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
	virtual bool HitTest(const FRay& WorldRay, FHitResult& OutHit) const = 0;

	/**
	 * Set the visibility of the Component associated with this Source (ie to hide during Tool usage)
	 * @param bVisible desired visibility
	 */
	virtual void SetOwnerVisibility(bool bVisible) const = 0;


	/** @return true if this Source is read-only, ie Commit functions cannot be called */
	virtual bool IsReadOnly() const { return true; }

	/**
	 * Call this to modify the MeshDescription provided by the Source. You provide a callback
	 * function that the Source will immediately call with a suitable MeshDescription instance.
	 * The Source will then update the Component as necessary. The Source is responsible for
	 * making sure that it is safe to modify this MeshDescription instance.
	 * @param ModifyFunction callback function that updates/modifies a MeshDescription
	 */
	virtual void CommitInPlaceModification(const TFunction<void(FMeshDescription*)>& ModifyFunction) { check(false); }
};





/**
 * Interface to a Factory that knows how to build Sources for UObjects. 
 */
class IComponentSourceFactory
{
public:
	virtual ~IComponentSourceFactory() {}

	/**
	 * Create a MeshDescription source for the given Component
	 * @param Component A UObject that can provide a MeshDescription. Assumption is this is a Component of an Actor.
	 * @return A MeshDescriptionSource instance. Must not return null.
	 */
	virtual TUniquePtr<IMeshDescriptionSource> MakeMeshDescriptionSource(UActorComponent* Component) = 0;
};

