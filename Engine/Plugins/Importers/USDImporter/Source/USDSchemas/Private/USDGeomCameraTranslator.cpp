// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "USDGeomCameraTranslator.h"

#include "USDMemory.h"
#include "USDPrimConversion.h"
#include "USDSchemasModule.h"

#include "CineCameraActor.h"
#include "CineCameraComponent.h"
#include "Modules/ModuleManager.h"

#if USE_USD_SDK
#include "USDIncludesStart.h"
	#include "pxr/usd/usdGeom/camera.h"
#include "USDIncludesEnd.h"


void FUsdGeomCameraTranslator::RegisterTranslator()
{
	IUsdSchemasModule& UsdSchemasModule = FModuleManager::Get().LoadModuleChecked< IUsdSchemasModule >( TEXT("USDSchemas") );
	UsdSchemasModule.GetTranslatorRegistry().Register< FUsdGeomCameraTranslator >( TEXT("UsdGeomCamera") );
}

USceneComponent* FUsdGeomCameraTranslator::CreateComponents()
{
	constexpr bool bNeedsActor = true;
	USceneComponent* RootComponent = FUsdGeomXformableTranslator::CreateComponents( UCineCameraComponent::StaticClass(), bNeedsActor );

	UpdateComponents( RootComponent );

	return RootComponent;
}

void FUsdGeomCameraTranslator::UpdateComponents( USceneComponent* SceneComponent )
{
	if ( UCineCameraComponent* CameraComponent = Cast< UCineCameraComponent >( SceneComponent ) )
	{
		FScopedUsdAllocs UsdAllocs;
		UsdToUnreal::ConvertGeomCamera( Schema.Get().GetPrim().GetStage(), pxr::UsdGeomCamera( Schema.Get() ), *CameraComponent, pxr::UsdTimeCode( Context->Time ) );
	}
}

#endif // #if USE_USD_SDK
