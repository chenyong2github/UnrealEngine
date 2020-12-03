// Copyright Epic Games, Inc. All Rights Reserved.

#include "USDShadeMaterialTranslator.h"

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

	FString MaterialHashString = UsdUtils::HashShadeMaterial( ShadeMaterial ).ToString();

	UMaterialInterface* ConvertedMaterial = nullptr;
	if ( UObject** FoundCachedMaterial = Context->AssetsCache.Find( MaterialHashString ) )
	{
		ConvertedMaterial = Cast<UMaterialInterface>( *FoundCachedMaterial );
	}

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
				if ( UMaterialInstanceConstant* NewMaterial = NewObject<UMaterialInstanceConstant>( GetTransientPackage() ) )
				{
					NewMaterial->SetParentEditorOnly( MasterMaterial );

					UUsdAssetImportData* ImportData = NewObject< UUsdAssetImportData >( NewMaterial, TEXT( "USDAssetImportData" ) );
					ImportData->PrimPath = PrimPath.GetString();
					NewMaterial->AssetImportData = ImportData;

					TMap<FString, int32> Unused;
					TMap<FString, int32>& PrimvarToUVIndex = Context->MaterialToPrimvarToUVIndex ? Context->MaterialToPrimvarToUVIndex->FindOrAdd( PrimPath.GetString() ) : Unused;

					UsdToUnreal::ConvertMaterial( ShadeMaterial, *NewMaterial, Context->AssetsCache, PrimvarToUVIndex );

					FMaterialUpdateContext UpdateContext( FMaterialUpdateContext::EOptions::Default, GMaxRHIShaderPlatform );
					UpdateContext.AddMaterialInstance( NewMaterial );
					NewMaterial->PreEditChange( nullptr );
					NewMaterial->PostEditChange();

					ConvertedMaterial = Cast<UMaterialInterface>( Context->AssetsCache.Add( MaterialHashString, NewMaterial ) );
				}
#endif // WITH_EDITOR
			}
			else if ( UMaterialInstanceDynamic* NewMaterial = UMaterialInstanceDynamic::Create( MasterMaterial, GetTransientPackage() ) )
			{
				TMap<FString, int32> Unused;
				TMap<FString, int32>& PrimvarToUVIndex = Context->MaterialToPrimvarToUVIndex ? Context->MaterialToPrimvarToUVIndex->FindOrAdd( PrimPath.GetString() ) : Unused;

				UsdToUnreal::ConvertMaterial( ShadeMaterial, *NewMaterial, Context->AssetsCache, PrimvarToUVIndex );

				ConvertedMaterial = Cast<UMaterialInterface>( Context->AssetsCache.Add( MaterialHashString, NewMaterial ) );
			}
		}
	}

	if ( !ConvertedMaterial )
	{
		return;
	}

	Context->CurrentlyUsedAssets.Add( ConvertedMaterial );

	// This won't work for the new material workflow as the new material instances aren't actually UMaterial
	if ( UMaterial* ImportedMaterial = Cast<UMaterial>( ConvertedMaterial ) )
	{
		TArray<UTexture*> UsedTextures;
		const bool bAllQualityLevels = true;
		const bool bAllFeatureLevels = true;
		ImportedMaterial->GetUsedTextures( UsedTextures, EMaterialQualityLevel::High, bAllQualityLevels, ERHIFeatureLevel::SM5, bAllFeatureLevels );
		for ( UTexture* UsedTexture : UsedTextures )
		{
			Context->CurrentlyUsedAssets.Add( UsedTexture );
		}
	}
	else if ( UMaterialInstance* MaterialInstance = Cast<UMaterialInstance>( ConvertedMaterial ) )
	{
		for ( const FTextureParameterValue& TextureValue : MaterialInstance->TextureParameterValues )
		{
			if ( TextureValue.ParameterValue )
			{
				Context->CurrentlyUsedAssets.Add( TextureValue.ParameterValue );
			}
		}
	}

	FScopeLock Lock( &Context->CriticalSection );
	{
		Context->PrimPathsToAssets.Add( PrimPath.GetString(), ConvertedMaterial );
	}
}

#endif // #if USE_USD_SDK
