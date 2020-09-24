// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"

#include "GroomAssetMeshes.generated.h"


class UMaterialInterface;
class UStaticMesh;


USTRUCT(BlueprintType)
struct HAIRSTRANDSCORE_API FHairGroupsMeshesSourceDescription
{
	GENERATED_BODY()

	FHairGroupsMeshesSourceDescription();

	UPROPERTY(EditAnywhere, Category = "Rendering", meta = (ToolTip = "Material used for meshes rendering"))
	UMaterialInterface* Material = nullptr;

	UPROPERTY(EditAnywhere, Category = "MeshSettings", meta = (ToolTip = "Mesh settings"))
	class UStaticMesh* ImportedMesh;

	/* Group index on which this mesh geometry will be used (#hair_todo: change this to be a dropdown selection menu in FHairLODSettings instead) */
	UPROPERTY(EditAnywhere, Category = "CardsSource")
	int32 GroupIndex = 0;

	/* LOD on which this mesh geometry will be used. -1 means not used  (#hair_todo: change this to be a dropdown selection menu in FHairLODSettings instead) */
	UPROPERTY(EditAnywhere, Category = "CardsSource")
	int32 LODIndex = -1;

	bool operator==(const FHairGroupsMeshesSourceDescription& A) const;

};