// Copyright Epic Games, Inc. All Rights Reserved.

#include "MeshTranslationImpl.h"

#include "USDAssetImportData.h"
#include "USDGeomMeshConversion.h"
#include "USDLog.h"
#include "USDMemory.h"
#include "USDTypesConversion.h"

#include "Components/MeshComponent.h"
#include "CoreMinimal.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

#if USE_USD_SDK

#include "USDIncludesStart.h"
	#include "pxr/usd/usd/editContext.h"
	#include "pxr/usd/usd/prim.h"
	#include "pxr/usd/usd/stage.h"
	#include "pxr/usd/usdGeom/mesh.h"
	#include "pxr/usd/usdGeom/xformable.h"
	#include "pxr/usd/usdShade/material.h"
#include "USDIncludesEnd.h"

TMap<const UsdUtils::FUsdPrimMaterialSlot*, UMaterialInterface*> MeshTranslationImpl::ResolveMaterialAssignmentInfo( const pxr::UsdPrim& UsdPrim, const TArray<UsdUtils::FUsdPrimMaterialAssignmentInfo>& AssignmentInfo, const TArray<UMaterialInterface*>& ExistingAssignments, const TMap< FString, UObject* >& PrimPathsToAssets, TMap< FString, UObject* >& AssetsCache, float Time, EObjectFlags Flags )
{
	TMap<const UsdUtils::FUsdPrimMaterialSlot*, UMaterialInterface*> ResolvedMaterials;

	uint32 GlobalResolvedMaterialIndex = 0;
	for ( int32 InfoIndex = 0; InfoIndex < AssignmentInfo.Num(); ++InfoIndex )
	{
		const TArray< UsdUtils::FUsdPrimMaterialSlot >& Slots = AssignmentInfo[ InfoIndex ].Slots;

		for ( int32 SlotIndex = 0; SlotIndex < Slots.Num(); ++SlotIndex, ++GlobalResolvedMaterialIndex )
		{
			const UsdUtils::FUsdPrimMaterialSlot& Slot = Slots[ SlotIndex ];
			UMaterialInterface* Material = nullptr;

			switch ( Slot.AssignmentType )
			{
			case UsdUtils::EPrimAssignmentType::DisplayColor:
			{
				FScopedUsdAllocs Allocs;

				// Try reusing an already created DisplayColor material
				if ( UObject** FoundAsset = AssetsCache.Find( Slot.MaterialSource ) )
				{
					if ( UMaterialInterface* ExistingMaterial = Cast<UMaterialInterface>( *FoundAsset ) )
					{
						Material = ExistingMaterial;
					}
				}

				// Need to create a new DisplayColor material
				if ( Material == nullptr )
				{
					if ( TOptional< UsdUtils::FDisplayColorMaterial > DisplayColorDesc = UsdUtils::FDisplayColorMaterial::FromString( Slot.MaterialSource ) )
					{
						UMaterialInstance* MaterialInstance = nullptr;

						if ( GIsEditor )  // Editor, PIE => true; Standlone, packaged => false
						{
							MaterialInstance = UsdUtils::CreateDisplayColorMaterialInstanceConstant( DisplayColorDesc.GetValue() );
#if WITH_EDITOR
							// Leave PrimPath as empty as it likely will be reused by many prims
							UUsdAssetImportData* ImportData = NewObject< UUsdAssetImportData >( MaterialInstance, TEXT( "USDAssetImportData" ) );
							MaterialInstance->AssetImportData = ImportData;
#endif // WITH_EDITOR
						}
						else
						{
							MaterialInstance = UsdUtils::CreateDisplayColorMaterialInstanceDynamic( DisplayColorDesc.GetValue() );
						}

						AssetsCache.Add( Slot.MaterialSource, MaterialInstance );
						Material = MaterialInstance;
					}
				}

				break;
			}
			case UsdUtils::EPrimAssignmentType::MaterialPrim:
			{
				FScopedUsdAllocs Allocs;

				// Check first or else we may get a warning
				if ( pxr::SdfPath::IsValidPathString( UnrealToUsd::ConvertString( *Slot.MaterialSource ).Get() ) )
				{
					pxr::SdfPath MaterialPrimPath = UnrealToUsd::ConvertPath( *Slot.MaterialSource ).Get();

					// TODO: This may break if MaterialPrimPath targets a prim inside a LOD variant that is disabled...
					TUsdStore< pxr::UsdPrim > MaterialPrim = UsdPrim.GetStage()->GetPrimAtPath( MaterialPrimPath );
					if ( MaterialPrim.Get() )
					{
						Material = Cast< UMaterialInterface >( PrimPathsToAssets.FindRef( UsdToUnreal::ConvertPath( MaterialPrim.Get().GetPrimPath() ) ) );
					}
				}

				break;
			}
			case UsdUtils::EPrimAssignmentType::UnrealMaterial:
			{
				Material = Cast< UMaterialInterface >( FSoftObjectPath( Slot.MaterialSource ).TryLoad() );
				break;
			}
			case UsdUtils::EPrimAssignmentType::None:
			default:
			{
				// Check if there is a material already on the mesh
				UMaterialInstanceConstant* ExistingMaterialInstance = Cast< UMaterialInstanceConstant >( ExistingAssignments[ GlobalResolvedMaterialIndex ] );

				// Assuming that we own the material instance and that we can change it as we wish, reuse it
				if ( ExistingMaterialInstance && ExistingMaterialInstance->GetOuter() == GetTransientPackage() )
				{
#if WITH_EDITOR
					UUsdAssetImportData* AssetImportData = Cast<UUsdAssetImportData>( ExistingMaterialInstance->AssetImportData );
					if ( AssetImportData && AssetImportData->PrimPath == UsdToUnreal::ConvertPath( UsdPrim.GetPrimPath() ) )
#endif // WITH_EDITOR
					{
						// If we have displayColor data on our prim, repurpose this material to show it
						if ( TOptional<UsdUtils::FDisplayColorMaterial> DisplayColorDescription = UsdUtils::ExtractDisplayColorMaterial( pxr::UsdGeomMesh( UsdPrim ) ) )
						{
							UsdToUnreal::ConvertDisplayColor( DisplayColorDescription.GetValue(), *ExistingMaterialInstance );
						}

						Material = ExistingMaterialInstance;
					}
				}
				break;
			}
			}

			ResolvedMaterials.Add( &Slot, Material );
		}
	}

	return ResolvedMaterials;
}

#endif // #if USE_USD_SDK