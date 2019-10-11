// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/DataAsset.h"
#include "StaticMeshEditorAssetContainer.generated.h"

/**
 * Asset container for the static mesh editor extension
 */
UCLASS()
class UStaticMeshEditorAssetContainer : public UDataAsset
{
    GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = Material)
	class UMaterialInterface* HoveredGeometryMaterial;

	UPROPERTY(EditAnywhere, Category = Material)
	class UMaterialInterface* HoveredFaceMaterial;

	UPROPERTY(EditAnywhere, Category = Material)
	class UMaterialInterface* WireMaterial;

	UPROPERTY(EditAnywhere, Category = Material)
	class UMaterialInterface* OverlayLineMaterial;

	UPROPERTY(EditAnywhere, Category = Material)
	class UMaterialInterface* OverlayPointMaterial;
};