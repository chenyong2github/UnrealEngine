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
	#include "pxr/base/tf/token.h"
	#include "pxr/usd/usdShade/material.h"
	#include "pxr/usd/usdShade/tokens.h"
#include "USDIncludesEnd.h"


void FUsdShadeMaterialTranslator::CreateAssets()
{
	pxr::UsdShadeMaterial ShadeMaterial( GetPrim() );

	if ( !ShadeMaterial )
	{
		return;
	}

	TRACE_CPUPROFILER_EVENT_SCOPE( FUsdShadeMaterialTranslator::CreateAssets );

	const pxr::TfToken RenderContextToken =
		Context->RenderContext.IsNone() ?
			pxr::UsdShadeTokens->universalRenderContext :
			UnrealToUsd::ConvertToken( *Context->RenderContext.ToString() ).Get();
	FString MaterialHashString = UsdUtils::HashShadeMaterial( ShadeMaterial, RenderContextToken ).ToString();

	UMaterialInterface* ConvertedMaterial = nullptr;

	if ( Context->AssetCache )
	{
		ConvertedMaterial = Cast< UMaterialInterface >( Context->AssetCache->GetCachedAsset( MaterialHashString ) );
	}

	if ( !ConvertedMaterial )
	{
		const bool bIsTranslucent = UsdUtils::IsMaterialTranslucent( ShadeMaterial );
		const bool bNeedsVirtualTextures = UsdUtils::IsMaterialUsingUDIMs( ShadeMaterial );

		FString BaseMaterialName = TEXT("UsdPreviewSurface");

		if ( bIsTranslucent )
		{
			BaseMaterialName += TEXT("Translucent");
		}

		if ( bNeedsVirtualTextures )
		{
			BaseMaterialName += TEXT("VT");
		}

		const FString BaseMaterialPath = FString::Printf( TEXT("Material'/USDImporter/Materials/%s.%s"), *BaseMaterialName, *BaseMaterialName );

		if ( UMaterialInterface* BaseMaterial = Cast< UMaterialInterface >( FSoftObjectPath( BaseMaterialPath ).TryLoad() ) )
		{
			FName InstanceName = MakeUniqueObjectName( GetTransientPackage(), UMaterialInstance::StaticClass(), *FPaths::GetBaseFilename( PrimPath.GetString() ) );
			if ( GIsEditor ) // Also have to prevent Standalone game from going with MaterialInstanceConstants
			{
#if WITH_EDITOR
				if ( UMaterialInstanceConstant* NewMaterial = NewObject<UMaterialInstanceConstant>( GetTransientPackage(), InstanceName, Context->ObjectFlags ) )
				{
					NewMaterial->SetParentEditorOnly( BaseMaterial );

					UUsdAssetImportData* ImportData = NewObject< UUsdAssetImportData >( NewMaterial, TEXT( "USDAssetImportData" ) );
					ImportData->PrimPath = PrimPath.GetString();
					NewMaterial->AssetImportData = ImportData;

					TMap<FString, int32> Unused;
					TMap<FString, int32>& PrimvarToUVIndex = Context->MaterialToPrimvarToUVIndex ? Context->MaterialToPrimvarToUVIndex->FindOrAdd( PrimPath.GetString() ) : Unused;

					if ( UsdToUnreal::ConvertMaterial( ShadeMaterial, *NewMaterial, Context->AssetCache.Get(), PrimvarToUVIndex, *Context->RenderContext.ToString() ) )
					{
						// We can't blindly recreate all component render states when a level is being added, because we may end up first creating
						// render states for some components, and UWorld::AddToWorld calls FScene::AddPrimitive which expects the component to not have
						// primitives yet
						FMaterialUpdateContext::EOptions::Type Options = FMaterialUpdateContext::EOptions::Default;
						if ( Context->Level->bIsAssociatingLevel )
						{
							Options = ( FMaterialUpdateContext::EOptions::Type ) ( Options & ~FMaterialUpdateContext::EOptions::RecreateRenderStates );
						}

						FMaterialUpdateContext UpdateContext( Options, GMaxRHIShaderPlatform );
						UpdateContext.AddMaterialInstance( NewMaterial );
						NewMaterial->PreEditChange( nullptr );
						NewMaterial->PostEditChange();

						ConvertedMaterial = NewMaterial;
					}
				}
#endif // WITH_EDITOR
			}
			else if ( UMaterialInstanceDynamic* NewMaterial = UMaterialInstanceDynamic::Create( BaseMaterial, GetTransientPackage(), InstanceName ) )
			{
				TMap<FString, int32> Unused;
				TMap<FString, int32>& PrimvarToUVIndex = Context->MaterialToPrimvarToUVIndex ? Context->MaterialToPrimvarToUVIndex->FindOrAdd( PrimPath.GetString() ) : Unused;

				if ( UsdToUnreal::ConvertMaterial( ShadeMaterial, *NewMaterial, Context->AssetCache.Get(), PrimvarToUVIndex, *Context->RenderContext.ToString() ) )
				{
					ConvertedMaterial = NewMaterial;
				}
			}
		}
	}
	else if ( Context->MaterialToPrimvarToUVIndex && Context->AssetCache )
	{
		if ( TMap<FString, int32>* PrimvarToUVIndex = Context->MaterialToPrimvarToUVIndex->Find( Context->AssetCache->GetPrimForAsset( ConvertedMaterial ) ) )
		{
			// Copy the Material -> Primvar -> UV index mapping from the cached material prim path to this prim path
			Context->MaterialToPrimvarToUVIndex->FindOrAdd( PrimPath.GetString() ) = *PrimvarToUVIndex;
		}
	}

	if ( ConvertedMaterial )
	{
		Context->AssetCache->CacheAsset( MaterialHashString, ConvertedMaterial, PrimPath.GetString() );
	}
}

#endif // #if USE_USD_SDK
