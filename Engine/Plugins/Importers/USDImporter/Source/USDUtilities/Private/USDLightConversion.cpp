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
#include "Engine/TextureCube.h"
#include "Factories/TextureFactory.h"

#if USE_USD_SDK

#include "USDIncludesStart.h"
	#include "pxr/usd/usdLux/diskLight.h"
	#include "pxr/usd/usdLux/distantLight.h"
	#include "pxr/usd/usdLux/domeLight.h"
	#include "pxr/usd/usdLux/rectLight.h"
	#include "pxr/usd/usdLux/sphereLight.h"
#include "USDIncludesEnd.h"

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
	LightComponent.Intensity *= LightComponent.LightSourceAngle; // Lux = Nits * Steradian

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
	LightComponent.Intensity *= PI * AreaInSqMeters; // Lumen = Nits * PI * Area
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
	LightComponent.Intensity *= PI * AreaInSqMeters; // Lumen = Nits * PI * Area
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
	LightComponent.Intensity *= PI * AreaInSqMeters; // Lumen = Nits * PI * Area
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

#endif // #if USE_USD_SDK
