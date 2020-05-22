// Copyright Epic Games, Inc. All Rights Reserved.

#include "USDConversionUtils.h"

#include "USDTypesConversion.h"

#include "UsdWrappers/UsdPrim.h"

#include "Algo/Copy.h"
#include "CineCameraActor.h"
#include "CineCameraComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/RectLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/PointLight.h"
#include "Engine/RectLight.h"
#include "Engine/SkyLight.h"
#include "ObjectTools.h"

#if USE_USD_SDK
#include "USDIncludesStart.h"
	#include "pxr/base/tf/token.h"
	#include "pxr/usd/usd/attribute.h"
	#include "pxr/usd/usd/editContext.h"
	#include "pxr/usd/usd/modelAPI.h"
	#include "pxr/usd/usd/primRange.h"
	#include "pxr/usd/usd/stage.h"
	#include "pxr/usd/usd/variantSets.h"
	#include "pxr/usd/usdGeom/camera.h"
	#include "pxr/usd/usdGeom/scope.h"
	#include "pxr/usd/usdGeom/xform.h"
	#include "pxr/usd/usdGeom/mesh.h"
	#include "pxr/usd/usdGeom/metrics.h"
	#include "pxr/usd/usdLux/diskLight.h"
	#include "pxr/usd/usdLux/distantLight.h"
	#include "pxr/usd/usdLux/domeLight.h"
	#include "pxr/usd/usdLux/rectLight.h"
	#include "pxr/usd/usdLux/sphereLight.h"
	#include "pxr/usd/usdSkel/root.h"
#include "USDIncludesEnd.h"

#include <string>

template< typename ValueType >
ValueType UsdUtils::GetUsdValue( const pxr::UsdAttribute& Attribute, pxr::UsdTimeCode TimeCode )
{
	ValueType Value{};
	if ( Attribute )
	{
		Attribute.Get( &Value, TimeCode );
	}

	return Value;
}

// Explicit template instantiation
template USDUTILITIES_API bool							UsdUtils::GetUsdValue< bool >( const pxr::UsdAttribute& Attribute, pxr::UsdTimeCode TimeCode );
template USDUTILITIES_API float							UsdUtils::GetUsdValue< float >( const pxr::UsdAttribute& Attribute, pxr::UsdTimeCode TimeCode );
template USDUTILITIES_API pxr::GfVec3f					UsdUtils::GetUsdValue< pxr::GfVec3f >( const pxr::UsdAttribute& Attribute, pxr::UsdTimeCode TimeCode );
template USDUTILITIES_API pxr::SdfAssetPath				UsdUtils::GetUsdValue< pxr::SdfAssetPath >( const pxr::UsdAttribute& Attribute, pxr::UsdTimeCode TimeCode );
template USDUTILITIES_API pxr::VtArray< pxr::GfVec3f >	UsdUtils::GetUsdValue< pxr::VtArray< pxr::GfVec3f > >( const pxr::UsdAttribute& Attribute, pxr::UsdTimeCode TimeCode );
template USDUTILITIES_API pxr::VtArray< float >			UsdUtils::GetUsdValue< pxr::VtArray< float > >( const pxr::UsdAttribute& Attribute, pxr::UsdTimeCode TimeCode );
template USDUTILITIES_API pxr::VtArray< int >			UsdUtils::GetUsdValue< pxr::VtArray< int > >( const pxr::UsdAttribute& Attribute, pxr::UsdTimeCode TimeCode );

pxr::TfToken UsdUtils::GetUsdStageAxis( const pxr::UsdStageRefPtr& Stage )
{
	return pxr::UsdGeomGetStageUpAxis( Stage );
}

void UsdUtils::SetUsdStageAxis( const pxr::UsdStageRefPtr& Stage, pxr::TfToken Axis )
{
	pxr::UsdGeomSetStageUpAxis( Stage, Axis );
}

float UsdUtils::GetUsdStageMetersPerUnit( const pxr::UsdStageRefPtr& Stage )
{
	return (float)pxr::UsdGeomGetStageMetersPerUnit( Stage );
}

void UsdUtils::SetUsdStageMetersPerUnit( const pxr::UsdStageRefPtr& Stage, float MetersPerUnit )
{
	if ( !Stage || !Stage->GetRootLayer() )
	{
		return;
	}

	pxr::UsdEditContext( Stage, Stage->GetRootLayer() );
	pxr::UsdGeomSetStageMetersPerUnit( Stage, MetersPerUnit );
}

bool UsdUtils::HasCompositionArcs( const pxr::UsdPrim& Prim )
{
	if ( !Prim )
	{
		return false;
	}

	return Prim.HasAuthoredReferences() || Prim.HasPayload() || Prim.HasAuthoredInherits() || Prim.HasAuthoredSpecializes() || Prim.HasVariantSets();
}

UClass* UsdUtils::GetActorTypeForPrim( const pxr::UsdPrim& Prim )
{
	if ( Prim.IsA< pxr::UsdGeomCamera >() )
	{
		return ACineCameraActor::StaticClass();
	}
	else if ( Prim.IsA< pxr::UsdLuxDistantLight >() )
	{
		return ADirectionalLight::StaticClass();
	}
	else if ( Prim.IsA< pxr::UsdLuxRectLight >() || Prim.IsA< pxr::UsdLuxDiskLight >() )
	{
		return ARectLight::StaticClass();
	}
	else if ( Prim.IsA< pxr::UsdLuxSphereLight >() )
	{
		return APointLight::StaticClass();
	}
	else if ( Prim.IsA< pxr::UsdLuxDomeLight >() )
	{
		return ASkyLight::StaticClass();
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
	else if ( Prim.IsA< pxr::UsdLuxDistantLight >() )
	{
		return UDirectionalLightComponent::StaticClass();
	}
	else if ( Prim.IsA< pxr::UsdLuxRectLight >() || Prim.IsA< pxr::UsdLuxDiskLight >() )
	{
		return URectLightComponent::StaticClass();
	}
	else if ( Prim.IsA< pxr::UsdLuxSphereLight >() )
	{
		return UPointLightComponent::StaticClass();
	}
	else if ( Prim.IsA< pxr::UsdLuxDomeLight >() )
	{
		return USkyLightComponent::StaticClass();
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
    return GetAllPrimsOfType( StartPrim, SchemaType, []( const pxr::UsdPrim& ) { return false; }, ExcludeSchemaTypes );
}

TArray< TUsdStore< pxr::UsdPrim > > UsdUtils::GetAllPrimsOfType( const pxr::UsdPrim& StartPrim, const pxr::TfType& SchemaType, TFunction< bool( const pxr::UsdPrim& ) > PruneChildren, const TArray< TUsdStore< pxr::TfType > >& ExcludeSchemaTypes )
{
	TArray< TUsdStore< pxr::UsdPrim > > Result;

	pxr::UsdPrimRange PrimRange( StartPrim, pxr::UsdTraverseInstanceProxies() );

	for ( pxr::UsdPrimRange::iterator PrimRangeIt = PrimRange.begin(); PrimRangeIt != PrimRange.end(); ++PrimRangeIt )
	{
		bool bIsExcluded = false;

		for ( const TUsdStore< pxr::TfType >& SchemaToExclude : ExcludeSchemaTypes )
		{
			if ( PrimRangeIt->IsA( SchemaToExclude.Get() ) )
			{
				bIsExcluded = true;
				break;
			}
		}

		if ( !bIsExcluded && PrimRangeIt->IsA( SchemaType ) )
		{
			Result.Add( *PrimRangeIt );
		}

		if ( bIsExcluded || PruneChildren( *PrimRangeIt ) )
		{
			PrimRangeIt.PruneChildren();
		}
	}

	return Result;
}

FString UsdUtils::GetAssetPathFromPrimPath( const FString& RootContentPath, const pxr::UsdPrim& Prim )
{
	FString FinalPath;

	auto GetEnclosingModelPrim = []( const pxr::UsdPrim& Prim ) -> pxr::UsdPrim
	{
		pxr::UsdPrim ModelPrim = Prim.GetParent();

		while ( ModelPrim )
		{
			if ( IUsdPrim::IsKindChildOf( ModelPrim, "model" ) )
			{
				break;
			}
			else
			{
				ModelPrim = ModelPrim.GetParent();
			}
		}

		return ModelPrim.IsValid() ? ModelPrim : Prim;
	};

	const pxr::UsdPrim& ModelPrim = GetEnclosingModelPrim( Prim );

	const FString RawPrimName = UsdToUnreal::ConvertString( Prim.GetName() );

	pxr::UsdModelAPI ModelApi = pxr::UsdModelAPI( ModelPrim );

	std::string RawAssetName;
	ModelApi.GetAssetName( &RawAssetName );

	FString AssetName = UsdToUnreal::ConvertString( RawAssetName );
	FString MeshName = ObjectTools::SanitizeObjectName( RawPrimName );

	FString USDPath = UsdToUnreal::ConvertString( Prim.GetPrimPath().GetString().c_str() );

	pxr::SdfAssetPath AssetPath;
	if ( ModelApi.GetAssetIdentifier( &AssetPath ) )
	{
		std::string AssetIdentifier = AssetPath.GetAssetPath();
		USDPath = UsdToUnreal::ConvertString( AssetIdentifier.c_str() );

		USDPath = FPaths::ConvertRelativePathToFull( RootContentPath, USDPath );

		FPackageName::TryConvertFilenameToLongPackageName( USDPath, USDPath );
		USDPath.RemoveFromEnd( AssetName );
	}

	FString VariantName;

	if ( ModelPrim.HasVariantSets() )
	{
		pxr::UsdVariantSet ModelVariantSet = ModelPrim.GetVariantSet( "modelingVariant" );
		if ( ModelVariantSet.IsValid() )
		{
			std::string VariantSelection = ModelVariantSet.GetVariantSelection();

			if ( VariantSelection.length() > 0 )
			{
				VariantName = UsdToUnreal::ConvertString( VariantSelection.c_str() );
			}
		}
	}

	if ( !VariantName.IsEmpty() )
	{
		USDPath = USDPath / VariantName;
	}

	USDPath.RemoveFromStart( TEXT("/") );
	USDPath.RemoveFromEnd( RawPrimName );
	FinalPath /= (USDPath / MeshName);

	return FinalPath;
}
#endif // #if USE_USD_SDK

bool UsdUtils::IsAnimated( const UE::FUsdPrim& Prim )
{
#if USE_USD_SDK
	return IsAnimated( static_cast< const pxr::UsdPrim& >( Prim ) );
#else
	return false;
#endif // #if USE_USD_SDK
}

TArray< UE::FUsdPrim > UsdUtils::GetAllPrimsOfType( const UE::FUsdPrim& StartPrim, const TCHAR* SchemaName )
{
	return GetAllPrimsOfType( StartPrim, SchemaName, []( const UE::FUsdPrim& ) { return false; } );
}

TArray< UE::FUsdPrim > UsdUtils::GetAllPrimsOfType( const UE::FUsdPrim& StartPrim, const TCHAR* SchemaName, TFunction< bool( const UE::FUsdPrim& ) > PruneChildren )
{
	TArray< UE::FUsdPrim > Result;

#if USE_USD_SDK
	const pxr::TfType SchemaType = pxr::TfType::FindByName( TCHAR_TO_ANSI( SchemaName ) );

	auto UsdPruneChildren = [ &PruneChildren ]( const pxr::UsdPrim& ChildPrim ) -> bool
	{
		return PruneChildren( UE::FUsdPrim( ChildPrim ) );
	};

	TArray< TUsdStore< pxr::UsdPrim > > UsdResult = GetAllPrimsOfType( StartPrim, SchemaType, UsdPruneChildren );

	for ( const TUsdStore< pxr::UsdPrim >& Prim : UsdResult )
	{
		Result.Emplace( Prim.Get() );
	}
#endif // #if USE_USD_SDK

	return Result;
}
