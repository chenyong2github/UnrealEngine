// Copyright Epic Games, Inc. All Rights Reserved.

#include "USDShadeConversion.h"

#if USE_USD_SDK

#include "USDAssetImportData.h"
#include "USDMemory.h"
#include "USDTypesConversion.h"

#include "Engine/Texture2D.h"
#include "Factories/TextureFactory.h"
#include "MaterialEditingLibrary.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionConstant4Vector.h"
#include "Materials/MaterialInstanceConstant.h"

#include "USDIncludesStart.h"

#include "pxr/usd/ar/resolver.h"
#include "pxr/usd/ar/resolverScopedCache.h"
#include "pxr/usd/sdf/layer.h"
#include "pxr/usd/sdf/layerUtils.h"
#include "pxr/usd/usdShade/material.h"

#include "USDIncludesEnd.h"

namespace UsdShadeConversionImpl
{
	// Finds the strongest layer contributing to an attribute
	pxr::SdfLayerHandle FindLayerHandle( const pxr::UsdAttribute& Attr, const pxr::UsdTimeCode& Time )
	{
		FScopedUsdAllocs UsdAllocs;

		for ( const pxr::SdfPropertySpecHandle& PropertySpec : Attr.GetPropertyStack( Time ) )
		{
			if ( PropertySpec->HasDefaultValue() || PropertySpec->GetLayer()->GetNumTimeSamplesForPath( PropertySpec->GetPath() ) > 0 )
			{
				return PropertySpec->GetLayer();
			}
		}

		return {};
	}

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

	UMaterialExpression* ParseInputTexture( pxr::UsdShadeInput& ShadeInput, UMaterial& Material, TMap< FString, UObject* >& TexturesCache )
	{
		UMaterialExpression* Result = nullptr;

		FScopedUsdAllocs UsdAllocs;

		const bool bIsNormalInput = ( ShadeInput.GetTypeName() == pxr::SdfValueTypeNames->Normal3f || ShadeInput.GetTypeName() == pxr::SdfValueTypeNames->Normal3fArray );

		pxr::UsdShadeConnectableAPI Source;
		pxr::TfToken SourceName;
		pxr::UsdShadeAttributeType AttributeType;

		if ( ShadeInput.GetConnectedSource( &Source, &SourceName, &AttributeType ) )
		{
			pxr::UsdShadeInput FileInput = Source.GetInput( pxr::TfToken("file") );

			if ( FileInput )
			{
				const FString TexturePath = UsdUtils::GetResolvedTexturePath( FileInput.GetAttr() );
				UTexture* Texture = Cast< UTexture >( TexturesCache.FindRef( TexturePath ) );

				if ( !Texture )
				{
					Texture = UsdUtils::CreateTexture( FileInput.GetAttr() );

					if ( bIsNormalInput )
					{
						Texture->SRGB = false;
						Texture->CompressionSettings = TextureCompressionSettings::TC_Normalmap;
					}
				}

				if ( Texture )
				{
					UMaterialExpressionTextureSample* TextureSample = Cast< UMaterialExpressionTextureSample >( UMaterialEditingLibrary::CreateMaterialExpression( &Material, UMaterialExpressionTextureSample::StaticClass() ) );
					TextureSample->Texture = Texture;
					TextureSample->SamplerType = UMaterialExpressionTextureBase::GetSamplerTypeForTexture( Texture );

					Result = TextureSample;
					TexturesCache.Add( TexturePath, Texture );
				}
			}
		}

		return Result;
	}
}

bool UsdToUnreal::ConvertMaterial( const pxr::UsdShadeMaterial& UsdShadeMaterial, UMaterial& Material )
{
	TMap< FString, UObject* > TexturesCache;
	return ConvertMaterial( UsdShadeMaterial, Material, TexturesCache );
}

bool UsdToUnreal::ConvertMaterial( const pxr::UsdShadeMaterial& UsdShadeMaterial, UMaterial& Material, TMap< FString, UObject* >& TexturesCache )
{
	pxr::UsdShadeShader SurfaceShader = UsdShadeMaterial.ComputeSurfaceSource();

	if ( !SurfaceShader )
	{
		return false;
	}

	bool bHasMaterialInfo = false;

	auto ParseFloatInput = [ &Material, &SurfaceShader, &TexturesCache ]( const ANSICHAR* InputName, float DefaultValue ) -> UMaterialExpression*
	{
		pxr::UsdShadeInput Input = SurfaceShader.GetInput( pxr::TfToken(InputName) );
		UMaterialExpression* InputExpression = nullptr;

		if ( Input )
		{
			if ( !Input.HasConnectedSource() )
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
			else
			{
				InputExpression = UsdShadeConversionImpl::ParseInputTexture( Input, Material, TexturesCache );
			}
		}

		return InputExpression;
	};

	// Base color
	{
		pxr::UsdShadeInput DiffuseInput = SurfaceShader.GetInput( pxr::TfToken("diffuseColor") );

		if ( DiffuseInput )
		{
			UMaterialExpression* BaseColorExpression = nullptr;

			if ( !DiffuseInput.HasConnectedSource() )
			{
				pxr::GfVec3f UsdDiffuseColor;
				DiffuseInput.Get< pxr::GfVec3f >( &UsdDiffuseColor );

				FLinearColor DiffuseColor = UsdToUnreal::ConvertColor( UsdDiffuseColor );

				UMaterialExpressionConstant4Vector* Constant4VectorExpression = Cast< UMaterialExpressionConstant4Vector >( UMaterialEditingLibrary::CreateMaterialExpression( &Material, UMaterialExpressionConstant4Vector::StaticClass() ) );
				Constant4VectorExpression->Constant = DiffuseColor;

				BaseColorExpression = Constant4VectorExpression;
			}
			else
			{
				BaseColorExpression = UsdShadeConversionImpl::ParseInputTexture( DiffuseInput, Material, TexturesCache );
			}

			if ( BaseColorExpression )
			{
				Material.BaseColor.Expression = BaseColorExpression;
				bHasMaterialInfo = true;
			}
		}
	}

	// Metallic
	{
		UMaterialExpression* MetallicExpression = ParseFloatInput( "metallic", 0.f );

		if ( MetallicExpression )
		{
			Material.Metallic.Expression = MetallicExpression;
			bHasMaterialInfo = true;
		}
	}

	// Roughness
	{
		UMaterialExpression* RoughnessExpression = ParseFloatInput( "roughness", 1.f );

		if ( RoughnessExpression )
		{
			Material.Roughness.Expression = RoughnessExpression;
			bHasMaterialInfo = true;
		}
	}

	// Opacity
	{
		UMaterialExpression* OpacityExpression = ParseFloatInput( "opacity", 1.f );

		if ( OpacityExpression )
		{
			Material.Opacity.Expression = OpacityExpression;
			Material.BlendMode = BLEND_Translucent;
			bHasMaterialInfo = true;
		}
	}

	// Normal
	{
		pxr::UsdShadeInput NormalInput = SurfaceShader.GetInput( pxr::TfToken("normal") );

		if ( NormalInput )
		{
			UMaterialExpression* NormalExpression = nullptr;

			if ( NormalInput.HasConnectedSource() )			
			{
				NormalExpression = UsdShadeConversionImpl::ParseInputTexture( NormalInput, Material, TexturesCache );
			}

			if ( NormalExpression )
			{
				Material.Normal.Expression = NormalExpression;
				bHasMaterialInfo = true;
			}
		}
	}

	return bHasMaterialInfo;
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

		pxr::SdfLayerHandle LayerHandle = UsdShadeConversionImpl::FindLayerHandle( TextureAssetPathAttr, pxr::UsdTimeCode::EarliestTime() );
		ResolvedTexturePath = UsdShadeConversionImpl::ResolveAssetPath( LayerHandle, TexturePath );
	}

	return ResolvedTexturePath;
}

UTexture* UsdUtils::CreateTexture( const pxr::UsdAttribute& TextureAssetPathAttr )
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
			Texture = Cast< UTexture >( TextureFactory->ImportObject( UTexture::StaticClass(), GetTransientPackage(), NAME_None, RF_Transient, ResolvedTexturePath, TEXT(""), bOutCancelled ) );

			if ( Texture )
			{
				UUsdAssetImportData* ImportData = NewObject< UUsdAssetImportData >( Texture, TEXT("USDAssetImportData") );
				ImportData->UpdateFilenameOnly( TexturePath );
				Texture->AssetImportData = ImportData;
			}
		}
	}

	return Texture;
}

namespace UsdShadeConversionImpl
{
	uint64 HashShadeInput( const pxr::UsdShadeInput& ShadeInput )
	{
		FScopedUsdAllocs UsdAllocs;

		uint64 InputHash = 0;

		if ( ShadeInput )
		{
			if ( !ShadeInput.HasConnectedSource() )
			{
				pxr::VtValue ShadeInputValue;
				ShadeInput.Get( &ShadeInputValue );
				InputHash = (uint64)ShadeInputValue.GetHash();
			}
			else
			{
				pxr::UsdShadeConnectableAPI Source;
				pxr::TfToken SourceName;
				pxr::UsdShadeAttributeType AttributeType;

				if ( ShadeInput.GetConnectedSource( &Source, &SourceName, &AttributeType ) )
				{
					pxr::UsdShadeInput FileInput = Source.GetInput( pxr::TfToken("file") );

					if ( FileInput )
					{
						pxr::SdfAssetPath TextureAssetPath;
						FileInput.Get< pxr::SdfAssetPath >( &TextureAssetPath );

						InputHash = (uint64)TextureAssetPath.GetHash();
					}
				}
			}
		}

		return InputHash;
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
		uint64 HashValue = UsdShadeConversionImpl::HashShadeInput( ShadeInput );
		HashState.Update( (uint8*)&HashValue, sizeof( uint64 ) );
	}

	FSHAHash OutHash;

	HashState.Final();
	HashState.GetHash( &OutHash.Hash[0] );

	return OutHash;
}

#endif // #if USE_USD_SDK
