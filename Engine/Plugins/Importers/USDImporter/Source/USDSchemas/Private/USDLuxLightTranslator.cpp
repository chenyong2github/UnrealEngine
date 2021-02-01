// Copyright Epic Games, Inc. All Rights Reserved.

#include "USDLuxLightTranslator.h"

#include "USDConversionUtils.h"
#include "USDLightConversion.h"
#include "USDTypesConversion.h"
#include "UsdWrappers/UsdPrim.h"
#include "UsdWrappers/UsdStage.h"

#include "Components/DirectionalLightComponent.h"
#include "Components/LightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/RectLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/SpotLightComponent.h"
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

USceneComponent* FUsdLuxLightTranslator::CreateComponents()
{
	const bool bNeedsActor = true;
	return CreateComponentsEx( {}, bNeedsActor );
}

void FUsdLuxLightTranslator::UpdateComponents( USceneComponent* SceneComponent )
{
	Super::UpdateComponents( SceneComponent );

	ULightComponentBase* LightComponent = Cast< ULightComponentBase >( SceneComponent );

	if ( !LightComponent )
	{
		return;
	}

	FScopedUsdAllocs UsdAllocs;

	pxr::UsdPrim Prim = GetPrim();
	pxr::UsdLuxLight UsdLight{ Prim };

	if ( !UsdLight )
	{
		return;
	}

	LightComponent->UnregisterComponent();

	UsdToUnreal::ConvertLight( pxr::UsdLuxLight{ Prim }, *LightComponent, Context->Time );

	if ( UDirectionalLightComponent* DirectionalLightComponent = Cast< UDirectionalLightComponent >( SceneComponent ) )
	{
		UsdToUnreal::ConvertDistantLight( pxr::UsdLuxDistantLight{ Prim }, *DirectionalLightComponent, Context->Time );
	}
	else if ( URectLightComponent* RectLightComponent = Cast< URectLightComponent >( SceneComponent ) )
	{
		FUsdStageInfo StageInfo( Context->Stage );

		if ( pxr::UsdLuxRectLight UsdRectLight{ Prim } )
		{
			UsdToUnreal::ConvertRectLight( StageInfo, UsdRectLight, *RectLightComponent, Context->Time );
		}
		else if ( pxr::UsdLuxDiskLight UsdDiskLight{ Prim } )
		{
			UsdToUnreal::ConvertDiskLight( StageInfo, UsdDiskLight, *RectLightComponent, Context->Time );
		}
	}
	else if ( UPointLightComponent* PointLightComponent = Cast< UPointLightComponent >( SceneComponent ) )
	{
		if ( USpotLightComponent* SpotLightComponent = Cast< USpotLightComponent >( SceneComponent ) )
		{
			UsdToUnreal::ConvertLuxShapingAPI( FUsdStageInfo( Context->Stage ), pxr::UsdLuxShapingAPI{ Prim }, *SpotLightComponent, Context->Time );
		}

		UsdToUnreal::ConvertSphereLight( FUsdStageInfo( Context->Stage ), pxr::UsdLuxSphereLight{ Prim }, *PointLightComponent, Context->Time );
	}
	else if ( USkyLightComponent* SkyLightComponent = Cast< USkyLightComponent >( SceneComponent ) )
	{
		UsdToUnreal::ConvertDomeLight( FUsdStageInfo( Context->Stage ), pxr::UsdLuxDomeLight{ Prim }, *SkyLightComponent, Context->AssetCache.Get(), Context->Time );
		SkyLightComponent->Mobility = EComponentMobility::Movable; // We won't bake geometry in the sky light so it needs to be movable
	}

	if ( !LightComponent->IsRegistered() )
	{
		LightComponent->RegisterComponent();
	}
}

#endif // #if USE_USD_SDK