// Copyright Epic Games, Inc. All Rights Reserved.

#include "USDConversionUtils.h"

#include "USDTypesConversion.h"

#include "CineCameraActor.h"
#include "CineCameraComponent.h"

#include "Components/PoseableMeshComponent.h"
#include "Components/StaticMeshComponent.h"

#if USE_USD_SDK
#include "USDIncludesStart.h"

#include "pxr/usd/usdGeom/camera.h"
#include "pxr/usd/usdGeom/scope.h"
#include "pxr/usd/usdGeom/xform.h"
#include "pxr/usd/usdGeom/mesh.h"
#include "pxr/usd/usdSkel/root.h"

#include "USDIncludesEnd.h"

UClass* UsdUtils::GetActorTypeForPrim( const pxr::UsdPrim& Prim )
{
	if ( Prim.IsA< pxr::UsdGeomCamera >() )
	{
		return ACineCameraActor::StaticClass();
	}
	else
	{
		return AActor::StaticClass();
	}
}

UClass* UsdUtils::GetComponentTypeForPrim( const pxr::UsdPrim& Prim )
{
	if ( Prim.IsA< pxr::UsdSkelRoot >() )
	{
		return UPoseableMeshComponent::StaticClass();
	}
	else if ( Prim.IsA< pxr::UsdGeomMesh >() )
	{
		return UStaticMeshComponent::StaticClass();
	}
	else if ( Prim.IsA< pxr::UsdGeomCamera >() )
	{
		return UCineCameraComponent::StaticClass();
	}
	else if ( Prim.IsA< pxr::UsdGeomXformable >() || Prim.IsA< pxr::UsdGeomScope >() )
	{
		return USceneComponent::StaticClass();
	}
	else
	{
		return nullptr;
	}
}

TUsdStore< pxr::TfToken > UsdUtils::GetUVSetName( int32 UVChannelIndex )
{
	FScopedUnrealAllocs UnrealAllocs;

	FString UVSetName = TEXT("primvars:st");

	if ( UVChannelIndex > 0 )
	{
		UVSetName += LexToString( UVChannelIndex );
	}

	TUsdStore< pxr::TfToken > UVSetNameToken = MakeUsdStore< pxr::TfToken >( UnrealToUsd::ConvertString( *UVSetName ).Get() );

	return UVSetNameToken;
}

#endif // #if USE_USD_SDK
