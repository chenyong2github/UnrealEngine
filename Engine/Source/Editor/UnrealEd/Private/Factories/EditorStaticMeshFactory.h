// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Factories/AssetFactoryInterface.h"
#include "ActorFactories/ActorFactoryStaticMesh.h"

#include "EditorStaticMeshFactory.generated.h"

struct FFoliageInfo;

UCLASS(Transient)
class UEditorStaticMeshFactory : public UActorFactoryStaticMesh
{
	GENERATED_BODY()

public:
	// Begin IAssetFactoryInterface
	virtual TArray<FTypedElementHandle> PlaceAsset(const FAssetPlacementInfo& InPlacementInfo, const FPlacementOptions& InPlacementOptions) override;
	virtual FAssetData GetAssetDataFromElementHandle(const FTypedElementHandle& InHandle) override;
	virtual void EndPlacement(TArrayView<const FTypedElementHandle> InPlacedElements, const FPlacementOptions& InPlacementOptions) override;
	// End IAssetFactoryInterface

protected:
	bool ShouldPlaceInstancedStaticMeshes(const FPlacementOptions& InPlacementOptions) const;
	TSet<FFoliageInfo*> CurrentPlacementScopedFoliageInfos;
};