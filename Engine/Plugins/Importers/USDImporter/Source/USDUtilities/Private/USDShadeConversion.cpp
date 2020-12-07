// Copyright Epic Games, Inc. All Rights Reserved.

#include "USDShadeConversion.h"

#if USE_USD_SDK

#include "USDAssetImportData.h"
#include "USDConversionUtils.h"
#include "USDLayerUtils.h"
#include "USDLog.h"
#include "USDMemory.h"
#include "USDTypesConversion.h"

#include "UsdWrappers/SdfLayer.h"

#include "Engine/Texture2D.h"
#include "Factories/TextureFactory.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "IMaterialBakingModule.h"
#include "MaterialBakingStructures.h"
#include "MaterialEditingLibrary.h"
#include "MaterialOptions.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionConstant4Vector.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Misc/FileHelper.h"
#include "Modules/ModuleManager.h"

#include "USDIncludesStart.h"
	#include "pxr/usd/ar/asset.h"
	#include "pxr/usd/ar/resolver.h"
	#include "pxr/usd/ar/resolverScopedCache.h"
	#include "pxr/usd/sdf/layer.h"
	#include "pxr/usd/sdf/layerUtils.h"
	#include "pxr/usd/usdShade/material.h"
#include "USDIncludesEnd.h"

namespace UsdShadeConversionImpl
{
	// Given an AssetPath, resolve it to an actual file path
	FString ResolveAssetPath( const pxr::SdfLayerHandle& LayerHandle, const FString& AssetPath )
	{
		FScopedUsdAllocs UsdAllocs;

		FString AssetPathToResolve = AssetPath;
		AssetPathToResolve.ReplaceInline( TEXT("<UDIM>"), TEXT("1001") ); // If it's a UDIM path, we replace the UDIM tag with the start tile

		pxr::ArResolverScopedCache ResolverCache;
		pxr::ArResolver& Resolver = pxr::ArGetResolver();

		const FString RelativePathToResolve =
			LayerHandle
			? UsdToUnreal::ConvertString( pxr::SdfComputeAssetPathRelativeToLayer( LayerHandle, UnrealToUsd::ConvertString( *AssetPathToResolve ).Get() ) )
			: AssetPathToResolve;

		std::string ResolvedPathUsd = Resolver.Resolve( UnrealToUsd::ConvertString( *RelativePathToResolve ).Get().c_str() );

		FString ResolvedAssetPath = UsdToUnreal::ConvertString( ResolvedPathUsd );

		return ResolvedAssetPath;
	}

	// If ResolvedTexturePath is in a format like "C:/MyFiles/scene.usdz[0/texture.png]", this will place "png" in OutExtensionNoDot, and return true
	bool IsInsideUsdzArchive(const FString& ResolvedTexturePath, FString& OutExtensionNoDot)
	{
		// We need at least an opening and closing bracket
		if ( ResolvedTexturePath.Len() < 3 )
		{
			return false;
		}

		int32 OpenBracketPos = ResolvedTexturePath.Find( TEXT( "[" ), ESearchCase::IgnoreCase, ESearchDir::FromEnd );
		int32 CloseBracketPos = ResolvedTexturePath.Find( TEXT( "]" ), ESearchCase::IgnoreCase, ESearchDir::FromEnd );
		if ( OpenBracketPos != INDEX_NONE && CloseBracketPos != INDEX_NONE && CloseBracketPos > OpenBracketPos - 1 )
		{
			// Should be like "C:/MyFiles/scene.usdz"
			FString UsdzFilePath = ResolvedTexturePath.Left( OpenBracketPos );

			// Should be like "0/texture.png"
			FString TextureInUsdzPath = ResolvedTexturePath.Mid( OpenBracketPos + 1, CloseBracketPos - OpenBracketPos - 1 );

			if ( FPaths::GetExtension( UsdzFilePath ).ToLower() == TEXT( "usdz" ) )
			{
				// Should be something like "png"
				OutExtensionNoDot = FPaths::GetExtension(TextureInUsdzPath);
				return true;
			}
		}

		return false;
	}

	/** If ResolvedTexturePath points at a texture inside an usdz file, this will use USD to pull the asset from the file, and TextureFactory to import it directly from the binary buffer */
	UTexture* ReadTextureFromUsdzArchive(const FString& ResolvedTexturePath, UTextureFactory* TextureFactory )
	{
		FString TextureExtension;
		if ( !IsInsideUsdzArchive( ResolvedTexturePath, TextureExtension ) )
		{
			return nullptr;
		}

		pxr::ArResolver& Resolver = pxr::ArGetResolver();
		std::shared_ptr<pxr::ArAsset> Asset = Resolver.OpenAsset( UnrealToUsd::ConvertString( *ResolvedTexturePath ).Get() );

		if ( Asset )
		{
			std::shared_ptr<const char> Buffer;
			uint64 BufferSize = static_cast< uint64 >( Asset->GetSize() );
			const uint8* BufferStart;
			{
				FScopedUsdAllocs Allocs;

				Buffer = Asset->GetBuffer();
				BufferStart = reinterpret_cast<const uint8*>( Buffer.get() );
			}

			if ( BufferStart && BufferSize > 0 )
			{
				return Cast<UTexture>(TextureFactory->FactoryCreateBinary(
					UTexture::StaticClass(),
					GetTransientPackage(),
					NAME_None,
					RF_Transient,
					nullptr,
					*TextureExtension,
					BufferStart,
					BufferStart + BufferSize,
					nullptr));
			}
		}

		return nullptr;
	}

	// Will traverse the shade material graph backwards looking for a string/token value and return it.
	// Returns the empty string if it didn't find anything.
	FString RecursivelySearchForStringValue( pxr::UsdShadeInput Input )
	{
		if ( !Input )
		{
			return {};
		}

		FScopedUsdAllocs Allocs;

		if ( Input.HasConnectedSource() )
		{
			pxr::UsdShadeConnectableAPI Souce;
			pxr::TfToken SourceName;
			pxr::UsdShadeAttributeType SourceType;
			if ( Input.GetConnectedSource( &Souce, &SourceName, &SourceType ) )
			{
				for ( const pxr::UsdShadeInput& DeeperInput : Souce.GetInputs() )
				{
					FString Result = RecursivelySearchForStringValue( DeeperInput );
					if ( !Result.IsEmpty() )
					{
						return Result;
					}
				}
			}
		}
		else
		{
			std::string StringValue;
			if ( Input.Get< std::string >( &StringValue ) )
			{
				return UsdToUnreal::ConvertString( StringValue );
			}

			pxr::TfToken TokenValue;
			if ( Input.Get< pxr::TfToken >( &TokenValue ) )
			{
				return UsdToUnreal::ConvertToken( TokenValue );
			}
		}

		return {};
	}

	/** Receives a UsdUVTexture shader, and returns the name of the primvar that it is using as 'st' */
	FString GetPrimvarUsedAsST( pxr::UsdShadeConnectableAPI& UsdUVTexture )
	{
		FScopedUsdAllocs Allocs;

		pxr::UsdShadeInput StInput = UsdUVTexture.GetInput( UnrealIdentifiers::St );
		if ( !StInput )
		{
			return {};
		}

		pxr::UsdShadeConnectableAPI PrimvarReader;
		pxr::TfToken PrimvarReaderId;
		pxr::UsdShadeAttributeType AttributeType;

		// Connected to a PrimvarReader
		if ( StInput.GetConnectedSource( &PrimvarReader, &PrimvarReaderId, &AttributeType ) )
		{
			if ( pxr::UsdShadeInput VarnameInput = PrimvarReader.GetInput( UnrealIdentifiers::Varname ) )
			{
				// PrimvarReader may have a string literal with the primvar name, or it may be connected to
				// e.g. an attribute defined elsewhere
				return RecursivelySearchForStringValue( VarnameInput );
			}
		}

		return {};
	}

	void CheckForUVIndexConflicts( TMap<FString, int32>& PrimvarToUVIndex, const FString& ShadeMaterialPath )
	{
		TMap<int32, TArray<FString>> InvertedMap;
		for ( const TPair< FString, int32 >& PrimvarAndUVIndex : PrimvarToUVIndex )
		{
			InvertedMap.FindOrAdd( PrimvarAndUVIndex.Value ).Add( PrimvarAndUVIndex.Key );
		}

		for ( const TPair< int32, TArray<FString> >& UVIndexAndPrimvars : InvertedMap )
		{
			const TArray<FString>& Primvars = UVIndexAndPrimvars.Value;
			if ( Primvars.Num() > 1 )
			{
				FString PrimvarNames = FString::Join(Primvars, TEXT(", "));
				UE_LOG(LogUsd, Warning, TEXT("For shade material '%s', multiple primvars (%s) target the same UV index '%d'"), *ShadeMaterialPath, *PrimvarNames, UVIndexAndPrimvars.Key);
			}
		}
	}

	UMaterialExpression* ParseInputTexture( pxr::UsdShadeInput& ShadeInput, UMaterial& Material, TMap< FString, UObject* >& TexturesCache, TMap<FString, int32>& PrimvarToUVIndex, TextureGroup LODGroup )
	{
		UMaterialExpression* Result = nullptr;

		FScopedUsdAllocs UsdAllocs;

		const bool bIsNormalInput = ( ShadeInput.GetTypeName() == pxr::SdfValueTypeNames->Normal3f || ShadeInput.GetTypeName() == pxr::SdfValueTypeNames->Normal3fArray );

		pxr::UsdShadeConnectableAPI Source;
		pxr::TfToken SourceName;
		pxr::UsdShadeAttributeType AttributeType;

		if ( ShadeInput.GetConnectedSource( &Source, &SourceName, &AttributeType ) )
		{
			pxr::UsdShadeInput FileInput = Source.GetInput( UnrealIdentifiers::File );

			if ( FileInput )
			{
				const FString TexturePath = UsdUtils::GetResolvedTexturePath( FileInput.GetAttr() );
				UTexture* Texture = Cast< UTexture >( TexturesCache.FindRef( TexturePath ) );

				if ( !Texture )
				{
					// Give the same prim path to the texture, so that it ends up imported right next to the material
					FString MaterialPrimPath;
					UUsdAssetImportData* ImportData = Cast<UUsdAssetImportData>(Material.AssetImportData);
					if ( ImportData )
					{
						MaterialPrimPath = ImportData->PrimPath;
					}

					Texture = UsdUtils::CreateTexture( FileInput.GetAttr(), MaterialPrimPath, LODGroup );
				}

				if ( Texture )
				{
					if ( bIsNormalInput )
					{
						Texture->SRGB = false;
						Texture->CompressionSettings = TextureCompressionSettings::TC_Normalmap;
					}

					FString PrimvarName = GetPrimvarUsedAsST(Source);
					int32 UVIndex = 0;
					if ( !PrimvarName.IsEmpty() )
					{
						UVIndex = UsdUtils::GetPrimvarUVIndex(PrimvarName);
						PrimvarToUVIndex.Add(PrimvarName, UVIndex);
					}
					else
					{
						UE_LOG(LogUsd, Warning, TEXT("Failed to find primvar used as st input for texture '%s' in material '%s'. Will use UV index 0 instead"), *TexturePath, *Material.GetName());
					}

					UMaterialExpressionTextureSample* TextureSample = Cast< UMaterialExpressionTextureSample >( UMaterialEditingLibrary::CreateMaterialExpression( &Material, UMaterialExpressionTextureSample::StaticClass() ) );
					TextureSample->Texture = Texture;
					TextureSample->SamplerType = UMaterialExpressionTextureBase::GetSamplerTypeForTexture( Texture );
					TextureSample->ConstCoordinate = UVIndex;

					Result = TextureSample;
					TexturesCache.Add( TexturePath, Texture );
				}
			}
		}

		return Result;
	}

	UMaterialExpression* ParseFloatInput( const pxr::TfToken& InputName, float DefaultValue, UMaterial& Material, pxr::UsdShadeConnectableAPI& Connectable, TMap< FString, UObject* >& TexturesCache, TMap<FString, int32>& PrimvarToUVIndex )
	{
		FScopedUsdAllocs Allocs;

		UMaterialExpression* InputExpression = nullptr;

		pxr::UsdShadeInput Input = Connectable.GetInput( InputName );
		if ( !Input )
		{
			return InputExpression;
		}

		// If we have another shader/node connected
		pxr::UsdShadeConnectableAPI Source;
		pxr::TfToken SourceName;
		pxr::UsdShadeAttributeType AttributeType;
		if ( Input.GetConnectedSource( &Source, &SourceName, &AttributeType ) )
		{
			InputExpression = ParseInputTexture( Input, Material, TexturesCache, PrimvarToUVIndex, TEXTUREGROUP_WorldSpecular );
			if ( !InputExpression )
			{
				// Recurse because the attribute may just be pointing at some other attribute that has the data
				// (e.g. when shader input is just "hoisted" and connected to the parent material input)
				InputExpression = ParseFloatInput( SourceName, DefaultValue, Material, Source, TexturesCache, PrimvarToUVIndex );
			}
		}
		// No other node connected, so we must have some value
		else
		{
			float InputValue = DefaultValue;
			Input.Get< float >( &InputValue );

			if ( !FMath::IsNearlyEqual( InputValue, DefaultValue ) )
			{
				UMaterialExpressionConstant* ExpressionConstant = Cast< UMaterialExpressionConstant >( UMaterialEditingLibrary::CreateMaterialExpression( &Material, UMaterialExpressionConstant::StaticClass() ) );
				ExpressionConstant->R = InputValue;

				InputExpression = ExpressionConstant;
			}
		}

		return InputExpression;
	}

	UMaterialExpression* ParseVec3Input( const pxr::TfToken& InputName, const FLinearColor& DefaultValue, UMaterial& Material, pxr::UsdShadeConnectableAPI& Connectable, TMap< FString, UObject* >& TexturesCache,
		TMap<FString, int32>& PrimvarToUVIndex, bool bIsNormalMap )
	{
		FScopedUsdAllocs Allocs;

		UMaterialExpression* InputExpression = nullptr;

		pxr::UsdShadeInput Input = Connectable.GetInput( InputName );
		if ( !Input )
		{
			return InputExpression;
		}

		// If we have another shader/node connected
		pxr::UsdShadeConnectableAPI Source;
		pxr::TfToken SourceName;
		pxr::UsdShadeAttributeType AttributeType;
		if ( Input.GetConnectedSource( &Source, &SourceName, &AttributeType ) )
		{
			InputExpression = ParseInputTexture( Input, Material, TexturesCache, PrimvarToUVIndex, bIsNormalMap ? TEXTUREGROUP_WorldNormalMap : TEXTUREGROUP_World );
			if ( !InputExpression )
			{
				InputExpression = ParseVec3Input( SourceName, DefaultValue, Material, Source, TexturesCache, PrimvarToUVIndex, bIsNormalMap );
			}
		}
		// No other node connected, so we must have some value
		else if ( InputName != UnrealIdentifiers::Normal )
		{
			FLinearColor DiffuseColor = DefaultValue;
			pxr::GfVec3f UsdDiffuseColor;
			if ( Input.Get< pxr::GfVec3f >( &UsdDiffuseColor ) )
			{
				DiffuseColor = UsdToUnreal::ConvertColor( UsdDiffuseColor );

				// Always create a constant expression, even if the color is default (black). This way we end up with a black material, and not WorldGrid
				UMaterialExpressionConstant4Vector* Constant4VectorExpression = Cast< UMaterialExpressionConstant4Vector >( UMaterialEditingLibrary::CreateMaterialExpression( &Material, UMaterialExpressionConstant4Vector::StaticClass() ) );
				Constant4VectorExpression->Constant = DiffuseColor;

				InputExpression = Constant4VectorExpression;
			}
		}

		return InputExpression;
	}

	// Note that we will bake things that aren't supported on the default USD surface shader schema. These could be useful in case the user has a custom
	// renderer though, and he can pick which properties he wants anyway
	bool BakeMaterial( const UMaterialInterface& Material, const TArray<FPropertyEntry>& InMaterialProperties, const FIntPoint& InDefaultTextureSize, FBakeOutput& OutBakedData )
	{
		const bool bAllQualityLevels = true;
		const bool bAllFeatureLevels = true;
		TArray<UTexture*> MaterialTextures;
		Material.GetUsedTextures( MaterialTextures, EMaterialQualityLevel::Num, bAllQualityLevels, GMaxRHIFeatureLevel, bAllFeatureLevels );

		// Precache all used textures, otherwise could get everything rendered with low-res textures.
		for ( UTexture* Texture : MaterialTextures )
		{
			if ( UTexture2D* Texture2D = Cast<UTexture2D>( Texture ) )
			{
				Texture2D->SetForceMipLevelsToBeResident( 30.0f );
				Texture2D->WaitForStreaming();
			}
		}

		FMaterialData MatSet;
		MatSet.Material = const_cast<UMaterialInterface*>(&Material); // We don't modify it and neither does the material baking module, it's just a bad signature

		for ( const FPropertyEntry& Entry : InMaterialProperties )
		{
			// No point in asking it to bake if we're going to use the user-supplied value
			if ( Entry.bUseConstantValue )
			{
				continue;
			}

			switch ( Entry.Property )
			{
			case MP_Normal:
				if ( !Material.GetMaterial()->HasNormalConnected() && !Material.GetMaterial()->bUseMaterialAttributes )
				{
					continue;
				}
				break;
			case MP_Tangent:
				if ( !Material.GetMaterial()->Tangent.IsConnected() && !Material.GetMaterial()->bUseMaterialAttributes )
				{
					continue;
				}
				break;
			case MP_EmissiveColor:
				if ( !Material.GetMaterial()->EmissiveColor.IsConnected() && !Material.GetMaterial()->bUseMaterialAttributes )
				{
					continue;
				}
				break;
			case MP_OpacityMask:
				if ( !Material.IsPropertyActive( MP_OpacityMask ) || Material.GetBlendMode() != BLEND_Masked )
				{
					continue;
				}
				break;
			case MP_Opacity:
				if ( !Material.IsPropertyActive( MP_Opacity ) || !IsTranslucentBlendMode( Material.GetBlendMode() ) )
				{
					continue;
				}
				break;
			case MP_MAX:
				continue;
				break;
			default:
				if ( !Material.IsPropertyActive( Entry.Property ) )
				{
					continue;
				}
				break;
			}

			MatSet.PropertySizes.Add( Entry.Property, Entry.bUseCustomSize ? Entry.CustomSize : InDefaultTextureSize );
		}

		FMeshData MeshSettings;
		MeshSettings.RawMeshDescription = nullptr;
		MeshSettings.TextureCoordinateBox = FBox2D( FVector2D( 0.0f, 0.0f ), FVector2D( 1.0f, 1.0f ) );
		MeshSettings.TextureCoordinateIndex = 0;

		TArray<FBakeOutput> BakeOutputs;
		IMaterialBakingModule& Module = FModuleManager::Get().LoadModuleChecked<IMaterialBakingModule>( "MaterialBaking" );
		Module.BakeMaterials( { &MatSet }, { &MeshSettings }, BakeOutputs );

		if ( BakeOutputs.Num() < 1 )
		{
			return false;
		}

		OutBakedData = BakeOutputs[0];
		return true;
	}

	// Writes textures for all baked channels in BakedSamples that are larger than 1x1, and returns the filenames of the emitted textures for each channel
	TMap<EMaterialProperty, FString> WriteTextures( FBakeOutput& BakedSamples, const FString& TextureNamePrefix, const FDirectoryPath& TexturesFolder )
	{
		IImageWrapperModule& ImageWrapperModule = FModuleManager::Get().LoadModuleChecked<IImageWrapperModule>( TEXT( "ImageWrapper" ) );
		TSharedPtr<IImageWrapper> PNGImageWrapper = ImageWrapperModule.CreateImageWrapper( EImageFormat::PNG );

		TMap<EMaterialProperty, FString> WrittenTexturesPerChannel;

		// Write textures for baked properties larger than 1x1
		for ( TPair<EMaterialProperty, TArray<FColor>>& BakedDataPair : BakedSamples.PropertyData )
		{
			const EMaterialProperty Property = BakedDataPair.Key;
			TArray<FColor>& Samples = BakedDataPair.Value;
			const FIntPoint& FinalSize = BakedSamples.PropertySizes.FindChecked( Property );

			FString TextureFilePath;
			if ( FinalSize.GetMin() > 1 )
			{
				const UEnum* PropertyEnum = StaticEnum<EMaterialProperty>();
				FName PropertyName = PropertyEnum->GetNameByValue( Property );
				FString TrimmedPropertyName = PropertyName.ToString();
				TrimmedPropertyName.RemoveFromStart( TEXT( "MP_" ) );

				TextureFilePath = FPaths::Combine( TexturesFolder.Path, FString::Printf( TEXT( "T_%s_%s.png" ), *TextureNamePrefix, *TrimmedPropertyName ) );

				// For some reason the baked samples always have zero alpha and there is nothing we can do about it... It seems like the material baking module is made
				// with the intent that the data ends up in UTexture2Ds, where they can be set to be compressed without alpha and have the value ignored.
				// Since we need to write these to file, we must set them back up to full alpha. This is potentially useless as USD handles these at most as color3f, but
				// it could be annoying for the user if he intends on using the textures for anything else
				for ( FColor& Sample : Samples )
				{
					Sample.A = 255;
				}

				PNGImageWrapper->SetRaw( Samples.GetData(), Samples.GetAllocatedSize(), FinalSize.X, FinalSize.Y, ERGBFormat::BGRA, 8 );
				const TArray64<uint8>& PNGData = PNGImageWrapper->GetCompressed( 100 );

				bool bWroteFile = FFileHelper::SaveArrayToFile( PNGData, *TextureFilePath );
				if ( bWroteFile )
				{
					WrittenTexturesPerChannel.Add(Property, TextureFilePath);
				}
				else
				{
					UE_LOG(LogUsd, Warning, TEXT("Failed to write texture '%s', baked channel will be ignored."), *TextureFilePath);
				}
			}
		}

		return WrittenTexturesPerChannel;
	}

	bool ConfigureShadePrim( const FBakeOutput& BakedData, const TMap<EMaterialProperty, FString>& WrittenTextures, const TMap<EMaterialProperty, float>& UserConstantValues, pxr::UsdShadeMaterial& OutUsdShadeMaterial )
	{
		FScopedUsdAllocs Allocs;

		pxr::UsdPrim MaterialPrim = OutUsdShadeMaterial.GetPrim();
		pxr::UsdStageRefPtr Stage = MaterialPrim.GetStage();
		if ( !MaterialPrim || !Stage )
		{
			return false;
		}

		FString UsdFilePath = UsdToUnreal::ConvertString( Stage->GetRootLayer()->GetRealPath() );

		FUsdStageInfo StageInfo{ Stage };

		pxr::SdfPath MaterialPath = MaterialPrim.GetPath();

		// Create surface shader
		pxr::UsdShadeShader ShadeShader = pxr::UsdShadeShader::Define( Stage, MaterialPath.AppendChild( UnrealToUsd::ConvertToken( TEXT( "SurfaceShader" ) ).Get() ) );
		ShadeShader.SetShaderId( UnrealIdentifiers::UsdPreviewSurface );
		pxr::UsdShadeOutput ShaderOutOutput = ShadeShader.CreateOutput( UnrealIdentifiers::Surface, pxr::SdfValueTypeNames->Token );

		// Connect material to surface shader
		pxr::UsdShadeOutput MaterialSurfaceOutput = OutUsdShadeMaterial.CreateSurfaceOutput();
		MaterialSurfaceOutput.ConnectToSource(ShaderOutOutput);

		pxr::UsdShadeShader PrimvarReaderShader;

		const pxr::TfToken PrimvarReaderShaderName = UnrealToUsd::ConvertToken(TEXT("PrimvarReader")).Get();
		const pxr::TfToken PrimvarVariableName = UnrealToUsd::ConvertToken(TEXT("stPrimvarName")).Get();

		// Collect all the properties we'll process, as some data is baked and some comes from values the user input as export options
		TSet<EMaterialProperty> PropertiesToProcess;
		{
			PropertiesToProcess.Reserve( BakedData.PropertyData.Num() + UserConstantValues.Num() );
			BakedData.PropertyData.GetKeys( PropertiesToProcess );

			TSet<EMaterialProperty> PropertiesWithUserConstantData;
			UserConstantValues.GetKeys( PropertiesWithUserConstantData );

			PropertiesToProcess.Append(PropertiesWithUserConstantData);
		}

		bool bHasEmissive = false;

		// Fill in outputs
		for ( const EMaterialProperty& Property : PropertiesToProcess )
		{
			const TArray<FColor>* Samples = BakedData.PropertyData.Find( Property );
			const FIntPoint* SampleSize = BakedData.PropertySizes.Find( Property );
			const float* UserConstantValue = UserConstantValues.Find( Property );
			const FString* TextureFilePath = WrittenTextures.Find( Property );
			const bool bPropertyValueIsConstant = ( UserConstantValue != nullptr || ( Samples && Samples->Num() == 1 ) );

			if ( ( !Samples || !SampleSize ) && !UserConstantValue )
			{
				UE_LOG( LogUsd, Warning, TEXT( "Skipping material property %d as we have no valid data to use." ), Property );
				continue;
			}

			if ( !UserConstantValue &&
				 Samples &&
				 SampleSize &&
				 ( SampleSize->GetMin() < 1 || Samples->Num() != SampleSize->X * SampleSize->Y || Samples->Num() == 0 ) )
			{
				UE_LOG( LogUsd, Warning, TEXT( "Skipping material property %d as we need the baked samples and they're invalid." ), Property );
				continue;
			}

			if ( !bPropertyValueIsConstant && ( TextureFilePath == nullptr || !FPaths::FileExists( *TextureFilePath ) ) )
			{
				UE_LOG( LogUsd, Warning, TEXT( "Skipping material property %d as target texture '%s' was not found." ), Property, TextureFilePath ? **TextureFilePath : TEXT( "" ) );
				continue;
			}

			pxr::TfToken InputToken;
			pxr::SdfValueTypeName InputType;
			pxr::VtValue ConstantValue;
			pxr::GfVec4f FallbackValue;
			switch ( Property )
			{
			case MP_BaseColor:
				InputToken = UnrealIdentifiers::DiffuseColor;
				InputType = pxr::SdfValueTypeNames->Color3f;
				if ( bPropertyValueIsConstant )
				{
					if ( UserConstantValue )
					{
						ConstantValue = pxr::GfVec3f( *UserConstantValue, *UserConstantValue, *UserConstantValue );
					}
					else
					{
						pxr::GfVec4f ConvertedColor = UnrealToUsd::ConvertColor( ( *Samples )[ 0 ] );
						ConstantValue = pxr::GfVec3f( ConvertedColor[ 0 ], ConvertedColor[ 1 ], ConvertedColor[ 2 ] );
					}
				}
				FallbackValue = pxr::GfVec4f{ 0.0, 0.0, 0.0, 1.0f };
				break;
			case MP_Specular:
				InputToken = UnrealIdentifiers::Specular;
				InputType = pxr::SdfValueTypeNames->Float;
				ConstantValue = UserConstantValue ? *UserConstantValue : ( *Samples )[ 0 ].R / 255.0f;
				FallbackValue = pxr::GfVec4f{ 0.5, 0.5, 0.5, 1.0f };
				break;
			case MP_Metallic:
				InputToken = UnrealIdentifiers::Metallic;
				InputType = pxr::SdfValueTypeNames->Float;
				ConstantValue = UserConstantValue ? *UserConstantValue : ( *Samples )[ 0 ].R / 255.0f;
				FallbackValue = pxr::GfVec4f{ 0.0, 0.0, 0.0, 1.0f };
				break;
			case MP_Roughness:
				InputToken = UnrealIdentifiers::Roughness;
				InputType = pxr::SdfValueTypeNames->Float;
				ConstantValue = UserConstantValue ? *UserConstantValue : ( *Samples )[ 0 ].R / 255.0f;
				FallbackValue = pxr::GfVec4f{ 0.5, 0.5, 0.5, 1.0f };
				break;
			case MP_Normal:
				InputToken = UnrealIdentifiers::Normal;
				InputType = pxr::SdfValueTypeNames->Normal3f;
				if ( bPropertyValueIsConstant )
				{
					// This doesn't make much sense here but for it's an available option, so here we go
					if ( UserConstantValue )
					{
						ConstantValue = pxr::GfVec3f( *UserConstantValue, *UserConstantValue, *UserConstantValue );
					}
					else
					{
						FVector ConvertedNormal{ ( *Samples )[ 0 ].ReinterpretAsLinear() };
						ConstantValue = UnrealToUsd::ConvertVector( StageInfo, ConvertedNormal );
					}
				}
				FallbackValue = pxr::GfVec4f{ 0.0, 1.0, 0.0, 1.0f };
				break;
			case MP_Tangent:
				InputToken = UnrealIdentifiers::Tangent;
				InputType = pxr::SdfValueTypeNames->Normal3f;
				if ( bPropertyValueIsConstant )
				{
					// This doesn't make much sense here but for it's an available option, so here we go
					if ( UserConstantValue )
					{
						ConstantValue = pxr::GfVec3f( *UserConstantValue, *UserConstantValue, *UserConstantValue );
					}
					else
					{
						FVector ConvertedNormal{ ( *Samples )[ 0 ].ReinterpretAsLinear() };
						ConstantValue = UnrealToUsd::ConvertVector( StageInfo, ConvertedNormal );
					}
				}
				FallbackValue = pxr::GfVec4f{ 0.0, 1.0, 0.0, 1.0f };
				break;
			case MP_EmissiveColor:
				InputToken = UnrealIdentifiers::EmissiveColor;
				InputType = pxr::SdfValueTypeNames->Color3f;
				if ( bPropertyValueIsConstant )
				{
					// This doesn't make much sense here but for it's an available option, so here we go
					if ( UserConstantValue )
					{
						ConstantValue = pxr::GfVec3f( *UserConstantValue, *UserConstantValue, *UserConstantValue );
					}
					else
					{
						pxr::GfVec4f ConvertedColor = UnrealToUsd::ConvertColor( ( *Samples )[ 0 ] );
						ConstantValue = pxr::GfVec3f( ConvertedColor[ 0 ], ConvertedColor[ 1 ], ConvertedColor[ 2 ] );
					}
				}
				FallbackValue = pxr::GfVec4f{ 0.0, 0.0, 0.0, 1.0f };
				bHasEmissive = true;
				break;
			case MP_Opacity: // It's OK that we use the same for both as these are mutually exclusive blend modes
			case MP_OpacityMask:
				InputToken = UnrealIdentifiers::Opacity;
				InputType = pxr::SdfValueTypeNames->Float;
				ConstantValue = UserConstantValue ? *UserConstantValue : ( *Samples )[ 0 ].R / 255.0f;
				FallbackValue = pxr::GfVec4f{ 1.0, 1.0, 1.0, 1.0f };
				break;
			case MP_Anisotropy:
				InputToken = UnrealIdentifiers::Anisotropy;
				InputType = pxr::SdfValueTypeNames->Float;
				ConstantValue = UserConstantValue ? *UserConstantValue : ( *Samples )[ 0 ].R / 255.0f;
				FallbackValue = pxr::GfVec4f{ 0.0, 0.0, 0.0, 1.0f };
				break;
			case MP_AmbientOcclusion:
				InputToken = UnrealIdentifiers::AmbientOcclusion;
				InputType = pxr::SdfValueTypeNames->Float;
				ConstantValue = UserConstantValue ? *UserConstantValue : ( *Samples )[ 0 ].R / 255.0f;
				FallbackValue = pxr::GfVec4f{ 1.0, 1.0, 1.0, 1.0f };
				break;
			case MP_SubsurfaceColor:
				InputToken = UnrealIdentifiers::SubsurfaceColor;
				InputType = pxr::SdfValueTypeNames->Color3f;
				if ( bPropertyValueIsConstant )
				{
					if ( UserConstantValue )
					{
						ConstantValue = pxr::GfVec3f( *UserConstantValue, *UserConstantValue, *UserConstantValue );
					}
					else
					{
						pxr::GfVec4f ConvertedColor = UnrealToUsd::ConvertColor( ( *Samples )[ 0 ] );
						ConstantValue = pxr::GfVec3f( ConvertedColor[ 0 ], ConvertedColor[ 1 ], ConvertedColor[ 2 ] );
					}
				}
				FallbackValue = pxr::GfVec4f{ 1.0, 1.0, 1.0, 1.0f };
				break;
			default:
				continue;
				break;
			}

			pxr::UsdShadeInput ShadeInput = ShadeShader.CreateInput( InputToken, InputType );
			if ( bPropertyValueIsConstant )
			{
				ShadeInput.Set( ConstantValue );
			}
			else // Its a texture
			{
				// Create the primvar/uv set reader on-demand. We'll be using the same UV set for everything for now
				if ( !PrimvarReaderShader )
				{
					PrimvarReaderShader = pxr::UsdShadeShader::Define( Stage, MaterialPath.AppendChild( PrimvarReaderShaderName ) );
					PrimvarReaderShader.SetShaderId( UnrealIdentifiers::UsdPrimvarReader_float2 );

					// Create the 'st' input directly on the material, as that seems to be preferred
					pxr::UsdShadeInput MaterialStInput = OutUsdShadeMaterial.CreateInput( PrimvarVariableName, pxr::SdfValueTypeNames->Token );
					MaterialStInput.Set( UnrealIdentifiers::St );

					pxr::UsdShadeInput VarnameInput = PrimvarReaderShader.CreateInput( UnrealIdentifiers::Varname, pxr::SdfValueTypeNames->String );
					VarnameInput.ConnectToSource( MaterialStInput );

					PrimvarReaderShader.CreateOutput( UnrealIdentifiers::Result, pxr::SdfValueTypeNames->Token );
				}

				FString TextureReaderName = UsdToUnreal::ConvertToken( InputToken );
				TextureReaderName.RemoveFromEnd( TEXT( "Color" ) );
				TextureReaderName += TEXT( "Texture" );

				pxr::UsdShadeShader UsdUVTextureShader = pxr::UsdShadeShader::Define( Stage, MaterialPath.AppendChild( UnrealToUsd::ConvertToken( *TextureReaderName ).Get() ) );
				UsdUVTextureShader.SetShaderId( UnrealIdentifiers::UsdUVTexture );

				pxr::UsdShadeInput TextureFileInput = UsdUVTextureShader.CreateInput( UnrealIdentifiers::File, pxr::SdfValueTypeNames->Asset );
				FString TextureRelativePath = *TextureFilePath;
				FPaths::MakePathRelativeTo( TextureRelativePath, *UsdFilePath );
				TextureFileInput.Set( pxr::SdfAssetPath( UnrealToUsd::ConvertString( *TextureRelativePath ).Get() ) );

				pxr::UsdShadeInput TextureStInput = UsdUVTextureShader.CreateInput( UnrealIdentifiers::St, pxr::SdfValueTypeNames->Float2 );
				TextureStInput.ConnectToSource( PrimvarReaderShader.GetOutput( UnrealIdentifiers::Result ) );

				pxr::UsdShadeInput TextureFallbackInput = UsdUVTextureShader.CreateInput( UnrealIdentifiers::Fallback, pxr::SdfValueTypeNames->Float4 );
				TextureFallbackInput.Set( FallbackValue );

				pxr::UsdShadeOutput TextureOutput = UsdUVTextureShader.CreateOutput(
					InputType == pxr::SdfValueTypeNames->Float ? UnrealIdentifiers::R : UnrealIdentifiers::RGB,
					InputType
				);

				ShadeInput.ConnectToSource( TextureOutput );
			}
		}

		// The emissive texture is just remapped to [0, 255] on bake, and is intended to be scaled by the emissiveScale before usage
		if ( bHasEmissive )
		{
			pxr::UsdShadeInput EmissiveScaleInput = OutUsdShadeMaterial.CreateInput(
				UnrealToUsd::ConvertToken( TEXT( "emissiveScale" ) ).Get(),
				pxr::SdfValueTypeNames->Float
			);

			EmissiveScaleInput.Set( BakedData.EmissiveScale );
		}

		return true;
	}
}

bool UsdToUnreal::ConvertMaterial( const pxr::UsdShadeMaterial& UsdShadeMaterial, UMaterial& Material )
{
	TMap< FString, UObject* > TexturesCache;
	TMap<FString, int32> PrimvarToUVIndex;
	return ConvertMaterial( UsdShadeMaterial, Material, TexturesCache, PrimvarToUVIndex );
}

bool UsdToUnreal::ConvertMaterial( const pxr::UsdShadeMaterial& UsdShadeMaterial, UMaterial& Material, TMap< FString, UObject* >& TexturesCache, TMap<FString, int32>& PrimvarToUVIndex )
{
	pxr::UsdShadeShader SurfaceShader = UsdShadeMaterial.ComputeSurfaceSource();
	if ( !SurfaceShader )
	{
		return false;
	}
	pxr::UsdShadeConnectableAPI Connectable{ SurfaceShader };

	bool bHasMaterialInfo = false;

	// Base color
	{
		const bool bIsNormalMap = false;
		UMaterialExpression* BaseColorExpression = UsdShadeConversionImpl::ParseVec3Input( UnrealIdentifiers::DiffuseColor, FLinearColor( 0, 0, 0 ), Material, Connectable, TexturesCache, PrimvarToUVIndex, bIsNormalMap );

		if ( BaseColorExpression )
		{
			Material.BaseColor.Expression = BaseColorExpression;
			bHasMaterialInfo = true;
		}
	}

	// Metallic
	{
		UMaterialExpression* MetallicExpression = UsdShadeConversionImpl::ParseFloatInput( UnrealIdentifiers::Metallic, 0.f, Material, Connectable, TexturesCache, PrimvarToUVIndex );

		if ( MetallicExpression )
		{
			Material.Metallic.Expression = MetallicExpression;
			bHasMaterialInfo = true;
		}
	}

	// Roughness
	{
		UMaterialExpression* RoughnessExpression = UsdShadeConversionImpl::ParseFloatInput( UnrealIdentifiers::Roughness, 1.f, Material, Connectable, TexturesCache, PrimvarToUVIndex );

		if ( RoughnessExpression )
		{
			Material.Roughness.Expression = RoughnessExpression;
			bHasMaterialInfo = true;
		}
	}

	// Opacity
	{
		UMaterialExpression* OpacityExpression = UsdShadeConversionImpl::ParseFloatInput( UnrealIdentifiers::Opacity, 1.f, Material, Connectable, TexturesCache, PrimvarToUVIndex );

		if ( OpacityExpression )
		{
			Material.Opacity.Expression = OpacityExpression;
			Material.BlendMode = BLEND_Translucent;
			bHasMaterialInfo = true;
		}
	}

	// Normal
	{
		const bool bIsNormalMap = true;
		UMaterialExpression* NormalExpression = UsdShadeConversionImpl::ParseVec3Input( UnrealIdentifiers::Normal, FLinearColor( 0, 0, 1 ), Material, Connectable, TexturesCache, PrimvarToUVIndex, bIsNormalMap );

		if ( NormalExpression )
		{
			Material.Normal.Expression = NormalExpression;
			bHasMaterialInfo = true;
		}
	}

	FString ShadeMaterialPath = UsdToUnreal::ConvertPath( UsdShadeMaterial.GetPrim().GetPath() );
	UsdShadeConversionImpl::CheckForUVIndexConflicts( PrimvarToUVIndex, ShadeMaterialPath );

	return bHasMaterialInfo;
}


bool UnrealToUsd::ConvertMaterialToBakedSurface( const UMaterialInterface& InMaterial, const TArray<FPropertyEntry>& InMaterialProperties, const FIntPoint& InDefaultTextureSize, const FDirectoryPath& InTexturesDir, pxr::UsdPrim& OutUsdShadeMaterialPrim )
{
	pxr::UsdShadeMaterial OutUsdShadeMaterial{ OutUsdShadeMaterialPrim };
	if ( !OutUsdShadeMaterial )
	{
		return false;
	}

	FBakeOutput BakedData;
	if ( !UsdShadeConversionImpl::BakeMaterial( InMaterial, InMaterialProperties, InDefaultTextureSize, BakedData ) )
	{
		return false;
	}

	TMap<EMaterialProperty, FString> WrittenTextures = UsdShadeConversionImpl::WriteTextures( BakedData, InMaterial.GetName(), InTexturesDir );

	// Manually add user supplied constant values. Can't temporarily place these in BakedData as they're floats, and baked data is just FColors
	TMap<EMaterialProperty, float> UserConstantValues;
	for ( const FPropertyEntry& Entry : InMaterialProperties )
	{
		if ( Entry.bUseConstantValue )
		{
			UserConstantValues.Add( Entry.Property, Entry.ConstantValue );
		}
	}

	return UsdShadeConversionImpl::ConfigureShadePrim( BakedData, WrittenTextures, UserConstantValues, OutUsdShadeMaterial );
}

FString UsdUtils::GetResolvedTexturePath( const pxr::UsdAttribute& TextureAssetPathAttr )
{
	FScopedUsdAllocs UsdAllocs;

	pxr::SdfAssetPath TextureAssetPath;
	TextureAssetPathAttr.Get< pxr::SdfAssetPath >( &TextureAssetPath );

	FString ResolvedTexturePath = UsdToUnreal::ConvertString( TextureAssetPath.GetResolvedPath() );
	FPaths::NormalizeFilename( ResolvedTexturePath );

	if ( ResolvedTexturePath.IsEmpty() )
	{
		FString TexturePath = UsdToUnreal::ConvertString( TextureAssetPath.GetAssetPath() );
		FPaths::NormalizeFilename( TexturePath );

		pxr::SdfLayerRefPtr TextureLayer = UsdUtils::FindLayerForAttribute( TextureAssetPathAttr, pxr::UsdTimeCode::EarliestTime().GetValue() );
		ResolvedTexturePath = UsdShadeConversionImpl::ResolveAssetPath( TextureLayer, TexturePath );
	}

	return ResolvedTexturePath;
}

UTexture* UsdUtils::CreateTexture( const pxr::UsdAttribute& TextureAssetPathAttr, const FString& PrimPath, TextureGroup LODGroup )
{
	FScopedUsdAllocs UsdAllocs;

	pxr::SdfAssetPath TextureAssetPath;
	TextureAssetPathAttr.Get< pxr::SdfAssetPath >( &TextureAssetPath );

	FString TexturePath = UsdToUnreal::ConvertString( TextureAssetPath.GetAssetPath() );
	const bool bIsSupportedUdimTexture = TexturePath.Contains( TEXT("<UDIM>") );
	FPaths::NormalizeFilename( TexturePath );

	FScopedUnrealAllocs UnrealAllocs;

	UTexture* Texture = nullptr;

	if ( !TexturePath.IsEmpty() )
	{
		bool bOutCancelled = false;
		UTextureFactory* TextureFactory = NewObject< UTextureFactory >();
		TextureFactory->SuppressImportOverwriteDialog();
		TextureFactory->bUseHashAsGuid = true;
		TextureFactory->LODGroup = LODGroup;

		if ( bIsSupportedUdimTexture )
		{
			FString BaseFileName = FPaths::GetBaseFilename( TexturePath );

			FString BaseFileNameBeforeUdim;
			FString BaseFileNameAfterUdim;
			BaseFileName.Split( TEXT("<UDIM>"), &BaseFileNameBeforeUdim, &BaseFileNameAfterUdim );

			FString UdimRegexPattern = FString::Printf( TEXT(R"((%s)(\d{4})(%s))"), *BaseFileNameBeforeUdim, *BaseFileNameAfterUdim );
			TextureFactory->UdimRegexPattern = MoveTemp( UdimRegexPattern );
		}

		const FString ResolvedTexturePath = GetResolvedTexturePath( TextureAssetPathAttr );

		if ( !ResolvedTexturePath.IsEmpty() )
		{
			// Try checking if the texture is inside an usdz package first, or else TextureFactory throws an error
			Texture = UsdShadeConversionImpl::ReadTextureFromUsdzArchive(ResolvedTexturePath, TextureFactory);

			if ( !Texture )
			{
				Texture = Cast< UTexture >( TextureFactory->ImportObject( UTexture::StaticClass(), GetTransientPackage(), NAME_None, RF_Transient, ResolvedTexturePath, TEXT(""), bOutCancelled ) );
			}

			if ( Texture )
			{
				UUsdAssetImportData* ImportData = NewObject< UUsdAssetImportData >( Texture, TEXT("USDAssetImportData") );
				ImportData->PrimPath = PrimPath;
				ImportData->UpdateFilenameOnly( TexturePath );
				Texture->AssetImportData = ImportData;
			}
		}
	}

	return Texture;
}

namespace UsdShadeConversionImpl
{
	void HashShadeInput( const pxr::UsdShadeInput& ShadeInput, FSHA1& InOutHashState )
	{
		if ( !ShadeInput )
		{
			return;
		}

		FScopedUsdAllocs UsdAllocs;

		FString InputName = UsdToUnreal::ConvertToken( ShadeInput.GetBaseName() );
		InOutHashState.UpdateWithString( *InputName, InputName.Len() );

		FString InputTypeName = UsdToUnreal::ConvertToken(ShadeInput.GetTypeName().GetAsToken());
		InOutHashState.UpdateWithString( *InputTypeName, InputTypeName.Len() );

		// Connected to something else, recurse
		pxr::UsdShadeConnectableAPI Source;
		pxr::TfToken SourceName;
		pxr::UsdShadeAttributeType AttributeType;
		if ( ShadeInput.GetConnectedSource( &Source, &SourceName, &AttributeType ) )
		{
			FString SourceOutputName = UsdToUnreal::ConvertToken(SourceName);
			InOutHashState.UpdateWithString( *SourceOutputName, SourceOutputName.Len() );

			for ( const pxr::UsdShadeInput& ChildInput : Source.GetInputs() )
			{
				HashShadeInput( ChildInput, InOutHashState );
			}
		}
		// Not connected to anything, has a value (this could be a texture file path too)
		else
		{
			pxr::VtValue ShadeInputValue;
			ShadeInput.Get( &ShadeInputValue );
			uint64 ValueHash = ( uint64 )ShadeInputValue.GetHash();
			InOutHashState.Update(reinterpret_cast<uint8*>(&ValueHash), sizeof(uint64));
		}
	}
}

FSHAHash UsdUtils::HashShadeMaterial( const pxr::UsdShadeMaterial& UsdShadeMaterial )
{
	FScopedUsdAllocs UsdAllocs;

	pxr::UsdShadeShader SurfaceShader = UsdShadeMaterial.ComputeSurfaceSource();
	if ( !SurfaceShader )
	{
		return {};
	}

	FSHA1 HashState;

	for ( const pxr::UsdShadeInput& ShadeInput : SurfaceShader.GetInputs() )
	{
		UsdShadeConversionImpl::HashShadeInput( ShadeInput, HashState );
	}

	FSHAHash OutHash;

	HashState.Final();
	HashState.GetHash( &OutHash.Hash[0] );

	return OutHash;
}

#endif // #if USE_USD_SDK
