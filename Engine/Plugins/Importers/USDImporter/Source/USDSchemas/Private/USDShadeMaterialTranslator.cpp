// Copyright Epic Games, Inc. All Rights Reserved.

#include "USDShadeMaterialTranslator.h"

#include "MeshTranslationImpl.h"
#include "USDAssetCache.h"
#include "USDAssetImportData.h"
#include "USDClassesModule.h"
#include "USDClassesModule.h"
#include "USDLog.h"
#include "USDShadeConversion.h"
#include "USDTypesConversion.h"

#include "UsdWrappers/SdfPath.h"

#include "Engine/Level.h"
#include "Engine/Texture.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "MaterialShared.h"
#include "Misc/SecureHash.h"

#if USE_USD_SDK

#include "USDIncludesStart.h"
	#include "pxr/base/tf/token.h"
	#include "pxr/usd/usdShade/material.h"
	#include "pxr/usd/usdShade/tokens.h"
#include "USDIncludesEnd.h"

namespace UE::UsdShadeTranslator::Private
{
	void RecursiveUpgradeMaterialsAndTexturesToVT(
		const TSet<UTexture*>& TexturesToUpgrade,
		const TSharedRef< FUsdSchemaTranslationContext >& Context,
		TSet<UMaterialInterface*>& VisitedMaterials,
		TSet<UMaterialInterface*>& NewMaterials
	)
	{
		for ( UTexture* Texture : TexturesToUpgrade )
		{
			if ( Texture->VirtualTextureStreaming )
			{
				continue;
			}

			UE_LOG( LogUsd, Log, TEXT( "Upgrading texture '%s' to VT as it is used by a material that must be VT" ),
				*Texture->GetName()
			);
			FPropertyChangedEvent PropertyChangeEvent( UTexture::StaticClass()->FindPropertyByName( GET_MEMBER_NAME_CHECKED( UTexture, VirtualTextureStreaming ) ) );
			Texture->Modify();
			Texture->VirtualTextureStreaming = true;

#if WITH_EDITOR
			Texture->PostEditChangeProperty( PropertyChangeEvent );
#endif // WITH_EDITOR

			// Now that our texture is VT, all materials that use the texture must be VT
			if ( const TSet<UMaterialInterface*>* UserMaterials = Context->TextureToUserMaterials.Find( Texture ) )
			{
				for ( UMaterialInterface* UserMaterial : *UserMaterials )
				{
					if ( VisitedMaterials.Contains( UserMaterial ) )
					{
						continue;
					}

					if ( UMaterialInstance* MaterialInstance = Cast<UMaterialInstance>( UserMaterial ) )
					{
						// Important to not use GetBaseMaterial() here because if our parent is the translucent we'll
						// get the reference UsdPreviewSurface instead, as that is also *its* reference
						UMaterialInterface* ReferenceMaterial = MaterialInstance->Parent.Get();
						UMaterialInterface* ReferenceMaterialVT =
							MeshTranslationImpl::GetVTVersionOfReferencePreviewSurfaceMaterial( ReferenceMaterial );
						if ( ReferenceMaterial == ReferenceMaterialVT )
						{
							// Material is already VT, we're good
							continue;
						}

						// Visit it before we start recursing. We need this because we must convert textures to VT
						// before materials (or else we get a warning) but we'll only actually swap the reference material
						// at the end of this scope
						VisitedMaterials.Add( UserMaterial );

						// If we're going to update this material to VT, all of *its* textures need to be VT too
						TSet<UTexture*> OtherUsedTextures;
						for ( const FTextureParameterValue& TextureValue : MaterialInstance->TextureParameterValues )
						{
							if ( UTexture* OtherTexture = TextureValue.ParameterValue )
							{
								OtherUsedTextures.Add( OtherTexture );
							}
						}

						RecursiveUpgradeMaterialsAndTexturesToVT(
							OtherUsedTextures,
							Context,
							VisitedMaterials,
							NewMaterials
						);

						// We can't blindly recreate all component render states when a level is being added, because
						// we may end up first creating render states for some components, and UWorld::AddToWorld
						// calls FScene::AddPrimitive which expects the component to not have primitives yet
						FMaterialUpdateContext::EOptions::Type Options = FMaterialUpdateContext::EOptions::Default;
						if ( Context->Level->bIsAssociatingLevel )
						{
							Options = ( FMaterialUpdateContext::EOptions::Type ) (
								Options & ~FMaterialUpdateContext::EOptions::RecreateRenderStates
							);
						}

						UE_LOG( LogUsd, Log, TEXT( "Upgrading material instance '%s' to having a VT reference as texture '%s' requires it" ),
							*MaterialInstance->GetName(),
							*Texture->GetName()
						);

#if WITH_EDITOR
						UMaterialInstanceConstant* MIC = Cast<UMaterialInstanceConstant>( MaterialInstance );
						if ( GIsEditor && MIC )
						{
							FMaterialUpdateContext UpdateContext( Options, GMaxRHIShaderPlatform );
							UpdateContext.AddMaterialInstance( MIC );
							MIC->PreEditChange( nullptr );
							MIC->SetParentEditorOnly( ReferenceMaterialVT );
							MIC->PostEditChange();
						}
						else
#endif // WITH_EDITOR
						if ( UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>( UserMaterial ) )
						{
							if ( Context->AssetCache && Context->InfoCache )
							{
								UE::FSdfPath PrimPath;
								for (const UE::FSdfPath& Path : Context->InfoCache->GetPrimsForAsset(MID))
								{
									PrimPath = Path;
									break;
								}
								const FString Hash = Context->AssetCache->GetHashForAsset( MID );

								const FName NewInstanceName = MakeUniqueObjectName(
									GetTransientPackage(),
									UMaterialInstance::StaticClass(),
									PrimPath.IsEmpty()
										? TEXT("MaterialInstance")
										: *IUsdClassesModule::SanitizeObjectName(FPaths::GetBaseFilename(PrimPath.GetString()))
								);

								// For MID we can't swap the reference material, so we need to create a brand new one and copy
								// the overrides
								UMaterialInstanceDynamic* NewMID = UMaterialInstanceDynamic::Create(
									ReferenceMaterialVT,
									GetTransientPackage(),
									NewInstanceName
								);
								if ( !ensure( NewMID ) )
								{
									continue;
								}
								NewMID->CopyParameterOverrides( MID );

								if (Context->AssetCache->CanRemoveAsset(Hash) && Context->AssetCache->RemoveAsset(Hash))
								{
									Context->AssetCache->CacheAsset(Hash, NewMID);
									if (!PrimPath.IsEmpty())
									{
										Context->InfoCache->LinkAssetToPrim(PrimPath, NewMID);
									}
									NewMaterials.Add( NewMID );
								}
							}
						}
						else
						{
							// This should never happen
							ensure( false );
						}
					}
				}
			}
		}
	}

	void UpgradeMaterialsAndTexturesToVT( TSet<UTexture*> TexturesToUpgrade, TSharedRef< FUsdSchemaTranslationContext >& Context )
	{
		TSet<UMaterialInterface*> VisitedMaterials;
		TSet<UMaterialInterface*> NewMaterials;
		RecursiveUpgradeMaterialsAndTexturesToVT( TexturesToUpgrade, Context, VisitedMaterials, NewMaterials );

		// When we "visit" a MID we'll create a brand new instance of it and discard the old one, so let's drop the old ones
		// from Context->TextureToUserMaterials too
		for ( UMaterialInterface* Material : VisitedMaterials )
		{
			if ( UMaterialInstanceDynamic* OldMID = Cast<UMaterialInstanceDynamic>( Material ) )
			{
				for ( TPair<UTexture*, TSet<UMaterialInterface*>>& Pair : Context->TextureToUserMaterials )
				{
					Pair.Value.Remove( Material );
				}
			}
		}

		// Additionally now we need to add those new MIDs we created back into Context->TextureToUserMaterials
		for ( UMaterialInterface* Material : NewMaterials )
		{
			UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>( Material );
			if ( ensure( MID ) )
			{
				for ( const FTextureParameterValue& TextureValue : MID->TextureParameterValues )
				{
					if ( UTexture* Texture = TextureValue.ParameterValue )
					{
						Context->TextureToUserMaterials.FindOrAdd( Texture ).Add( MID );
					}
				}
			}
		}
	}
}

void FUsdShadeMaterialTranslator::CreateAssets()
{
	TRACE_CPUPROFILER_EVENT_SCOPE( FUsdShadeMaterialTranslator::CreateAssets );

	pxr::UsdShadeMaterial ShadeMaterial( GetPrim() );

	if ( !ShadeMaterial )
	{
		return;
	}

	if (Context->bTranslateOnlyUsedMaterials && Context->InfoCache)
	{
		if (!Context->InfoCache->IsMaterialUsed(PrimPath))
		{
			UE_LOG(LogUsd, Verbose, TEXT("Skipping creating assets for material prim '%s' as it is not currently bound by any prim."), *PrimPath.GetString());
			return;
		}
	}

	RegisterAuxiliaryPrims();

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
		const FString PrimPathString = PrimPath.GetString();
		const bool bIsTranslucent = UsdUtils::IsMaterialTranslucent( ShadeMaterial );
		const FName InstanceName = MakeUniqueObjectName(
			GetTransientPackage(),
			UMaterialInstance::StaticClass(),
			*IUsdClassesModule::SanitizeObjectName(FPaths::GetBaseFilename(PrimPathString))
		);

		// TODO: There is an issue here: Note how we're only ever going to write the material prim's primvars into
		// this PrimvarToUVIndex map when first creating the material, and not if we find it in the asset cache.
		// This because finding the primvars to use essentially involves parsing the entire material again, so we
		// likely shouldn't do it every time.
		// This means that a mesh with float2 UV sets and materials that use them will likely end up with no UVs once
		// the stage reloads...
		TMap<FString, int32> Unused;
		TMap<FString, int32>& PrimvarToUVIndex = Context->MaterialToPrimvarToUVIndex
			? Context->MaterialToPrimvarToUVIndex->FindOrAdd(PrimPathString)
			: Unused;

#if WITH_EDITOR
		if ( GIsEditor ) // Also have to prevent Standalone game from going with MaterialInstanceConstants
		{
			if ( UMaterialInstanceConstant* NewMaterial = NewObject<UMaterialInstanceConstant>( GetTransientPackage(), InstanceName, Context->ObjectFlags | EObjectFlags::RF_Transient ) )
			{
				UUsdAssetImportData* ImportData = NewObject< UUsdAssetImportData >( NewMaterial, TEXT( "USDAssetImportData" ) );
				ImportData->PrimPath = PrimPath.GetString();
				NewMaterial->AssetImportData = ImportData;

				const bool bSuccess = UsdToUnreal::ConvertMaterial(
					ShadeMaterial,
					*NewMaterial,
					Context->AssetCache.Get(),
					PrimvarToUVIndex,
					*Context->RenderContext.ToString()
				);
				if ( !bSuccess )
				{
					return;
				}

				TSet<UTexture*> VTTextures;
				TSet<UTexture*> NonVTTextures;
				for ( const FTextureParameterValue& TextureValue : NewMaterial->TextureParameterValues )
				{
					if ( UTexture* Texture = TextureValue.ParameterValue )
					{
						if ( Texture->VirtualTextureStreaming )
						{
							UsdUtils::NotifyIfVirtualTexturesNeeded( Texture );
							VTTextures.Add( Texture );
						}
						else
						{
							NonVTTextures.Add( Texture );
						}
					}
				}

				// Our VT material only has VT texture samplers, so *all* of its textures must be VT
				if ( VTTextures.Num() && NonVTTextures.Num() )
				{
					UE_LOG( LogUsd, Log, TEXT( "Upgrading textures used by material instance '%s' to VT as the material must be VT" ),
						*NewMaterial->GetName()
					);
					UE::UsdShadeTranslator::Private::UpgradeMaterialsAndTexturesToVT( NonVTTextures, Context );
				}

				MeshTranslationImpl::EUsdReferenceMaterialProperties Properties = MeshTranslationImpl::EUsdReferenceMaterialProperties::None;
				if ( bIsTranslucent )
				{
					Properties |= MeshTranslationImpl::EUsdReferenceMaterialProperties::Translucent;
				}
				if ( VTTextures.Num() > 0 )
				{
					Properties |= MeshTranslationImpl::EUsdReferenceMaterialProperties::VT;
				}
				UMaterialInterface* ReferenceMaterial = MeshTranslationImpl::GetReferencePreviewSurfaceMaterial( Properties );

				if ( ensure( ReferenceMaterial ) )
				{
					NewMaterial->SetParentEditorOnly( ReferenceMaterial );

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

					for ( UTexture* Texture : VTTextures.Union( NonVTTextures ) )
					{
						Context->TextureToUserMaterials.FindOrAdd( Texture ).Add( NewMaterial );
					}
				}
			}
		}
		else
#endif // WITH_EDITOR
		{
			// At runtime we always start with a non-VT reference and if we discover we need one we just create a new MID
			// using the VT reference and copy the overrides. Not much else we can do as we need a reference to call
			// UMaterialInstanceDynamic::Create and get our instance, but we an instance to call UsdToUnreal::ConvertMaterial
			// to create our textures and decide on our reference.
			MeshTranslationImpl::EUsdReferenceMaterialProperties Properties = MeshTranslationImpl::EUsdReferenceMaterialProperties::None;
			if ( bIsTranslucent )
			{
				Properties |= MeshTranslationImpl::EUsdReferenceMaterialProperties::Translucent;
			}
			UMaterialInterface* ReferenceMaterial = MeshTranslationImpl::GetReferencePreviewSurfaceMaterial( Properties );

			if ( ensure( ReferenceMaterial ) )
			{
				if ( UMaterialInstanceDynamic* NewMaterial = UMaterialInstanceDynamic::Create( ReferenceMaterial, GetTransientPackage(), InstanceName ) )
				{
					NewMaterial->SetFlags(RF_Transient);

					if ( UsdToUnreal::ConvertMaterial( ShadeMaterial, *NewMaterial, Context->AssetCache.Get(), PrimvarToUVIndex, *Context->RenderContext.ToString() ) )
					{
						TSet<UTexture*> VTTextures;
						TSet<UTexture*> NonVTTextures;
						for ( const FTextureParameterValue& TextureValue : NewMaterial->TextureParameterValues )
						{
							if ( UTexture* Texture = TextureValue.ParameterValue )
							{
								if ( Texture->VirtualTextureStreaming )
								{
									VTTextures.Add( Texture );
								}
								else
								{
									NonVTTextures.Add( Texture );
								}
							}
						}

						// We must stash our material and textures *before* we call UpgradeMaterialsAndTexturesToVT, as that
						// is what will actually swap our reference with a VT one if needed
						if ( Context->AssetCache && Context->InfoCache )
						{
							Context->AssetCache->CacheAsset(MaterialHashString, NewMaterial);
							Context->InfoCache->LinkAssetToPrim(PrimPath, NewMaterial);
						}
						for ( UTexture* Texture : VTTextures.Union( NonVTTextures ) )
						{
							Context->TextureToUserMaterials.FindOrAdd( Texture ).Add( NewMaterial );
						}

						// Our VT material only has VT texture samplers, so *all* of its textures must be VT
						if ( VTTextures.Num() && NonVTTextures.Num() )
						{
							UE_LOG( LogUsd, Log, TEXT( "Upgrading textures used by material instance '%s' to VT as the material must be VT" ),
								*NewMaterial->GetName()
							);
							UE::UsdShadeTranslator::Private::UpgradeMaterialsAndTexturesToVT( NonVTTextures, Context );
						}

						// We must go through the cache to fetch our result material here as UpgradeMaterialsAndTexturesToVT
						// may have created a new MID for this material with a VT reference
						ConvertedMaterial = Cast<UMaterialInterface>( Context->AssetCache->GetCachedAsset( MaterialHashString ) );
					}
				}
			}
		}
	}
	else if ( Context->MaterialToPrimvarToUVIndex && Context->InfoCache )
	{
		const TSet<UE::FSdfPath> FoundPrimPaths = Context->InfoCache->GetPrimsForAsset(ConvertedMaterial);
		if (FoundPrimPaths.Num() > 0)
		{
			const UE::FSdfPath& FoundPrimPath = *FoundPrimPaths.CreateConstIterator();

			if (TMap<FString, int32>* PrimvarToUVIndex = Context->MaterialToPrimvarToUVIndex->Find(FoundPrimPath.GetString()))
			{
				// Copy the Material -> Primvar -> UV index mapping from the cached material prim path to this prim path
				Context->MaterialToPrimvarToUVIndex->FindOrAdd(PrimPath.GetString()) = *PrimvarToUVIndex;
			}
		}
	}

	PostImportMaterial(MaterialHashString, ConvertedMaterial);
}

bool FUsdShadeMaterialTranslator::CollapsesChildren( ECollapsingType CollapsingType ) const
{
	return false;
}

bool FUsdShadeMaterialTranslator::CanBeCollapsed( ECollapsingType CollapsingType ) const
{
	return false;
}

void FUsdShadeMaterialTranslator::PostImportMaterial(const FString& MaterialHash, UMaterialInterface* ImportedMaterial)
{
	if (!ImportedMaterial || !Context->InfoCache || !Context->AssetCache)
	{
		return;
	}

#if WITH_EDITOR
	UUsdAssetImportData* ImportData = Cast<UUsdAssetImportData>(ImportedMaterial->AssetImportData);
	if (!ImportData)
	{
		ImportData = NewObject<UUsdAssetImportData>(ImportedMaterial, TEXT("USDAssetImportData"));
		ImportedMaterial->AssetImportData = ImportData;
	}
	ImportData->PrimPath = PrimPath.GetString();
#endif // WITH_EDITOR

	// Note that this needs to run even if we found this material in the asset cache already, otherwise we won't
	// re-register the prim asset links when we reload a stage
	Context->AssetCache->CacheAsset(MaterialHash, ImportedMaterial);
	Context->InfoCache->LinkAssetToPrim(PrimPath, ImportedMaterial);

	// Also link the textures to the same material prim.
	// This is important because it lets the stage actor drop its references to old unused textures in the
	// asset cache if they aren't being used by any other material
	TSet<UObject*> Dependencies = IUsdClassesModule::GetAssetDependencies(ImportedMaterial);
	for (UObject* Object : Dependencies)
	{
		if (UTexture* Texture = Cast<UTexture>(Object))
		{
			// We don't use "GetOutermost()" here because it's also possible to be owned by an asset cache that
			// itself lives in the transient package... bIsOwnedByTransientPackage should be true just for new textures
			// dumped on the transient package
			const bool bIsOwnedByTransientPackage = Texture->GetOuter()->GetPackage() == GetTransientPackage();
			const bool bIsOwnedByCache = Context->AssetCache->IsAssetOwnedByCache(Texture->GetPathName());

			// Texture is already owned by the cache: Just touch it without recomputing its hash as that's expensive
			if (bIsOwnedByCache)
			{
				Context->AssetCache->TouchAsset(Texture);
			}
			// Texture is owned by the transient package, but not cached yet: Let's take it
			else if (bIsOwnedByTransientPackage)
			{
				FString FilePath;
#if WITH_EDITOR
				FilePath = Texture->AssetImportData ? Texture->AssetImportData->GetFirstFilename() : Texture->GetName();
#else
				FilePath = Texture->GetName();
#endif // WITH_EDITOR
				const FString TextureHash = UsdUtils::GetTextureHash(
					FilePath,
					Texture->SRGB,
					Texture->CompressionSettings,
					Texture->GetTextureAddressX(),
					Texture->GetTextureAddressY()
				);

				// Some translators like FMaterialXUsdShadeMaterialTranslator will import many materials and textures
				// at once and preload them all in the cache. In some complex scenarios when some materials are updated
				// it is possible to arrive at a situation where the we need to reparse the source file again and
				// regenerate a bunch of materials and textures without access to the asset cache. In those cases we'll
				// unfortunately recreate identical textures, and if we try to store them in here we'll run into a hash
				// collision. These should be rare, however, and require repeatedly e.g. updating the Material prims
				// generated by MaterialX or updating the MaterialX import options
				UTexture* ExistingTexture = Cast<UTexture>(Context->AssetCache->GetCachedAsset(TextureHash));
				if (!ExistingTexture)
				{
					Texture->SetFlags(RF_Transient);
					Context->AssetCache->CacheAsset(TextureHash, Texture);
				}
			}

			if (bIsOwnedByCache || bIsOwnedByTransientPackage)
			{
#if WITH_EDITOR
				UUsdAssetImportData* TextureImportData = Cast<UUsdAssetImportData>(Texture->AssetImportData);
				if (!TextureImportData)
				{
					TextureImportData = NewObject<UUsdAssetImportData>(ImportedMaterial, TEXT("USDAssetImportData"));
					Texture->AssetImportData = TextureImportData;
				}
				TextureImportData->PrimPath = PrimPath.GetString();
#endif // WITH_EDITOR

				Context->InfoCache->LinkAssetToPrim(PrimPath, Texture);
			}
		}
		else if (UMaterialInterface* ReferenceMaterial = Cast<UMaterialInterface>(Object))
		{
			// Some scenarios can generate reference/instance material pairs, and reference materials are dependencies.
			// We won't handle these dependencies recursively though, the caller is responsible for calling this for
			// all individual materials as they need to also provide the hash to use for each
			UMaterialInstance* Instance = Cast<UMaterialInstance>(ImportedMaterial);
			ensure(Instance && Instance->Parent.Get() == ReferenceMaterial);
		}
		else
		{
			ensureMsgf(false, TEXT("Asset type unsupported!"));
		}
	}
}

TSet<UE::FSdfPath> FUsdShadeMaterialTranslator::CollectAuxiliaryPrims() const
{
	TSet<UE::FSdfPath> Result;
	{
		TFunction<void(const pxr::UsdShadeInput&)> TraverseShadeInput;
		TraverseShadeInput = [&TraverseShadeInput, &Result](const pxr::UsdShadeInput& ShadeInput)
		{
			if (!ShadeInput)
			{
				return;
			}

			pxr::UsdShadeConnectableAPI Source;
			pxr::TfToken SourceName;
			pxr::UsdShadeAttributeType AttributeType;
			if (pxr::UsdShadeConnectableAPI::GetConnectedSource(ShadeInput.GetAttr(), &Source, &SourceName, &AttributeType))
			{
				pxr::UsdPrim ConnectedPrim = Source.GetPrim();
				UE::FSdfPath ConnectedPrimPath = UE::FSdfPath{ConnectedPrim.GetPrimPath()};

				if (!Result.Contains(ConnectedPrimPath))
				{
					Result.Add(ConnectedPrimPath);

					for (const pxr::UsdShadeInput& ChildInput : Source.GetInputs())
					{
						TraverseShadeInput(ChildInput);
					}
				}
			}
		};

		FScopedUsdAllocs UsdAllocs;

		pxr::UsdPrim Prim = GetPrim();
		pxr::UsdShadeMaterial UsdShadeMaterial{Prim};
		if (!UsdShadeMaterial)
		{
			return {};
		}

		const pxr::TfToken RenderContextToken = Context->RenderContext.IsNone()
			? pxr::UsdShadeTokens->universalRenderContext
			: UnrealToUsd::ConvertToken(*Context->RenderContext.ToString()).Get();
		pxr::UsdShadeShader SurfaceShader = UsdShadeMaterial.ComputeSurfaceSource({RenderContextToken});
		if (!SurfaceShader)
		{
			return {};
		}

		Result.Add(UE::FSdfPath{SurfaceShader.GetPrim().GetPrimPath()});

		for (const pxr::UsdShadeInput& ShadeInput : SurfaceShader.GetInputs())
		{
			TraverseShadeInput(ShadeInput);
		}
	}
	return Result;
}

#endif // #if USE_USD_SDK
