// Copyright Epic Games, Inc. All Rights Reserved.

#include "USDShadeMaterialTranslator.h"

#include "USDAssetImportData.h"
#include "USDShadeConversion.h"
#include "USDTypesConversion.h"

#include "UsdWrappers/SdfPath.h"

#include "Engine/Texture.h"
#include "Materials/Material.h"
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

	UObject*& CachedMaterial = Context->AssetsCache.FindOrAdd( MaterialHashString );

	if ( !CachedMaterial )
	{
		UMaterial* NewMaterial = NewObject< UMaterial >( GetTransientPackage(), NAME_None, Context->ObjectFlags );

		UUsdAssetImportData* ImportData = NewObject< UUsdAssetImportData >( NewMaterial, TEXT("USDAssetImportData") );
		ImportData->PrimPath = PrimPath.GetString();
		NewMaterial->AssetImportData = ImportData;

		TMap<FString, int32> Unused;
		TMap<FString, int32>& PrimvarToUVIndex = Context->MaterialToPrimvarToUVIndex ? Context->MaterialToPrimvarToUVIndex->FindOrAdd( PrimPath.GetString() ) : Unused;

		if ( UsdToUnreal::ConvertMaterial( ShadeMaterial, *NewMaterial, Context->AssetsCache, PrimvarToUVIndex ) )
		{
			//UMaterialEditingLibrary::RecompileMaterial( CachedMaterial ); // Too slow
			NewMaterial->PostEditChange();
		}
		else
		{
			NewMaterial = nullptr;
		}

		// ConvertMaterial may have added other items to AssetsCache, so lets update the reference to make sure its ok
		CachedMaterial = Context->AssetsCache.Add( MaterialHashString, NewMaterial );
	}

	Context->CurrentlyUsedAssets.Add( CachedMaterial );
	if ( UMaterial* ImportedMaterial = Cast<UMaterial>( CachedMaterial ) )
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

	FScopeLock Lock( &Context->CriticalSection );
	{
		Context->PrimPathsToAssets.Add( PrimPath.GetString(), CachedMaterial );
	}
}

#endif // #if USE_USD_SDK
