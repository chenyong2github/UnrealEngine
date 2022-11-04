// Copyright Epic Games, Inc. All Rights Reserved.

#include "MeshTranslationImpl.h"

#include "UObject/Package.h"
#include "USDAssetCache.h"
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

namespace UE::MeshTranslationImplInternal::Private
{
	const static FString BaseMaterialPath = TEXT( "/USDImporter/Materials/UsdPreviewSurface.UsdPreviewSurface" );
	const static FString BaseMaterialPathVT = TEXT( "/USDImporter/Materials/UsdPreviewSurfaceVT.UsdPreviewSurfaceVT" );

	const static FString BaseMaterialPathTranslucent = TEXT( "/USDImporter/Materials/UsdPreviewSurfaceTranslucent.UsdPreviewSurfaceTranslucent" );
	const static FString BaseMaterialPathTranslucentVT = TEXT( "/USDImporter/Materials/UsdPreviewSurfaceTranslucentVT.UsdPreviewSurfaceTranslucentVT" );

	const static FString BaseMaterialPathTwoSided = TEXT( "/USDImporter/Materials/UsdPreviewSurfaceTwoSided.UsdPreviewSurfaceTwoSided" );
	const static FString BaseMaterialPathTwoSidedVT = TEXT( "/USDImporter/Materials/UsdPreviewSurfaceTwoSidedVT.UsdPreviewSurfaceTwoSidedVT" );

	const static FString BaseMaterialPathTranslucentTwoSided = TEXT( "/USDImporter/Materials/UsdPreviewSurfaceTranslucentTwoSided.UsdPreviewSurfaceTranslucentTwoSided" );
	const static FString BaseMaterialPathTranslucentTwoSidedVT = TEXT( "/USDImporter/Materials/UsdPreviewSurfaceTranslucentTwoSidedVT.UsdPreviewSurfaceTranslucentTwoSidedVT" );
}

TMap<const UsdUtils::FUsdPrimMaterialSlot*, UMaterialInterface*> MeshTranslationImpl::ResolveMaterialAssignmentInfo(
	const pxr::UsdPrim& UsdPrim,
	const TArray<UsdUtils::FUsdPrimMaterialAssignmentInfo>& AssignmentInfo,
	UUsdAssetCache& AssetCache,
	EObjectFlags Flags
)
{
	FScopedUnrealAllocs Allocs;

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
				// Try reusing an already created DisplayColor material
				if ( UMaterialInterface* ExistingMaterial = Cast<UMaterialInterface>( AssetCache.GetCachedAsset( Slot.MaterialSource ) ) )
				{
					Material = ExistingMaterial;
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

						AssetCache.CacheAsset( Slot.MaterialSource, MaterialInstance );
						Material = MaterialInstance;
					}
				}

				break;
			}
			case UsdUtils::EPrimAssignmentType::MaterialPrim:
			{
				FString PrimPath = Slot.MaterialSource;
				const static FString TwoSidedToken = TEXT( "!twosided" );
				if ( Slot.bMeshIsDoubleSided )
				{
					PrimPath += TwoSidedToken;
				}

				Material = Cast< UMaterialInterface >( AssetCache.GetAssetForPrim( PrimPath ) );

				// Need to create a two-sided material on-demand
				if ( !Material && Slot.bMeshIsDoubleSided )
				{
					// By now we parsed all materials so we must have the single-sided version of this material
					UMaterialInstance* OneSidedMat = Cast< UMaterialInstance >(
						AssetCache.GetAssetForPrim( Slot.MaterialSource )
					);
					if ( !ensure( OneSidedMat ) )
					{
						continue;
					}

					// Important to not use GetBaseMaterial() here because if our parent is the translucent we'll
					// get the base UsdPreviewSurface instead, as that is also *its* base
					UMaterialInterface* BaseMaterial = OneSidedMat->Parent.Get();
					UMaterialInterface* BaseMaterialTwoSided =
						MeshTranslationImpl::GetTwoSidedVersionOfBasePreviewSurfaceMaterial( BaseMaterial );
					if ( !ensure( BaseMaterialTwoSided && BaseMaterialTwoSided != BaseMaterial ) )
					{
						continue;
					}

					const FName NewInstanceName = MakeUniqueObjectName(
						GetTransientPackage(),
						UMaterialInstance::StaticClass(),
						*( FPaths::GetBaseFilename( Slot.MaterialSource ) + UnrealIdentifiers::TwoSidedMaterialSuffix )
					);

#if WITH_EDITOR
					UMaterialInstanceConstant* MIC = Cast<UMaterialInstanceConstant>( OneSidedMat );
					if ( GIsEditor && MIC )
					{
						UMaterialInstanceConstant* TwoSidedMat = NewObject<UMaterialInstanceConstant>(
							GetTransientPackage(),
							NewInstanceName,
							Flags
						);
						if ( TwoSidedMat )
						{
							UUsdAssetImportData* ImportData = NewObject< UUsdAssetImportData >(
								TwoSidedMat,
								TEXT( "USDAssetImportData" )
							);
							ImportData->PrimPath = Slot.MaterialSource;
							TwoSidedMat->AssetImportData = ImportData;
						}

						TwoSidedMat->SetParentEditorOnly( BaseMaterialTwoSided );
						TwoSidedMat->CopyMaterialUniformParametersEditorOnly( OneSidedMat );

						AssetCache.CacheAsset( PrimPath, TwoSidedMat, PrimPath );
						Material = TwoSidedMat;
					}
					else
#endif // WITH_EDITOR
					if ( UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>( OneSidedMat ) )
					{
						UMaterialInstanceDynamic* TwoSidedMat = UMaterialInstanceDynamic::Create(
							BaseMaterialTwoSided,
							GetTransientPackage(),
							NewInstanceName
						);
						if ( !ensure( TwoSidedMat ) )
						{
							continue;
						}

						TwoSidedMat->CopyParameterOverrides( MID );

						AssetCache.CacheAsset( PrimPath, TwoSidedMat, PrimPath );
						Material = TwoSidedMat;
					}
				}

				break;
			}
			case UsdUtils::EPrimAssignmentType::UnrealMaterial:
			{
				Material = Cast< UMaterialInterface >( FSoftObjectPath( Slot.MaterialSource ).TryLoad() );
				if ( !Material->IsTwoSided() && Slot.bMeshIsDoubleSided )
				{
					// TODO: Update this message with the proper source prim paths when UE-138122 is submitted,
					// as 'UsdPrim' may just be e.g. a SkelRoot
					UE_LOG( LogUsd, Warning, TEXT( "Using one-sided UE material '%s' for doubleSided prim '%s'" ),
						*Slot.MaterialSource,
						*UsdToUnreal::ConvertPath( UsdPrim.GetPrimPath() )
					);
				}

				break;
			}
			case UsdUtils::EPrimAssignmentType::None:
			default:
			{
				ensure( false );
				break;
			}
			}

			ResolvedMaterials.Add( &Slot, Material );
		}
	}

	return ResolvedMaterials;
}

void MeshTranslationImpl::SetMaterialOverrides(
	const pxr::UsdPrim& Prim,
	const TArray<UMaterialInterface*>& ExistingAssignments,
	UMeshComponent& MeshComponent,
	UUsdAssetCache& AssetCache,
	float Time,
	EObjectFlags Flags,
	bool bInterpretLODs,
	const FName& RenderContext,
	const FName& MaterialPurpose
)
{
	FScopedUsdAllocs Allocs;

	pxr::UsdGeomMesh Mesh{ Prim };
	if ( !Mesh )
	{
		return;
	}
	pxr::SdfPath PrimPath = Prim.GetPath();
	pxr::UsdStageRefPtr Stage = Prim.GetStage();

	pxr::TfToken RenderContextToken = pxr::UsdShadeTokens->universalRenderContext;
	if ( !RenderContext.IsNone() )
	{
		RenderContextToken = UnrealToUsd::ConvertToken( *RenderContext.ToString() ).Get();
	}

	pxr::TfToken MaterialPurposeToken = pxr::UsdShadeTokens->allPurpose;
	if ( !MaterialPurpose.IsNone() )
	{
		MaterialPurposeToken = UnrealToUsd::ConvertToken( *MaterialPurpose.ToString() ).Get();
	}

	TArray<UsdUtils::FUsdPrimMaterialAssignmentInfo> LODIndexToAssignments;
	const bool bProvideMaterialIndices = false; // We have no use for material indices and it can be slow to retrieve, as it will iterate all faces

	// Extract material assignment info from prim if it is a LOD mesh
	bool bInterpretedLODs = false;
	if ( bInterpretLODs && UsdUtils::IsGeomMeshALOD( Prim ) )
	{
		TMap<int32, UsdUtils::FUsdPrimMaterialAssignmentInfo> LODIndexToAssignmentsMap;
		TFunction<bool( const pxr::UsdGeomMesh&, int32 )> IterateLODs = [ & ]( const pxr::UsdGeomMesh& LODMesh, int32 LODIndex )
		{
			UsdUtils::FUsdPrimMaterialAssignmentInfo LODInfo = UsdUtils::GetPrimMaterialAssignments(
				LODMesh.GetPrim(),
				pxr::UsdTimeCode( Time ),
				bProvideMaterialIndices,
				RenderContextToken,
				MaterialPurposeToken
			);
			LODIndexToAssignmentsMap.Add( LODIndex, LODInfo );
			return true;
		};

		pxr::UsdPrim ParentPrim = Prim.GetParent();
		bInterpretedLODs = UsdUtils::IterateLODMeshes( ParentPrim, IterateLODs );

		if ( bInterpretedLODs )
		{
			LODIndexToAssignmentsMap.KeySort( TLess<int32>() );
			for ( TPair<int32, UsdUtils::FUsdPrimMaterialAssignmentInfo>& Entry : LODIndexToAssignmentsMap )
			{
				LODIndexToAssignments.Add( MoveTemp( Entry.Value ) );
			}
		}
	}

	// Refresh reference to Prim because variant switching potentially invalidated it
	pxr::UsdPrim ValidPrim = Stage->GetPrimAtPath( PrimPath );

	// Extract material assignment info from prim if its *not* a LOD mesh, or if we failed to parse LODs
	if ( !bInterpretedLODs )
	{
		LODIndexToAssignments = {
			UsdUtils::GetPrimMaterialAssignments(
				ValidPrim,
				pxr::UsdTimeCode( Time ),
				bProvideMaterialIndices,
				RenderContextToken,
				MaterialPurposeToken
			)
		};
	}

	TMap<const UsdUtils::FUsdPrimMaterialSlot*, UMaterialInterface*> ResolvedMaterials = MeshTranslationImpl::ResolveMaterialAssignmentInfo(
		ValidPrim,
		LODIndexToAssignments,
		AssetCache,
		Flags
	);

	// Compare resolved materials with existing assignments, and create overrides if we need to
	uint32 StaticMeshSlotIndex = 0;
	for ( int32 LODIndex = 0; LODIndex < LODIndexToAssignments.Num(); ++LODIndex )
	{
		const TArray< UsdUtils::FUsdPrimMaterialSlot >& LODSlots = LODIndexToAssignments[ LODIndex ].Slots;
		for ( int32 LODSlotIndex = 0; LODSlotIndex < LODSlots.Num(); ++LODSlotIndex, ++StaticMeshSlotIndex )
		{
			const UsdUtils::FUsdPrimMaterialSlot& Slot = LODSlots[ LODSlotIndex ];

			UMaterialInterface* Material = nullptr;
			if ( UMaterialInterface** FoundMaterial = ResolvedMaterials.Find( &Slot ) )
			{
				Material = *FoundMaterial;
			}
			else
			{
				UE_LOG( LogUsd, Error, TEXT( "Lost track of resolved material for slot '%d' of LOD '%d' for mesh '%s'" ), LODSlotIndex, LODIndex, *UsdToUnreal::ConvertPath( Prim.GetPath() ) );
				continue;
			}

			UMaterialInterface* ExistingMaterial = ExistingAssignments[ StaticMeshSlotIndex ];
			if ( ExistingMaterial == Material )
			{
				continue;
			}
			else
			{
				MeshComponent.SetMaterial( StaticMeshSlotIndex, Material );
			}
		}
	}
}

UMaterialInterface* MeshTranslationImpl::GetBasePreviewSurfaceMaterial( EUsdBaseMaterialProperties BaseMaterialProperties )
{
	using namespace UE::MeshTranslationImplInternal::Private;

	const bool bIsTranslucent = EnumHasAnyFlags( BaseMaterialProperties, EUsdBaseMaterialProperties::Translucent );
	const bool bIsVT = EnumHasAnyFlags( BaseMaterialProperties, EUsdBaseMaterialProperties::VT );
	const bool bIsTwoSided = EnumHasAnyFlags( BaseMaterialProperties, EUsdBaseMaterialProperties::TwoSided );

	const FString* TargetMaterialPath = nullptr;
	if ( bIsTranslucent )
	{
		if ( bIsVT )
		{
			if ( bIsTwoSided )
			{
				TargetMaterialPath = &BaseMaterialPathTranslucentTwoSidedVT;
			}
			else
			{
				TargetMaterialPath = &BaseMaterialPathTranslucentVT;
			}
		}
		else
		{
			if ( bIsTwoSided )
			{
				TargetMaterialPath = &BaseMaterialPathTranslucentTwoSided;
			}
			else
			{
				TargetMaterialPath = &BaseMaterialPathTranslucent;
			}
		}
	}
	else
	{
		if ( bIsVT )
		{
			if ( bIsTwoSided )
			{
				TargetMaterialPath = &BaseMaterialPathTwoSidedVT;
			}
			else
			{
				TargetMaterialPath = &BaseMaterialPathVT;
			}
		}
		else
		{
			if ( bIsTwoSided )
			{
				TargetMaterialPath = &BaseMaterialPathTwoSided;
			}
			else
			{
				TargetMaterialPath = &BaseMaterialPath;
			}
		}
	}

	if ( !TargetMaterialPath )
	{
		return nullptr;
	}

	return Cast< UMaterialInterface >( FSoftObjectPath( *TargetMaterialPath ).TryLoad() );
}

UMaterialInterface* MeshTranslationImpl::GetVTVersionOfBasePreviewSurfaceMaterial( UMaterialInterface* BaseMaterial )
{
	using namespace UE::MeshTranslationImplInternal::Private;

	if ( !BaseMaterial )
	{
		return nullptr;
	}

	const FString PathName = BaseMaterial->GetPathName();
	if ( PathName.Contains( TEXT( "VT" ), ESearchCase::CaseSensitive, ESearchDir::FromEnd ) )
	{
		return BaseMaterial;
	}
	else if ( PathName == BaseMaterialPath )
	{
		return Cast< UMaterialInterface >( FSoftObjectPath{ BaseMaterialPathVT }.TryLoad() );
	}
	else if ( PathName == BaseMaterialPathTwoSided )
	{
		return Cast< UMaterialInterface >( FSoftObjectPath{ BaseMaterialPathTwoSidedVT }.TryLoad() );
	}
	else if ( PathName == BaseMaterialPathTranslucent )
	{
		return Cast< UMaterialInterface >( FSoftObjectPath{ BaseMaterialPathTranslucentVT }.TryLoad() );
	}
	else if ( PathName == BaseMaterialPathTranslucentTwoSided )
	{
		return Cast< UMaterialInterface >( FSoftObjectPath{ BaseMaterialPathTranslucentTwoSidedVT }.TryLoad() );
	}

	// We should only ever call this function with a BaseMaterial that matches one of the above paths
	ensure( false );
	return nullptr;
}

UMaterialInterface* MeshTranslationImpl::GetTwoSidedVersionOfBasePreviewSurfaceMaterial( UMaterialInterface* BaseMaterial )
{
	using namespace UE::MeshTranslationImplInternal::Private;

	if ( !BaseMaterial )
	{
		return nullptr;
	}

	const FString PathName = BaseMaterial->GetPathName();
	if ( PathName.Contains( TEXT( "TwoSided" ), ESearchCase::CaseSensitive, ESearchDir::FromEnd ) )
	{
		return BaseMaterial;
	}
	else if ( PathName == BaseMaterialPath )
	{
		return Cast< UMaterialInterface >( FSoftObjectPath{ BaseMaterialPathTwoSided }.TryLoad() );
	}
	else if ( PathName == BaseMaterialPathTranslucent )
	{
		return Cast< UMaterialInterface >( FSoftObjectPath{ BaseMaterialPathTranslucentTwoSided }.TryLoad() );
	}
	else if ( PathName == BaseMaterialPathVT )
	{
		return Cast< UMaterialInterface >( FSoftObjectPath{ BaseMaterialPathTwoSidedVT }.TryLoad() );
	}
	else if ( PathName == BaseMaterialPathTranslucentVT )
	{
		return Cast< UMaterialInterface >( FSoftObjectPath{ BaseMaterialPathTranslucentTwoSidedVT }.TryLoad() );
	}

	// We should only ever call this function with a BaseMaterial that matches one of the above paths
	ensure( false );
	return nullptr;
}

#endif // #if USE_USD_SDK