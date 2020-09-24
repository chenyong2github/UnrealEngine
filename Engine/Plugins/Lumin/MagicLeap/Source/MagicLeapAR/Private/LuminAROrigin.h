// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once
#include "AROriginActor.h"
#include "MRMeshComponent.h"
#include "MagicLeapPlanesTypes.h"
#include "LuminAROrigin.generated.h"

/**
 *	Because of the way ARPlanesTracker works, assuming the current ARSession 
 *		configuration contains the flag 'bGenerateMeshDataFromTrackedGeometry',
 *		the ARPlanesTracker requires that there exists a UMaterialInterface
 *		which is compatible with the mesh data provided to UMRMeshComponents
 *		which are subsequently attached to the AROrigin.  This is because for
 *		some reason, the default surface material obtained via 
 *		UMaterial::GetDefaultMaterial(MD_Surface) does not correctly render
 *		vertex data provided to the UMRMeshComponents, even if an exact copy
 *		of the WorldGridMaterial itself does!
 *	For this reason, the LuminAROrigin will store a compatible material which
 *		is loaded via standard UE4 constructor interfaces.
 */
UCLASS()
class ALuminAROrigin : public AAROriginActor
{
	GENERATED_UCLASS_BODY()

public:
	/**
	*	Either return an instance to the origin actor in the world or spawn one
	*		if it doesn't already exist in the current world.
	*	@param ARSessionConfig   The desired configuration for the AROrigin's MRMeshComponent(s)
	*	@param World	The world in which to search for the origin actor.  If world is not provided, this function will attempt to call FindWorld first.
	*/
	static ALuminAROrigin* GetOriginActor(
								const class UARSessionConfig& ARSessionConfig,
								UWorld* World = nullptr);
	/**
	*	Find and return the first most appropriate UWorld in which an 
	*		AROriginActor should reside by iterating over all global world
	*		contexts.
	*/
	static UWorld* FindWorld();

public:
	virtual void Tick(float DeltaSeconds) override;

	void CreatePlane(const FGuid& PlaneId,
					 const TArray<FVector>& VerticesLocalSpace,
					 const FTransform& LocalToTracking,
					 EMagicLeapPlaneQueryFlags FlagForVertexColor);
	void DestroyPlane(const FGuid& PlaneId);

private:
	IMRMesh::FBrickId GetBrickId(const FGuid& PlaneId);

private:
	UPROPERTY()
	class UMRMeshComponent* MRMeshComponent;

	UPROPERTY()
	class UMaterialInterface* PlaneSurfaceMaterial;

	UPROPERTY()
	class UMaterialInterface* WireframeMaterial;

	TMap<FGuid, IMRMesh::FBrickId> PlaneIdToBrickId;
	IMRMesh::FBrickId NextBrickId = 0;
	TMap<EMagicLeapPlaneQueryFlags, FColor> VertexColorMapping;
};
