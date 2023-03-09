// Copyright Epic Games, Inc. All Rights Reserved.

#include "ClothingAssetToClothAssetExporter.h"
#include "ChaosClothAsset/ClothAsset.h"
#include "ChaosClothAsset/CollectionClothFacade.h"
#include "Animation/Skeleton.h"
#include "Engine/SkinnedAssetCommon.h"
#include "Engine/SkeletalMesh.h"
#include "Materials/Material.h"
#include "Misc/MessageDialog.h"
#include "ClothingAsset.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(ClothingAssetToClothAssetExporter)

#define LOCTEXT_NAMESPACE "ClothingAssetToClothAssetExporter"

UClass* UClothingAssetToChaosClothAssetExporter::GetExportedType() const
{
	return UChaosClothAsset::StaticClass();
}

void UClothingAssetToChaosClothAssetExporter::Export(const UClothingAssetBase* ClothingAsset, UObject* ExportedAsset)
{
	using namespace UE::Chaos::ClothAsset;

	const UClothingAssetCommon* const ClothingAssetCommon = ExactCast<UClothingAssetCommon>(ClothingAsset);
	if (!ClothingAssetCommon)
	{
		const FText TitleMessage = LOCTEXT("ClothingAssetExporterTitle", "Error Exporting Clothing Asset");
		const FText ErrorMessage = LOCTEXT("ClothingAssetExporterError", "Can only export from known ClothingAssetCommon types.");
		FMessageDialog::Open(EAppMsgType::Ok, EAppReturnType::Ok, ErrorMessage, &TitleMessage);
		return;
	}

	UChaosClothAsset* const ClothAsset = CastChecked<UChaosClothAsset>(ExportedAsset);
	check(ClothAsset);
	FCollectionClothFacade Cloth(ClothAsset->GetClothCollection());

	// Create the LODs
	for (const FClothLODDataCommon& ClothLODData : ClothingAssetCommon->LodData)
	{
		const FClothPhysicalMeshData& PhysicalMeshData = ClothLODData.PhysicalMeshData;

		// Unwrap the physical mesh data into the pattern and rest meshes
		FCollectionClothLodFacade ClothLod = Cloth.AddGetLod();
		ClothLod.Initialize(PhysicalMeshData.Vertices, PhysicalMeshData.Indices);
	}

	if (Cloth.GetNumLods())
	{
		// Set the render mesh to duplicate the sim mesh
		ClothAsset->CopySimMeshToRenderMesh();
	}
	else
	{
		// Make sure that at least one empty LOD is always created
		Cloth.AddLod();
	}

	// Assign the physics asset if any (must be done after having added the LODs)
	ClothAsset->SetPhysicsAsset(ClothingAssetCommon->PhysicsAsset);

	// Set the skeleton from the skeletal mesh (must be done after having added the LODs)
	constexpr bool bRebuildModels = false;  // Build is called last
	USkeletalMesh* const SkeletalMesh = CastChecked<USkeletalMesh>(ClothingAssetCommon->GetOuter());
	ClothAsset->SetSkeleton(SkeletalMesh->GetSkeleton(), bRebuildModels);

	// Build the asset, since it is already loaded, it won't rebuild on load
	ClothAsset->Build();
}

#undef LOCTEXT_NAMESPACE

