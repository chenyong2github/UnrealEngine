// Copyright Epic Games, Inc. All Rights Reserved.

#include "USDGeomPointInstancerTranslator.h"

#include "MeshTranslationImpl.h"
#include "USDAssetCache.h"
#include "USDAssetImportData.h"
#include "USDConversionUtils.h"
#include "USDSchemasModule.h"
#include "USDTypesConversion.h"

#include "UsdWrappers/UsdTyped.h"

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

		FUsdStageInfo StageInfo( Stage );

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

	UStaticMesh* SetStaticMesh( const pxr::UsdGeomMesh& UsdMesh, UStaticMeshComponent& MeshComponent, const UUsdAssetCache& AssetCache )
	{
		FScopedUnrealAllocs UnrealAllocs;

		FString MeshPrimPath = UsdToUnreal::ConvertPath( UsdMesh.GetPrim().GetPrimPath() );

		UStaticMesh* StaticMesh = Cast< UStaticMesh >( AssetCache.GetAssetForPrim( MeshPrimPath ) );

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

		return StaticMesh;
	}

	TUsdStore< pxr::SdfPath > UnwindToNonCollapsedPrim( FUsdSchemaTranslator::ECollapsingType CollapsingType, TUsdStore< pxr::UsdPrim > UsdPrim, const TSharedRef< FUsdSchemaTranslationContext >& TranslationContext )
	{
		IUsdSchemasModule& UsdSchemasModule = FModuleManager::Get().LoadModuleChecked< IUsdSchemasModule >( TEXT("USDSchemas") );

		TUsdStore< pxr::SdfPath > UsdPrimPath = UsdPrim.Get().GetPrimPath();

		if ( TSharedPtr< FUsdSchemaTranslator > SchemaTranslator = UsdSchemasModule.GetTranslatorRegistry().CreateTranslatorForSchema( TranslationContext, UE::FUsdTyped( UsdPrim.Get() ) ) )
		{
			while ( SchemaTranslator->IsCollapsed( CollapsingType ) )
			{
				UsdPrimPath = UsdPrimPath.Get().GetParentPath();
				UsdPrim = UsdPrim.Get().GetStage()->GetPrimAtPath( UsdPrimPath.Get() );

				SchemaTranslator = UsdSchemasModule.GetTranslatorRegistry().CreateTranslatorForSchema( TranslationContext, UE::FUsdTyped( UsdPrim.Get() ) );

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

	PointInstancerRootComponent->Modify();

	FScopedUsdAllocs UsdAllocs;

	pxr::UsdPrim Prim = GetPrim();
	pxr::UsdGeomPointInstancer PointInstancer( Prim );

	if ( !PointInstancer )
	{
		return;
	}

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

			USceneComponent* PrototypeParentComponent = Context->ParentComponent;

			// Temp fix to prevent us from creating yet another UStaticMeshComponent as a parent prim to the HISM component
			const bool bNeedsActor = false;
			TOptional<TGuardValue< USceneComponent* >> ParentComponentGuard2;
			if ( !PrototypePrim.IsA<pxr::UsdGeomMesh>() )
			{
				FUsdGeomXformableTranslator PrototypeXformTranslator( Context, UE::FUsdTyped( PrototypePrim ) );

				if ( USceneComponent* PrototypeXformComponent = PrototypeXformTranslator.CreateComponentsEx( {}, bNeedsActor ) )
				{
					PrototypeParentComponent = PrototypeXformComponent;
				}

				ParentComponentGuard2.Emplace(Context->ParentComponent, PrototypeParentComponent );
			}

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
						FUsdGeomXformableTranslator PrototypeGeomXformableTranslator( UHierarchicalInstancedStaticMeshComponent::StaticClass(), Context, UE::FUsdTyped( PrototypeGeomMesh ) );

						USceneComponent* UsdGeomPrimComponent = PrototypeGeomXformableTranslator.CreateComponentsEx( {}, bNeedsActor );

						if ( UHierarchicalInstancedStaticMeshComponent* HismComponent = Cast< UHierarchicalInstancedStaticMeshComponent >( UsdGeomPrimComponent ) )
						{
							UUsdAssetCache& AssetCache = *Context->AssetCache.Get();
							UStaticMesh* StaticMesh = UsdGeomPointInstancerTranslatorImpl::SetStaticMesh( PrototypeGeomMesh, *HismComponent, AssetCache );
							UsdGeomPointInstancerTranslatorImpl::ConvertGeomPointInstancer( Prim.GetStage(), PointInstancer, PrototypeIndex, *HismComponent, pxr::UsdTimeCode( Context->Time ) );

							// Handle material overrides
							if ( StaticMesh )
							{
#if WITH_EDITOR
								// If the prim paths match, it means that it was this prim that created (and so "owns") the static mesh,
								// so its material assignments will already be directly on the mesh. If they differ, we're using some other prim's mesh,
								// so we may need material overrides on our component
								UUsdAssetImportData* UsdImportData = Cast<UUsdAssetImportData>( StaticMesh->AssetImportData );
								if ( UsdImportData && UsdImportData->PrimPath != PrimPath.GetString() )
#endif // WITH_EDITOR
								{
									TArray<UMaterialInterface*> ExistingAssignments;
									for ( FStaticMaterial& StaticMaterial : StaticMesh->GetStaticMaterials() )
									{
										ExistingAssignments.Add( StaticMaterial.MaterialInterface );
									}

									MeshTranslationImpl::SetMaterialOverrides(
										PrototypeTargetPrim,
										ExistingAssignments,
										*HismComponent,
										AssetCache,
										Context->Time,
										Context->ObjectFlags,
										Context->bAllowInterpretingLODs,
										Context->RenderContext
									);
								}
							}
						}
					}
				}
			}
		}
	}

	Super::UpdateComponents( PointInstancerRootComponent );
}

#endif // #if USE_USD_SDK

