// Copyright Epic Games, Inc. All Rights Reserved.

#include "USDLightConversion.h"

#include "USDAssetCache.h"
#include "USDConversionUtils.h"
#include "USDLayerUtils.h"
#include "USDLog.h"
#include "USDShadeConversion.h"
#include "USDTypesConversion.h"

#include "UsdWrappers/SdfLayer.h"

#include "Components/DirectionalLightComponent.h"
#include "Components/LightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/RectLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "EditorFramework/AssetImportData.h"
#include "Engine/TextureCube.h"

#if USE_USD_SDK

#include "USDIncludesStart.h"
	#include "pxr/usd/usdLux/diskLight.h"
	#include "pxr/usd/usdLux/distantLight.h"
	#include "pxr/usd/usdLux/domeLight.h"
	#include "pxr/usd/usdLux/rectLight.h"
	#include "pxr/usd/usdLux/shapingAPI.h"
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

	// Only use PI instead of 2PI because URectLightComponent::SetLightBrightness will use just PI and not 2PI for lumen conversions, due to a cosine distribution
	// c.f. UActorFactoryRectLight::PostSpawnActor, and the PI factor between candela and lumen for rect lights on https://docs.unrealengine.com/en-US/BuildingWorlds/LightingAndShadows/PhysicalLightUnits/index.html#point,spot,andrectlights
	LightComponent.Intensity *= PI * AreaInSqMeters; // Lumen = Nits * (PI sr for area light) * Area
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

	// Only use PI instead of 2PI because URectLightComponent::SetLightBrightness will use just PI and not 2PI for lumen conversions, due to a cosine distribution
	// c.f. UActorFactoryRectLight::PostSpawnActor, and the PI factor between candela and lumen for rect lights on https://docs.unrealengine.com/en-US/BuildingWorlds/LightingAndShadows/PhysicalLightUnits/index.html#point,spot,andrectlights
	const float AreaInSqMeters = PI * FMath::Square( Radius / 100.f );
	LightComponent.Intensity *= PI * AreaInSqMeters; // Lumen = Nits * (PI sr for area light) * Area
	LightComponent.IntensityUnits = ELightUnits::Lumens;

	return true;
}

bool UsdToUnreal::ConvertSphereLight( const FUsdStageInfo& StageInfo, const pxr::UsdLuxSphereLight& SphereLight, UPointLightComponent& LightComponent, double TimeCode )
{
	if ( !SphereLight )
	{
		return false;
	}

	float Radius = UsdToUnreal::ConvertDistance( StageInfo, UsdUtils::GetUsdValue< float >( SphereLight.GetRadiusAttr(), TimeCode ) );

	float SolidAngle = 4.f * PI;
	if ( USpotLightComponent* SpotLightComponent = Cast< USpotLightComponent >( &LightComponent ) )
	{
		// c.f. USpotLightComponent::ComputeLightBrightness
		SolidAngle = 2.f * PI * ( 1.0f - SpotLightComponent->GetCosHalfConeAngle() );
	}

	// Using solid angle for this area is possibly incorrect, but using Nits for point lights also doesn't make much sense in the first place either,
	// but we must do it for consistency with USD
	const float AreaInSqMeters = FMath::Max( SolidAngle * FMath::Square( Radius / 100.f ), KINDA_SMALL_NUMBER );

	if ( UsdUtils::GetUsdValue< bool >( SphereLight.GetTreatAsPointAttr(), TimeCode ) == true )
	{
		Radius = 0.f;
	}

	LightComponent.Intensity *= SolidAngle * AreaInSqMeters; // Lumen = Nits * SolidAngle * Area
	LightComponent.IntensityUnits = ELightUnits::Lumens;
	LightComponent.SourceRadius = Radius;

	return true;
}

bool UsdToUnreal::ConvertDomeLight( const FUsdStageInfo& StageInfo, const pxr::UsdLuxDomeLight& DomeLight, USkyLightComponent& LightComponent, UUsdAssetCache* TexturesCache, double TimeCode )
{
	if ( !DomeLight )
	{
		return false;
	}

	const FString ResolvedDomeTexturePath = UsdUtils::GetResolvedTexturePath( DomeLight.GetTextureFileAttr() );
	if ( ResolvedDomeTexturePath.IsEmpty() )
	{
		FScopedUsdAllocs Allocs;

		pxr::SdfAssetPath TextureAssetPath;
		DomeLight.GetTextureFileAttr().Get< pxr::SdfAssetPath >( &TextureAssetPath );

		// Show a good warning for this because it's easy to pick some cubemap asset from the engine (that usually don't come with the
		// source texture) and have the dome light silently not work again
		FString TargetAssetPath = UsdToUnreal::ConvertString( TextureAssetPath.GetAssetPath() );
		UE_LOG( LogUsd, Warning, TEXT( "Failed to find texture '%s' used for UsdLuxDomeLight '%s'!" ), *TargetAssetPath, *UsdToUnreal::ConvertPath( DomeLight.GetPrim().GetPath() ) );

		return true;
	}

	const FString DomeTextureHash = LexToString( FMD5Hash::HashFile( *ResolvedDomeTexturePath ) );
	UTextureCube* Cubemap = Cast< UTextureCube >( TexturesCache ? TexturesCache->GetCachedAsset( DomeTextureHash ) : nullptr );

	if ( !Cubemap )
	{
		Cubemap = Cast< UTextureCube >( UsdUtils::CreateTexture( DomeLight.GetTextureFileAttr(), UsdToUnreal::ConvertPath( DomeLight.GetPrim().GetPath() ), TEXTUREGROUP_Skybox, TexturesCache ) );

		if ( TexturesCache )
		{
			TexturesCache->CacheAsset( DomeTextureHash, Cubemap );
		}
	}

	if ( Cubemap )
	{
		LightComponent.Cubemap = Cubemap;
		LightComponent.SourceType = ESkyLightSourceType::SLS_SpecifiedCubemap;
	}

	return true;
}

bool UsdToUnreal::ConvertLuxShapingAPI( const FUsdStageInfo& StageInfo, const pxr::UsdLuxShapingAPI& ShapingAPI, USpotLightComponent& LightComponent, double TimeCode )
{
	if ( !ShapingAPI )
	{
		return false;
	}

	const float ConeAngle = UsdUtils::GetUsdValue< float >( ShapingAPI.GetShapingConeAngleAttr(), TimeCode );
	const float ConeSoftness = UsdUtils::GetUsdValue< float >( ShapingAPI.GetShapingConeSoftnessAttr(), TimeCode );

	// As of March 2021 there doesn't seem to be a consensus on what the 'softness' attribute means, according to https://groups.google.com/g/usd-interest/c/A6bc4OZjSB0
	// We approximate the best look here by trying to convert from inner/outer cone angle to softness according to the renderman docs
	LightComponent.SetInnerConeAngle( ConeAngle * ( 1.0f - ConeSoftness ) );
	LightComponent.SetOuterConeAngle( ConeAngle );

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

	// USD intensity units should be in Nits == Lux / Steradian, but there is no
	// meaningful solid angle to use to perform that conversion from Lux, so we leave intensity as-is

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
		float OldIntensity = UsdUtils::GetUsdValue< float >( Attr, TimeCode );

		// Area light with no area probably shouldn't emit any light?
		// It's not possible to set width/height less than 1 via the Details panel anyway, but just in case
		if ( FMath::IsNearlyZero( AreaInSqMeters ) )
		{
			OldIntensity = 0.0f;
		}

		AreaInSqMeters = FMath::Max( AreaInSqMeters, KINDA_SMALL_NUMBER );

		switch ( LightComponent.IntensityUnits )
		{
		case ELightUnits::Candelas:
			// Nit = candela / area
			FinalIntensityNits = OldIntensity / AreaInSqMeters;
			break;
		case ELightUnits::Lumens:
			// Nit = lumen / ( sr * area ); For area lights sr is technically 2PI, but we cancel that with an
			// extra factor of 2.0 here because URectLightComponent::SetLightBrightness uses just PI and not 2PI as steradian
			// due to some cosine distribution. This also matches the PI factor between candelas and lumen for rect lights on
			// https://docs.unrealengine.com/en-US/Engine/Rendering/LightingAndShadows/PhysicalLightUnits/index.html#point,spot,andrectlights
			FinalIntensityNits = OldIntensity / ( PI * AreaInSqMeters );
			break;
		case ELightUnits::Unitless:
			// Nit = (unitless/625) / area = candela / area
			// https://docs.unrealengine.com/en-US/Engine/Rendering/LightingAndShadows/PhysicalLightUnits/index.html#point,spot,andrectlights
			FinalIntensityNits = ( OldIntensity / 625.0f ) / AreaInSqMeters;
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

	if ( pxr::UsdAttribute Attr = Light.CreateTreatAsPointAttr() )
	{
		Attr.Set<bool>( FMath::IsNearlyZero( LightComponent.SourceRadius ), TimeCode );
	}

	float SolidAngle = 4.f * PI;
	if ( const USpotLightComponent* SpotLightComponent = Cast<const USpotLightComponent>( &LightComponent ) )
	{
		SolidAngle = 2.f * PI * ( 1.0f - SpotLightComponent->GetCosHalfConeAngle() );
	}

	// It doesn't make much physical sense to use nits for point lights in this way, but USD light intensities are always in nits
	// so we must do something. We do the analogue on the UsdToUnreal conversion, at least. Also using the solid angle for the
	// area calculation is possibly incorrect, but I think it depends on the chosen convention
	const float AreaInSqMeters = FMath::Max( SolidAngle * FMath::Square( LightComponent.SourceRadius / 100.f ), KINDA_SMALL_NUMBER );
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
			FinalIntensityNits = OldIntensity / ( SolidAngle * AreaInSqMeters );
			break;
		case ELightUnits::Unitless:
			// Nit = (unitless/625) / area = candela / area
			// https://docs.unrealengine.com/en-US/Engine/Rendering/LightingAndShadows/PhysicalLightUnits/index.html#point,spot,andrectlights
			FinalIntensityNits = ( OldIntensity / 625.0f ) / AreaInSqMeters;
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

#if WITH_EDITORONLY_DATA
	FUsdStageInfo StageInfo( Prim.GetStage() );

	if ( pxr::UsdAttribute Attr = Light.CreateTextureFileAttr() )
	{
		if ( UTextureCube* TextureCube = LightComponent.Cubemap )
		{
			if ( UAssetImportData* AssetImportData = TextureCube->AssetImportData )
			{
				FString FilePath = AssetImportData->GetFirstFilename();
				if ( !FPaths::FileExists( FilePath ) )
				{
					UE_LOG(LogTemp, Warning, TEXT("Used '%s' as cubemap when converting SkyLightComponent '%s' onto prim '%s', but the cubemap does not exist on the filesystem!"),
						*FilePath,
						*LightComponent.GetPathName(),
						*UsdToUnreal::ConvertPath(Prim.GetPath())
					);
				}

				UsdUtils::MakePathRelativeToLayer( UE::FSdfLayer( Prim.GetStage()->GetEditTarget().GetLayer() ), FilePath );
				Attr.Set<pxr::SdfAssetPath>( pxr::SdfAssetPath{ UnrealToUsd::ConvertString( *FilePath ).Get() }, TimeCode );
			}
		}
	}
#endif //  #if WITH_EDITORONLY_DATA

	return true;
}

bool UnrealToUsd::ConvertSpotLightComponent( const USpotLightComponent& LightComponent, pxr::UsdPrim& Prim, double TimeCode )
{
	pxr::UsdLuxShapingAPI ShapingAPI = pxr::UsdLuxShapingAPI::Apply( Prim );

	if ( !ShapingAPI )
	{
		return false;
	}

	if ( pxr::UsdAttribute ConeAngleAttr = ShapingAPI.CreateShapingConeAngleAttr() )
	{
		ConeAngleAttr.Set<float>( LightComponent.OuterConeAngle, TimeCode );
	}

	// As of March 2021 there doesn't seem to be a consensus on what softness means, according to https://groups.google.com/g/usd-interest/c/A6bc4OZjSB0
	// We approximate the best look here by trying to convert from inner/outer cone angle to softness according to the renderman docs
	if ( pxr::UsdAttribute SoftnessAttr = ShapingAPI.CreateShapingConeSoftnessAttr() )
	{
		// Keep in [0, 1] range, where 1 is maximum softness, i.e. inner cone angle is zero
		const float Softness = FMath::IsNearlyZero( LightComponent.OuterConeAngle ) ? 0.0 : 1.0f - LightComponent.InnerConeAngle / LightComponent.OuterConeAngle;
		SoftnessAttr.Set<float>( Softness, TimeCode );
	}

	return true;
}

#endif // #if USE_USD_SDK
