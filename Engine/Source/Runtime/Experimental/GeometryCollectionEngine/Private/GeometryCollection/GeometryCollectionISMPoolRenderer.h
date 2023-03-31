// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GeometryCollection/GeometryCollectionExternalRenderInterface.h"

#include "GeometryCollectionISMPoolRenderer.generated.h"

class AGeometryCollectionISMPoolActor;

/** Implementation of a custom renderer that pushes AutoInstanceMeshes to an ISMPool. */
UCLASS()
class UGeometryCollectionCustomRendererISMPool : public UObject, public IGeometryCollectionExternalRenderInterface
{
	GENERATED_BODY()

public:
	//~ Begin IGeometryCollectionExternalRenderInterface Interface.
	virtual void UpdateState(UGeometryCollection const& InGeometryCollection, FTransform const& InBaseTransform, bool bInIsBroken) override;
	virtual void UpdateTransforms(UGeometryCollection const& InGeometryCollection, FTransform const& InBaseTransform, TArrayView<const FMatrix> InMatrices) override;
	virtual void OnUnregisterGeometryCollection() override;
	//~ End IGeometryCollectionExternalRenderInterface Interface.

	/** Instanced Static Mesh Pool actor that is used to render our meshes. */
	UPROPERTY(Transient)
	TObjectPtr<AGeometryCollectionISMPoolActor> ISMPoolActor = nullptr;

	/** Description for a group of meshes that are added/updated together. */
	struct FISMPoolGroup
	{
		int32 GroupIndex = INDEX_NONE;
		TArray<int32> MeshIds;
	};

protected:
	/** ISM pool groups per rendering element type. */
	FISMPoolGroup MergedMeshGroup;
	FISMPoolGroup InstancesGroup;

private:
	void InitMergedMeshFromGeometryCollection(UGeometryCollection const& InGeometryCollection);
	void InitInstancesFromGeometryCollection(UGeometryCollection const& InGeometryCollection);
	void UpdateMergedMeshTransforms(FTransform const& InBaseTransform);
	void UpdateInstanceTransforms(UGeometryCollection const& InGeometryCollection, FTransform const& InBaseTransform, TArrayView<const FMatrix> InMatrices);
	void ReleaseGroup(FISMPoolGroup& InOutGroup);
};
