// Copyright Epic Games, Inc. All Rights Reserved.

#include "USDGeomCameraTranslator.h"

#include "USDMemory.h"
#include "USDPrimConversion.h"

#include "CineCameraActor.h"
#include "CineCameraComponent.h"

#if USE_USD_SDK
#include "USDIncludesStart.h"
	#include "pxr/usd/usdGeom/camera.h"
#include "USDIncludesEnd.h"


USceneComponent* FUsdGeomCameraTranslator::CreateComponents()
{
	constexpr bool bNeedsActor = true;
	USceneComponent* RootComponent = CreateComponentsEx( TSubclassOf< USceneComponent >( UCineCameraComponent::StaticClass() ), bNeedsActor );

	UpdateComponents( RootComponent );

	return RootComponent;
}

void FUsdGeomCameraTranslator::UpdateComponents( USceneComponent* SceneComponent )
{
	if ( !SceneComponent )
	{
		return;
	}

	FUsdGeomXformableTranslator::UpdateComponents( SceneComponent );

	// The CineCamera component is not actually the root component
	for ( USceneComponent* ChildComponent : SceneComponent->GetAttachChildren() )
	{
		if ( UCineCameraComponent* CameraComponent = Cast< UCineCameraComponent >( ChildComponent ) )
		{
			FScopedUsdAllocs UsdAllocs;
			UsdToUnreal::ConvertGeomCamera( Schema.Get().GetPrim().GetStage(), pxr::UsdGeomCamera( Schema.Get() ), *CameraComponent, pxr::UsdTimeCode( Context->Time ) );
		}
	}
}

#endif // #if USE_USD_SDK
