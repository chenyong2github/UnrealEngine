// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Math/UnrealMath.h"
#include "Misc/Optional.h"
#include "Templates/Function.h"
#include "Engine/EngineTypes.h"    // FHitResult

// predeclarations
class AActor;
class UPrimitiveComponent;
struct FMeshDescription;
class UMaterialInterface;

template <class MeshT>
struct TMeshBridge
{
	using  FSource    = TFunction< MeshT*() >;
	using  FCommitter = TFunction< void( MeshT* ) >;
	using  FSink      = TFunction< void( const FCommitter& ) >;
	using  FBuilder   = TFunction< TMeshBridge ( UPrimitiveComponent* ) >;

	bool HasSource(){ return !!GetMesh; }
	bool HasSink(){ return !!CommitMesh; }

	FSource GetMesh{nullptr};
	FSink   CommitMesh{nullptr};
};
using FMeshDescriptionBridge = TMeshBridge<FMeshDescription>;

/**
 * Add a factory method to make MeshDescriptionSources from UActorComponent*
 * @param Builder The MeshDescriptionSourceBuilder
 * @return void
 */
INTERACTIVETOOLSFRAMEWORK_API void AddMeshDescriptionBridgeBuilder( FMeshDescriptionBridge::FBuilder Builder );


/**
 * Wrapper around a UObject Component that can provide a MeshDescription, and
 * (optionally) bake a modified MeshDescription back to this Component.
 * An example of a Source might be a StaticMeshComponent. How a modified
 * MeshDescription is committed back is context-dependent (in Editor vs PIE vs Runtime, etc).
 *
 * (Conceivably this doesn't have to be backed by a Component, but most usage will assume there is an Actor)
 */
class INTERACTIVETOOLSFRAMEWORK_API FComponentTarget
{
public:
	/** @return the Actor that owns this Component */
	AActor* GetOwnerActor() const;

	/** @return the Component this is a Source for */
	UPrimitiveComponent* GetOwnerComponent() const;

	/**
	 * Get pointer to a Material provided by this Source
	 * @param MaterialIndex index of the material
	 * @return MaterialInterface pointer, or null if MaterialIndex is invalid
	 */
	UMaterialInterface* GetMaterial(int32 MaterialIndex) const;

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

	FMeshDescriptionBridge MeshDescriptionBridge{};
	UPrimitiveComponent* Component{};
};

/**
 * Create a TargetComponent for the given Component
 * @param Component A UObject that we would like to use as tool target. This must presently descend from
 * UPrimitiveComponent
 * @return An FComponentTarget instance. Must not return null, though the MeshSource and MeshSink in it's MeshBridge may
 * be
 */
INTERACTIVETOOLSFRAMEWORK_API FComponentTarget MakeComponentTarget(UPrimitiveComponent* Component);
