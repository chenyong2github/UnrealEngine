// Copyright Epic Games, Inc. All Rights Reserved.

#include "USDGeomPointInstancerTranslator.h"

#include "USDConversionUtils.h"
#include "USDTypesConversion.h"

#if USE_USD_SDK

#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"

#include "USDIncludesStart.h"
	#include "pxr/usd/usd/prim.h"
	#include "pxr/usd/usd/stage.h"
	#include "pxr/usd/usdGeom/camera.h"
	#include "pxr/usd/usdGeom/mesh.h"
	#include "pxr/usd/usdGeom/pointInstancer.h"
	#include "pxr/usd/usdGeom/xformable.h"
#include "USDIncludesEnd.h"

namespace UsdGeomPointInstancerTranslatorImpl
{
	bool ConvertGeomPointInstancer( const pxr::UsdStageRefPtr& Stage, const pxr::UsdGeomPointInstancer& PointInstancer, const int32 ProtoIndex, UHierarchicalInstancedStaticMeshComponent& HismComponent, pxr::UsdTimeCode EvalTime )
	{
		FScopedUsdAllocs UsdAllocs;

		pxr::VtArray< int > ProtoIndices = UsdUtils::GetUsdValue< pxr::VtArray< int > >( PointInstancer.GetProtoIndicesAttr(), EvalTime );

		pxr::VtMatrix4dArray UsdInstanceTransforms;

		if ( !PointInstancer.ComputeInstanceTransformsAtTime(
				&UsdInstanceTransforms,
				EvalTime,
				EvalTime ) )
		{
			return false;
		}

		pxr::TfToken UpAxis = UsdUtils::GetUsdStageAxis( Stage );

		int32 Index = 0;

		FScopedUnrealAllocs UnrealAllocs;

		for ( pxr::GfMatrix4d& UsdMatrix : UsdInstanceTransforms )
		{
			if ( ProtoIndices[ Index ] == ProtoIndex )
			{
				FTransform InstanceTransform = UsdToUnreal::ConvertMatrix( UpAxis, UsdMatrix );

				HismComponent.AddInstance( InstanceTransform );
			}

			++Index;
		}

		HismComponent.BuildTreeIfOutdated( true, true );

		return true;
	}

	bool SetStaticMesh( const pxr::UsdGeomMesh& UsdMesh, UStaticMeshComponent& MeshComponent, const TMap< FString, UObject* >& PrimPathsToAssets )
	{
		FScopedUnrealAllocs UnrealAllocs;

		FString MeshPrimPath = UsdToUnreal::ConvertPath( UsdMesh.GetPrim().GetPrimPath() );

		UStaticMesh* StaticMesh = Cast< UStaticMesh >( PrimPathsToAssets.FindRef( MeshPrimPath ) );

		if ( StaticMesh != MeshComponent.GetStaticMesh() )
		{
			if ( MeshComponent.IsRegistered() )
			{
				MeshComponent.UnregisterComponent();
			}

			StaticMesh->CreateBodySetup(); // BodySetup is required for HISM component
			MeshComponent.SetStaticMesh( StaticMesh );

			MeshComponent.RegisterComponent();
		}

		return ( StaticMesh != nullptr );
	}
}

USceneComponent* FUsdGeomPointInstancerTranslator::CreateComponents()
{
	// Spawn Xform Actor / SceneComponent
	USceneComponent* PointInstancerRootComponent = FUsdGeomXformableTranslator::CreateComponents();

	if ( !PointInstancerRootComponent )
	{
		return nullptr;
	}

	FScopedUsdAllocs UsdAllocs;

	pxr::UsdGeomPointInstancer PointInstancer( Schema.Get() );

	if ( !PointInstancer )
	{
		return nullptr;
	}

	pxr::UsdPrim Prim = PointInstancer.GetPrim();

	// For each prototype
	const pxr::UsdRelationship& Prototypes = PointInstancer.GetPrototypesRel();
	pxr::SdfPathVector PrototypesPaths;

	if ( Prototypes.GetTargets( &PrototypesPaths ) )
	{
		TGuardValue< USceneComponent* > ParentComponentGuard( Context->ParentComponent, PointInstancerRootComponent );

		int32 PrototypeIndex = 0;
		for ( const pxr::SdfPath& PrototypePath : PrototypesPaths )
		{
			pxr::UsdPrim PrototypePrim = Prim.GetStage()->GetPrimAtPath( PrototypePath );

			FUsdGeomXformableTranslator PrototypeXformTranslator( Context, pxr::UsdTyped( PrototypePrim ) );
			USceneComponent* PrototypeXformComponent = PrototypeXformTranslator.CreateComponents();

			TGuardValue< USceneComponent* > ParentComponentGuard2( Context->ParentComponent, PrototypeXformComponent );

			// Find first UsdGeomMesh in prototype childs
			TArray< TUsdStore< pxr::UsdPrim > > ChildGeomMeshPrims = UsdUtils::GetAllPrimsOfType( PrototypePrim, pxr::TfType::Find< pxr::UsdGeomMesh >() );

			if ( ChildGeomMeshPrims.Num() > 0 )
			{
				pxr::UsdPrim PrototypeGeomMeshPrim = ChildGeomMeshPrims[ 0 ].Get();
				pxr::UsdGeomMesh PrototypeGeomMesh( PrototypeGeomMeshPrim );

				FUsdGeomXformableTranslator PrototypeGeomMeshTranslator( UHierarchicalInstancedStaticMeshComponent::StaticClass(), Context, PrototypeGeomMesh );

				USceneComponent* UsdGeomPrimComponent = PrototypeGeomMeshTranslator.CreateComponents();

				if ( UHierarchicalInstancedStaticMeshComponent* HismComponent = Cast< UHierarchicalInstancedStaticMeshComponent >( UsdGeomPrimComponent ) )
				{
					UsdGeomPointInstancerTranslatorImpl::SetStaticMesh( PrototypeGeomMesh, *HismComponent, Context->PrimPathsToAssets );
					UsdGeomPointInstancerTranslatorImpl::ConvertGeomPointInstancer( Prim.GetStage(), PointInstancer, PrototypeIndex, *HismComponent, pxr::UsdTimeCode( Context->Time ) );
				}
			}

			++PrototypeIndex;
		}
	}

	return PointInstancerRootComponent;
}

#endif // #if USE_USD_SDK
