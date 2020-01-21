// Copyright Epic Games, Inc. All Rights Reserved.

#include "USDConversionUtils.h"

#include "USDTypesConversion.h"

#include "Algo/Copy.h"
#include "CineCameraActor.h"
#include "CineCameraComponent.h"

#include "Components/PoseableMeshComponent.h"
#include "Components/StaticMeshComponent.h"

#if USE_USD_SDK
#include "USDIncludesStart.h"

#include "pxr/usd/usd/attribute.h"
#include "pxr/usd/usd/primRange.h"
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
	else if ( Prim.IsA< pxr::UsdGeomXformable >() )
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

bool UsdUtils::IsAnimated( const pxr::UsdPrim& Prim )
{
	FScopedUsdAllocs UsdAllocs;

	bool bHasXformbaleTimeSamples = false;
	{
		pxr::UsdGeomXformable Xformable( Prim );

		if ( Xformable )
		{
			std::vector< double > TimeSamples;
			Xformable.GetTimeSamples( &TimeSamples );

			bHasXformbaleTimeSamples = TimeSamples.size() > 0;
		}
	}

	bool bHasAttributesTimeSamples = false;
	{
		if ( !bHasXformbaleTimeSamples )
		{
			const std::vector< pxr::UsdAttribute >& Attributes = Prim.GetAttributes();

			for ( const pxr::UsdAttribute& Attribute : Attributes )
			{
				bHasAttributesTimeSamples = Attribute.ValueMightBeTimeVarying();
				if ( bHasAttributesTimeSamples )
				{
					break;
				}
			}
		}
	}

	return bHasXformbaleTimeSamples || bHasAttributesTimeSamples;
}

TArray< TUsdStore< pxr::UsdPrim > > UsdUtils::GetAllPrimsOfType( const pxr::UsdPrim& StartPrim, const pxr::TfType& SchemaType, const TArray< TUsdStore< pxr::TfType > >& ExcludeSchemaTypes )
{
    TArray< TUsdStore< pxr::UsdPrim > > Result;

	pxr::UsdPrimRange PrimRange( StartPrim, pxr::UsdTraverseInstanceProxies() );

	for ( pxr::UsdPrimRange::iterator PrimRangeIt = PrimRange.begin(); PrimRangeIt != PrimRange.end(); ++PrimRangeIt )
	{
		if ( PrimRangeIt->IsA( SchemaType ) )
		{
			Result.Add( *PrimRangeIt );
		}
		else
		{
			for ( const TUsdStore< pxr::TfType >& SchemaToExclude : ExcludeSchemaTypes )
			{
				if ( PrimRangeIt->IsA( SchemaToExclude.Get() ) )
				{
					PrimRangeIt.PruneChildren();
				}
			}
		}
	}

    return Result;
}

#endif // #if USE_USD_SDK
