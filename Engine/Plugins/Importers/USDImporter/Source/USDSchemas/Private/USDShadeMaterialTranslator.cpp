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

namespace UE
{
	namespace UsdShadeMaterialTranslator
	{
		namespace Private
		{
			bool IsMaterialUsingUDIMs(const pxr::UsdShadeMaterial& UsdShadeMaterial)
			{
				FScopedUsdAllocs UsdAllocs;

				pxr::UsdShadeShader SurfaceShader = UsdShadeMaterial.ComputeSurfaceSource();
				if (!SurfaceShader)
				{
					return false;
				}

				for (const pxr::UsdShadeInput& ShadeInput : SurfaceShader.GetInputs())
				{
					pxr::UsdShadeConnectableAPI Source;
					pxr::TfToken SourceName;
					pxr::UsdShadeAttributeType AttributeType;

					if (ShadeInput.GetConnectedSource(&Source, &SourceName, &AttributeType))
					{
						pxr::UsdShadeInput FileInput;

						// UsdUVTexture: Get its file input
						if (AttributeType == pxr::UsdShadeAttributeType::Output)
						{
							FileInput = Source.GetInput(UnrealIdentifiers::File);
						}
						// Check if we are being directly passed an asset
						else
						{
							FileInput = Source.GetInput(SourceName);
						}

						if (FileInput && FileInput.GetTypeName() == pxr::SdfValueTypeNames->Asset) // Check that FileInput is of type Asset
						{
							pxr::SdfAssetPath TextureAssetPath;
							FileInput.GetAttr().Get< pxr::SdfAssetPath >(&TextureAssetPath);

							FString TexturePath = UsdToUnreal::ConvertString(TextureAssetPath.GetAssetPath());

							if (TexturePath.Contains(TEXT("<UDIM>")))
							{
								return true;
							}
						}
					}
				}

				return false;
			}
		}
	}
}
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
		const bool bIsTranslucent = UsdUtils::IsMaterialTranslucent( ShadeMaterial );
		const bool bNeedsVirtualTextures = UE::UsdShadeMaterialTranslator::Private::IsMaterialUsingUDIMs( ShadeMaterial );

		FString MasterMaterialName = TEXT("UsdPreviewSurface");

		if ( bIsTranslucent )
		{
			MasterMaterialName += TEXT("Translucent");
		}

		if ( bNeedsVirtualTextures )
		{
			MasterMaterialName += TEXT("VT");
		}

		const FString MasterMaterialPath = FString::Printf( TEXT("Material'/USDImporter/Materials/%s.%s"), *MasterMaterialName, *MasterMaterialName );

		if ( UMaterialInterface* MasterMaterial = Cast< UMaterialInterface >( FSoftObjectPath( MasterMaterialPath ).TryLoad() ) )
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
	else if ( Context->MaterialToPrimvarToUVIndex && Context->AssetCache )
	{
		const TMap<FString, UObject*> AssetPrimLinks = Context->AssetCache->GetAssetPrimLinks();
		if ( const FString* PrimPathForCachedAsset = AssetPrimLinks.FindKey( ConvertedMaterial ) )
		{
			if ( TMap<FString, int32>* PrimvarToUVIndex = Context->MaterialToPrimvarToUVIndex->Find(*PrimPathForCachedAsset) )
			{
				// Copy the Material -> Primvar -> UV index mapping from the cached material prim path to this prim path
				Context->MaterialToPrimvarToUVIndex->FindOrAdd(PrimPath.GetString()) = *PrimvarToUVIndex;
			}
		}
	}

	if ( ConvertedMaterial )
	{
		Context->AssetCache->CacheAsset( MaterialHashString, ConvertedMaterial, PrimPath.GetString() );
	}
}

#endif // #if USE_USD_SDK
