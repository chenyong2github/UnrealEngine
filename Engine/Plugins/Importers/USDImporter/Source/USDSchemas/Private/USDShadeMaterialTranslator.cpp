// Copyright Epic Games, Inc. All Rights Reserved.

#include "USDShadeMaterialTranslator.h"

#include "USDAssetCache.h"
#include "USDAssetImportData.h"
#include "USDShadeConversion.h"
#include "USDTypesConversion.h"

#include "UsdWrappers/SdfPath.h"

#include "Engine/Texture.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/SecureHash.h"

#if USE_USD_SDK

#include "USDIncludesStart.h"
	#include "pxr/usd/usdShade/material.h"
#include "USDIncludesEnd.h"


void FUsdShadeMaterialTranslator::CreateAssets()
{
	pxr::UsdShadeMaterial ShadeMaterial( GetPrim() );

	if ( !ShadeMaterial )
	{
		return;
	}

	TRACE_CPUPROFILER_EVENT_SCOPE( FUsdShadeMaterialTranslator::CreateAssets );

	FString MaterialHashString = UsdUtils::HashShadeMaterial( ShadeMaterial ).ToString();

	UMaterialInterface* ConvertedMaterial = Cast<UMaterialInterface>( Context->AssetCache->GetCachedAsset( MaterialHashString ) );

	if ( !ConvertedMaterial )
	{
		FString MaterialPath = UsdUtils::IsMaterialTranslucent( ShadeMaterial )
			? TEXT( "Material'/USDImporter/Materials/UsdPreviewSurfaceTranslucent.UsdPreviewSurfaceTranslucent'" )
			: TEXT( "Material'/USDImporter/Materials/UsdPreviewSurface.UsdPreviewSurface'" );

		if ( UMaterialInterface* MasterMaterial = Cast< UMaterialInterface >( FSoftObjectPath( MaterialPath ).TryLoad() ) )
		{
			if ( GIsEditor ) // Also have to prevent Standalone game from going with MaterialInstanceConstants
			{
#if WITH_EDITOR
				if ( UMaterialInstanceConstant* NewMaterial = NewObject<UMaterialInstanceConstant>( GetTransientPackage(), NAME_None, Context->ObjectFlags ) )
				{
					NewMaterial->SetParentEditorOnly( MasterMaterial );

					UUsdAssetImportData* ImportData = NewObject< UUsdAssetImportData >( NewMaterial, TEXT( "USDAssetImportData" ) );
					ImportData->PrimPath = PrimPath.GetString();
					NewMaterial->AssetImportData = ImportData;

					TMap<FString, int32> Unused;
					TMap<FString, int32>& PrimvarToUVIndex = Context->MaterialToPrimvarToUVIndex ? Context->MaterialToPrimvarToUVIndex->FindOrAdd( PrimPath.GetString() ) : Unused;

					UsdToUnreal::ConvertMaterial( ShadeMaterial, *NewMaterial, Context->AssetCache.Get(), PrimvarToUVIndex );

					FMaterialUpdateContext UpdateContext( FMaterialUpdateContext::EOptions::Default, GMaxRHIShaderPlatform );
					UpdateContext.AddMaterialInstance( NewMaterial );
					NewMaterial->PreEditChange( nullptr );
					NewMaterial->PostEditChange();

					ConvertedMaterial = NewMaterial;
				}
#endif // WITH_EDITOR
			}
			else if ( UMaterialInstanceDynamic* NewMaterial = UMaterialInstanceDynamic::Create( MasterMaterial, GetTransientPackage() ) )
			{
				TMap<FString, int32> Unused;
				TMap<FString, int32>& PrimvarToUVIndex = Context->MaterialToPrimvarToUVIndex ? Context->MaterialToPrimvarToUVIndex->FindOrAdd( PrimPath.GetString() ) : Unused;

				UsdToUnreal::ConvertMaterial( ShadeMaterial, *NewMaterial, Context->AssetCache.Get(), PrimvarToUVIndex );

				ConvertedMaterial = NewMaterial;
			}
		}
	}

	if ( ConvertedMaterial )
	{
		Context->AssetCache->CacheAsset( MaterialHashString, ConvertedMaterial, PrimPath.GetString() );
	}
}

#endif // #if USE_USD_SDK
