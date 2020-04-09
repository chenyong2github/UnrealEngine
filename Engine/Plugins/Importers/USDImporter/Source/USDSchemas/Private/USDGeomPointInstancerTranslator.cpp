// Copyright Epic Games, Inc. All Rights Reserved.

#include "USDGeomPointInstancerTranslator.h"

#include "USDConversionUtils.h"
#include "USDSchemasModule.h"
#include "USDTypesConversion.h"

#if USE_USD_SDK

#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Modules/ModuleManager.h"

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

		UsdToUnreal::FUsdStageInfo StageInfo( Stage );

		int32 Index = 0;

		FScopedUnrealAllocs UnrealAllocs;

		for ( pxr::GfMatrix4d& UsdMatrix : UsdInstanceTransforms )
		{
			if ( ProtoIndices[ Index ] == ProtoIndex )
			{
				FTransform InstanceTransform = UsdToUnreal::ConvertMatrix( StageInfo, UsdMatrix );

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

	TUsdStore< pxr::SdfPath > UnwindToNonCollapsedPrim( FUsdSchemaTranslator::ECollapsingType CollapsingType, TUsdStore< pxr::UsdPrim > UsdPrim, const TSharedRef< FUsdSchemaTranslationContext >& TranslationContext )
	{
		IUsdSchemasModule& UsdSchemasModule = FModuleManager::Get().LoadModuleChecked< IUsdSchemasModule >( TEXT("USDSchemas") );

		TUsdStore< pxr::SdfPath > UsdPrimPath = UsdPrim.Get().GetPrimPath();

		if ( TSharedPtr< FUsdSchemaTranslator > SchemaTranslator = UsdSchemasModule.GetTranslatorRegistry().CreateTranslatorForSchema( TranslationContext, pxr::UsdTyped( UsdPrim.Get() ) ) )
		{
			while ( SchemaTranslator->IsCollapsed( CollapsingType ) )
			{
				UsdPrimPath = UsdPrimPath.Get().GetParentPath();
				UsdPrim = UsdPrim.Get().GetStage()->GetPrimAtPath( UsdPrimPath.Get() );

				SchemaTranslator = UsdSchemasModule.GetTranslatorRegistry().CreateTranslatorForSchema( TranslationContext, pxr::UsdTyped( UsdPrim.Get() ) );

				if ( !SchemaTranslator.IsValid() )
				{
					break;
				}
			}
		}

		return UsdPrimPath;
	};
}

void FUsdGeomPointInstancerTranslator::UpdateComponents( USceneComponent* PointInstancerRootComponent )
{
	if ( !PointInstancerRootComponent )
	{
		return;
	}

	FScopedUsdAllocs UsdAllocs;

	pxr::UsdGeomPointInstancer PointInstancer( Schema.Get() );

	if ( !PointInstancer )
	{
		return;
	}

	pxr::UsdPrim Prim = PointInstancer.GetPrim();

	// For each prototype
	const pxr::UsdRelationship& Prototypes = PointInstancer.GetPrototypesRel();
	pxr::SdfPathVector PrototypesPaths;

	TSet< FString > ProcessedPrims;

	if ( Prototypes.GetTargets( &PrototypesPaths ) )
	{
		TGuardValue< USceneComponent* > ParentComponentGuard( Context->ParentComponent, PointInstancerRootComponent );

		for ( int32 PrototypeIndex = 0; PrototypeIndex < PrototypesPaths.size(); ++PrototypeIndex)
		{
			pxr::UsdPrim PrototypePrim = Prim.GetStage()->GetPrimAtPath( PrototypesPaths[PrototypeIndex] );
			if ( !Prim )
			{
				continue;
			}

			FUsdGeomXformableTranslator PrototypeXformTranslator( Context, pxr::UsdTyped( PrototypePrim ) );

			const bool bNeedsActor = false;
			USceneComponent* PrototypeXformComponent = PrototypeXformTranslator.CreateComponentsEx( {}, bNeedsActor );

			TGuardValue< USceneComponent* > ParentComponentGuard2( Context->ParentComponent, PrototypeXformComponent );

			// Find UsdGeomMeshes in prototype childs
			TArray< TUsdStore< pxr::UsdPrim > > ChildGeomMeshPrims = UsdUtils::GetAllPrimsOfType( PrototypePrim, pxr::TfType::Find< pxr::UsdGeomMesh >() );

			for ( const TUsdStore< pxr::UsdPrim >& PrototypeGeomMeshPrim : ChildGeomMeshPrims )
			{
				TUsdStore< pxr::SdfPath > PrototypeTargetPrimPath = UsdGeomPointInstancerTranslatorImpl::UnwindToNonCollapsedPrim( FUsdSchemaTranslator::ECollapsingType::Assets, PrototypeGeomMeshPrim, Context );

				const FString UEPrototypeTargetPrimPath = UsdToUnreal::ConvertPath( PrototypeTargetPrimPath.Get() );

				if ( !ProcessedPrims.Contains( UEPrototypeTargetPrimPath ) )
				{
					ProcessedPrims.Add( UEPrototypeTargetPrimPath );

					if ( pxr::UsdPrim PrototypeTargetPrim = Prim.GetStage()->GetPrimAtPath( PrototypeTargetPrimPath.Get() ) )
					{
						pxr::UsdGeomMesh PrototypeGeomMesh( PrototypeTargetPrim );
						FUsdGeomXformableTranslator PrototypeGeomXformableTranslator( UHierarchicalInstancedStaticMeshComponent::StaticClass(), Context, PrototypeGeomMesh );

						USceneComponent* UsdGeomPrimComponent = PrototypeGeomXformableTranslator.CreateComponentsEx( {}, bNeedsActor );

						if ( UHierarchicalInstancedStaticMeshComponent* HismComponent = Cast< UHierarchicalInstancedStaticMeshComponent >( UsdGeomPrimComponent ) )
						{
							UsdGeomPointInstancerTranslatorImpl::SetStaticMesh( PrototypeGeomMesh, *HismComponent, Context->PrimPathsToAssets );
							UsdGeomPointInstancerTranslatorImpl::ConvertGeomPointInstancer( Prim.GetStage(), PointInstancer, PrototypeIndex, *HismComponent, pxr::UsdTimeCode( Context->Time ) );
						}
					}
				}
			}
		}
	}
}

#endif // #if USE_USD_SDK

