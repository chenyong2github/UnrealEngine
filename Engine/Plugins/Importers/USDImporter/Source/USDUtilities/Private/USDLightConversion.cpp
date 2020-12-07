// Copyright Epic Games, Inc. All Rights Reserved.

#include "USDLightConversion.h"

#include "USDConversionUtils.h"
#include "USDShadeConversion.h"
#include "USDTypesConversion.h"

#include "Components/DirectionalLightComponent.h"
#include "Components/LightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/RectLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "EditorFramework/AssetImportData.h"
#include "Engine/TextureCube.h"

#if USE_USD_SDK

#include "USDIncludesStart.h"
	#include "pxr/usd/usdLux/diskLight.h"
	#include "pxr/usd/usdLux/distantLight.h"
	#include "pxr/usd/usdLux/domeLight.h"
	#include "pxr/usd/usdLux/rectLight.h"
	#include "pxr/usd/usdLux/sphereLight.h"
#include "USDIncludesEnd.h"

namespace LightConversionImpl
{
	/**
	 * Calculates the solid angle in steradian that corresponds to the sphere surface area of the base of the cone
	 * with the apex at the center of a unit sphere, and angular diameter `SourceAngleDeg`.
	 * E.g. Sun in the sky has ~0.53 degree angular diameter -> 6.720407093551621e-05 sr
	 * Source: https://en.wikipedia.org/wiki/Solid_angle#Cone,_spherical_cap,_hemisphere
	 */
	float SourceAngleToSteradian( float SourceAngleDeg )
	{
		return 2.0f * PI * ( 1.0f - FMath::Cos( FMath::DegreesToRadians( SourceAngleDeg / 2.0f ) ) );
	}
}

bool UsdToUnreal::ConvertLight( const pxr::UsdLuxLight& Light, ULightComponentBase& LightComponentBase, double TimeCode )
{
	if ( !Light )
	{
		return false;
	}

	// Calculate the light intensity: this is equivalent to UsdLuxLight::ComputeBaseEmission() without the color term
	float LightIntensity = 1.f;
	LightIntensity *= UsdUtils::GetUsdValue< float >( Light.GetIntensityAttr(), TimeCode );
	LightIntensity *= FMath::Exp2( UsdUtils::GetUsdValue< float >( Light.GetExposureAttr(), TimeCode ) );
	LightComponentBase.Intensity = LightIntensity;

	if ( ULightComponent* LightComponent = Cast< ULightComponent >( &LightComponentBase ) )
	{
		LightComponent->bUseTemperature = UsdUtils::GetUsdValue< bool >( Light.GetEnableColorTemperatureAttr(), TimeCode );
		LightComponent->Temperature = UsdUtils::GetUsdValue< float >( Light.GetColorTemperatureAttr(), TimeCode );
	}

	const bool bSRGB = true;
	LightComponentBase.LightColor = UsdToUnreal::ConvertColor( UsdUtils::GetUsdValue< pxr::GfVec3f >( Light.GetColorAttr(), TimeCode ) ).ToFColor( bSRGB );

	return true;
}

bool UsdToUnreal::ConvertDistantLight( const pxr::UsdLuxDistantLight& DistantLight, UDirectionalLightComponent& LightComponent, double TimeCode )
{
	if ( !DistantLight )
	{
		return false;
	}

	LightComponent.LightSourceAngle = UsdUtils::GetUsdValue< float >( DistantLight.GetAngleAttr(), TimeCode );
	LightComponent.Intensity *= LightConversionImpl::SourceAngleToSteradian( LightComponent.LightSourceAngle ); // Lux = Nits * Steradian

	return true;
}

bool UsdToUnreal::ConvertRectLight( const FUsdStageInfo& StageInfo, const pxr::UsdLuxRectLight& RectLight, URectLightComponent& LightComponent, double TimeCode )
{
	if ( !RectLight )
	{
		return false;
	}

	LightComponent.SourceWidth = UsdToUnreal::ConvertDistance( StageInfo, UsdUtils::GetUsdValue< float >( RectLight.GetWidthAttr(), TimeCode ) );
	LightComponent.SourceHeight = UsdToUnreal::ConvertDistance( StageInfo, UsdUtils::GetUsdValue< float >( RectLight.GetHeightAttr(), TimeCode ) );

	const float AreaInSqMeters = ( LightComponent.SourceWidth / 100.f ) * ( LightComponent.SourceHeight / 100.f );
	LightComponent.Intensity *= 2.0f * PI * AreaInSqMeters; // Lumen = Nits * (2PI sr for area light) * Area
	LightComponent.IntensityUnits = ELightUnits::Lumens;

	return true;
}

bool UsdToUnreal::ConvertDiskLight( const FUsdStageInfo& StageInfo, const pxr::UsdLuxDiskLight& DiskLight, URectLightComponent& LightComponent, double TimeCode )
{
	if ( !DiskLight )
	{
		return false;
	}

	const float Radius = UsdToUnreal::ConvertDistance( StageInfo, UsdUtils::GetUsdValue< float >( DiskLight.GetRadiusAttr(), TimeCode ) );

	LightComponent.SourceWidth = Radius * 2.f;
	LightComponent.SourceHeight = Radius * 2.f;

	const float AreaInSqMeters = PI * FMath::Square( Radius / 100.f );
	LightComponent.Intensity *= 2.0f * PI * AreaInSqMeters; // Lumen = Nits * (2PI sr for area light) * Area
	LightComponent.IntensityUnits = ELightUnits::Lumens;

	return true;
}

bool UsdToUnreal::ConvertSphereLight( const FUsdStageInfo& StageInfo, const pxr::UsdLuxSphereLight& SphereLight, UPointLightComponent& LightComponent, double TimeCode )
{
	if ( !SphereLight )
	{
		return false;
	}

	const float Radius = UsdToUnreal::ConvertDistance( StageInfo, UsdUtils::GetUsdValue< float >( SphereLight.GetRadiusAttr(), TimeCode ) );
	LightComponent.SourceRadius = Radius;

	const float AreaInSqMeters = 4.f * PI * FMath::Square( Radius / 100.f );
	LightComponent.Intensity *= 4.f * PI * AreaInSqMeters; // Lumen = Nits * (4PI sr for point light) * Area
	LightComponent.IntensityUnits = ELightUnits::Lumens;

	return true;
}

bool UsdToUnreal::ConvertDomeLight( const FUsdStageInfo& StageInfo, const pxr::UsdLuxDomeLight& DomeLight, USkyLightComponent& LightComponent, TMap< FString, UObject* >& TexturesCache, double TimeCode )
{
	if ( !DomeLight )
	{
		return false;
	}

	const FString ResolvedDomeTexturePath = UsdUtils::GetResolvedTexturePath( DomeLight.GetTextureFileAttr() );

	if ( ResolvedDomeTexturePath.IsEmpty() )
	{
		return true;
	}

	UTextureCube* Cubemap = Cast< UTextureCube >( TexturesCache.FindRef( ResolvedDomeTexturePath ) );

	if ( !Cubemap )
	{
		Cubemap = Cast< UTextureCube >( UsdUtils::CreateTexture( DomeLight.GetTextureFileAttr(), UsdToUnreal::ConvertPath(DomeLight.GetPrim().GetPath()) ) );
		TexturesCache.Add( ResolvedDomeTexturePath ) = Cubemap;
	}

	if ( Cubemap )
	{
		LightComponent.Cubemap = Cubemap;
		LightComponent.SourceType = ESkyLightSourceType::SLS_SpecifiedCubemap;
	}

	return true;
}

bool UnrealToUsd::ConvertLightComponent( const ULightComponentBase& LightComponent, pxr::UsdPrim& Prim, double TimeCode )
{
	if ( !Prim )
	{
		return false;
	}

	pxr::UsdLuxLight Light{ Prim };
	if ( !Light )
	{
		return false;
	}

	FScopedUsdAllocs UsdAllocs;

	if ( pxr::UsdAttribute Attr = Light.CreateIntensityAttr() )
	{
		Attr.Set<float>( LightComponent.Intensity, TimeCode );
	}

	// When converting into UE we multiply intensity and exposure together, so when writing back we just
	// put everything in intensity. USD also multiplies those two together, meaning it should end up the same
	if ( pxr::UsdAttribute Attr = Light.CreateExposureAttr() )
	{
		Attr.Set<float>( 0.0f, TimeCode );
	}

	if ( const ULightComponent* DerivedLightComponent = Cast<ULightComponent>( &LightComponent ) )
	{
		if ( pxr::UsdAttribute Attr = Light.CreateEnableColorTemperatureAttr() )
		{
			Attr.Set<bool>( DerivedLightComponent->bUseTemperature, TimeCode );
		}

		if ( pxr::UsdAttribute Attr = Light.CreateColorTemperatureAttr() )
		{
			Attr.Set<float>( DerivedLightComponent->Temperature, TimeCode );
		}
	}

	if ( pxr::UsdAttribute Attr = Light.CreateColorAttr() )
	{
		pxr::GfVec4f LinearColor = UnrealToUsd::ConvertColor( FLinearColor( LightComponent.LightColor ) );
		Attr.Set<pxr::GfVec3f>( pxr::GfVec3f( LinearColor[0], LinearColor[1], LinearColor[2] ), TimeCode );
	}

	return true;
}

bool UnrealToUsd::ConvertDirectionalLightComponent( const UDirectionalLightComponent& LightComponent, pxr::UsdPrim& Prim, double TimeCode )
{
	if ( !Prim )
	{
		return false;
	}

	pxr::UsdLuxDistantLight Light{ Prim };
	if ( !Light )
	{
		return false;
	}

	FScopedUsdAllocs UsdAllocs;

	if ( pxr::UsdAttribute Attr = Light.CreateAngleAttr() )
	{
		Attr.Set<float>( LightComponent.LightSourceAngle, TimeCode );
	}

	if ( pxr::UsdAttribute Attr = Light.CreateIntensityAttr() )
	{
		const float IntensityLux = UsdUtils::GetUsdValue< float >( Attr, TimeCode );
		Attr.Set<float>( IntensityLux / LightConversionImpl::SourceAngleToSteradian( LightComponent.LightSourceAngle ), TimeCode ); // Nits = Lux / Steradian
	}

	return true;
}

bool UnrealToUsd::ConvertRectLightComponent( const URectLightComponent& LightComponent, pxr::UsdPrim& Prim, double TimeCode )
{
	if ( !Prim )
	{
		return false;
	}

	FScopedUsdAllocs UsdAllocs;

	pxr::UsdLuxLight BaseLight{ Prim };
	if ( !BaseLight )
	{
		return false;
	}

	FUsdStageInfo StageInfo( Prim.GetStage() );

	// Disk light
	float AreaInSqMeters = 1.0f;
	if ( pxr::UsdLuxDiskLight DiskLight{ Prim } )
	{
		// Averaging and converting "diameter" to "radius"
		const float Radius = ( LightComponent.SourceWidth + LightComponent.SourceHeight ) / 2.0f / 2.0f;
		AreaInSqMeters = PI * FMath::Square( Radius / 100.0f );

		if ( pxr::UsdAttribute Attr = DiskLight.CreateRadiusAttr() )
		{
			Attr.Set<float>( UnrealToUsd::ConvertDistance( StageInfo, Radius ), TimeCode );
		}
	}
	// Rect light
	else if ( pxr::UsdLuxRectLight RectLight{ Prim } )
	{
		AreaInSqMeters = ( LightComponent.SourceWidth / 100.0f ) * ( LightComponent.SourceHeight / 100.0f );

		if ( pxr::UsdAttribute Attr = RectLight.CreateWidthAttr() )
		{
			Attr.Set<float>( UnrealToUsd::ConvertDistance( StageInfo, LightComponent.SourceWidth ), TimeCode );
		}

		if ( pxr::UsdAttribute Attr = RectLight.CreateHeightAttr() )
		{
			Attr.Set<float>( UnrealToUsd::ConvertDistance( StageInfo, LightComponent.SourceHeight ), TimeCode );
		}
	}
	else
	{
		return false;
	}

	// Common for both
	if ( pxr::UsdAttribute Attr = BaseLight.CreateIntensityAttr() )
	{
		float FinalIntensityNits = 1.0f;
		const float OldIntensity = UsdUtils::GetUsdValue< float >( Attr, TimeCode );

		switch ( LightComponent.IntensityUnits )
		{
		case ELightUnits::Candelas:
			// Nit = candela / area
			FinalIntensityNits = OldIntensity / AreaInSqMeters;
			break;
		case ELightUnits::Lumens:
			// Nit = lumen / (sr * area); For area lights sr = 2PI
			FinalIntensityNits = OldIntensity / ( 2.0f * PI * AreaInSqMeters );
			break;
		case ELightUnits::Unitless:
			// Nit = 625 unitless / area; https://docs.unrealengine.com/en-US/Engine/Rendering/LightingAndShadows/PhysicalLightUnits/index.html#point,spot,andrectlights
			FinalIntensityNits = ( OldIntensity * 625.0f ) / AreaInSqMeters;
			break;
		default:
			break;
		}

		Attr.Set<float>( FinalIntensityNits, TimeCode );
	}

	return true;
}

bool UnrealToUsd::ConvertPointLightComponent( const UPointLightComponent& LightComponent, pxr::UsdPrim& Prim, double TimeCode )
{
	if ( !Prim )
	{
		return false;
	}

	pxr::UsdLuxSphereLight Light{ Prim };
	if ( !Light )
	{
		return false;
	}

	FScopedUsdAllocs UsdAllocs;

	FUsdStageInfo StageInfo( Prim.GetStage() );

	if ( pxr::UsdAttribute Attr = Light.CreateRadiusAttr() )
	{
		Attr.Set<float>( UnrealToUsd::ConvertDistance( StageInfo, LightComponent.SourceRadius ), TimeCode );
	}

	const float AreaInSqMeters = 4.f * PI * FMath::Square( LightComponent.SourceRadius / 100.f );
	if ( pxr::UsdAttribute Attr = Light.CreateIntensityAttr() )
	{
		float FinalIntensityNits = 1.0f;
		const float OldIntensity = UsdUtils::GetUsdValue< float >( Attr, TimeCode );

		switch ( LightComponent.IntensityUnits )
		{
		case ELightUnits::Candelas:
			// Nit = candela / area
			FinalIntensityNits = OldIntensity / AreaInSqMeters;
			break;
		case ELightUnits::Lumens:
			// Nit = lumen / (sr * area); For a full sphere sr = 4PI
			FinalIntensityNits = OldIntensity / ( 4.f * PI * AreaInSqMeters );
			break;
		case ELightUnits::Unitless:
			// Nit = 625 unitless / area; https://docs.unrealengine.com/en-US/Engine/Rendering/LightingAndShadows/PhysicalLightUnits/index.html#point,spot,andrectlights
			FinalIntensityNits = ( OldIntensity * 625.0f ) / AreaInSqMeters;
			break;
		default:
			break;
		}

		Attr.Set<float>( FinalIntensityNits, TimeCode );
	}

	return true;
}

bool UnrealToUsd::ConvertSkyLightComponent( const USkyLightComponent& LightComponent, pxr::UsdPrim& Prim, double TimeCode )
{
	if ( !Prim )
	{
		return false;
	}

	pxr::UsdLuxDomeLight Light{ Prim };
	if ( !Light )
	{
		return false;
	}

	FScopedUsdAllocs UsdAllocs;

	FUsdStageInfo StageInfo( Prim.GetStage() );

	if ( pxr::UsdAttribute Attr = Light.CreateTextureFileAttr() )
	{
		if ( UTextureCube* TextureCube = LightComponent.Cubemap )
		{
			if ( UAssetImportData* AssetImportData = TextureCube->AssetImportData )
			{
				FString FilePath = AssetImportData->GetFirstFilename();

				Attr.Set<pxr::SdfAssetPath>( pxr::SdfAssetPath{ UnrealToUsd::ConvertString( *FilePath ).Get() }, TimeCode );
			}
		}
	}

	return true;
}

#endif // #if USE_USD_SDK