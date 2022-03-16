// Copyright Epic Games, Inc. All Rights Reserved.

#include "USDGeomPointInstancerTranslator.h"

#include "MeshTranslationImpl.h"
#include "USDAssetCache.h"
#include "USDAssetImportData.h"
#include "USDConversionUtils.h"
#include "USDGeomMeshConversion.h"
#include "USDLog.h"
#include "USDSchemasModule.h"
#include "USDTypesConversion.h"

#include "UsdWrappers/UsdTyped.h"

#if WITH_EDITOR
#include "Editor.h"
#endif // WITH_EDITOR

#if USE_USD_SDK

#include "Async/Async.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Misc/ScopedSlowTask.h"
#include "Modules/ModuleManager.h"

#include "USDIncludesStart.h"
	#include "pxr/usd/usd/prim.h"
	#include "pxr/usd/usd/stage.h"
	#include "pxr/usd/usdGeom/camera.h"
	#include "pxr/usd/usdGeom/mesh.h"
	#include "pxr/usd/usdGeom/pointInstancer.h"
	#include "pxr/usd/usdGeom/xformable.h"
#include "USDIncludesEnd.h"

#define LOCTEXT_NAMESPACE "USDGeomPointInstancer"

namespace UsdGeomPointInstancerTranslatorImpl
{
	bool GetPointInstancerTransforms( const pxr::UsdStageRefPtr& Stage, const pxr::UsdGeomPointInstancer& PointInstancer, const int32 ProtoIndex, pxr::UsdTimeCode EvalTime, TArray<FTransform>& OutInstanceTransforms )
	{
		TRACE_CPUPROFILER_EVENT_SCOPE( GetPointInstancerTransforms );

		FScopedUsdAllocs UsdAllocs;

		pxr::VtArray< int > ProtoIndices = UsdUtils::GetUsdValue< pxr::VtArray< int > >( PointInstancer.GetProtoIndicesAttr(), EvalTime );

		pxr::VtMatrix4dArray UsdInstanceTransforms;

		// We don't want the prototype root prim's transforms to be included in these, as they'll already be baked into the meshes
		// themselves
		if ( !PointInstancer.ComputeInstanceTransformsAtTime( &UsdInstanceTransforms, EvalTime, EvalTime, pxr::UsdGeomPointInstancer::ExcludeProtoXform ) )
		{
			return false;
		}

		FUsdStageInfo StageInfo( Stage );

		int32 Index = 0;

		FScopedUnrealAllocs UnrealAllocs;

		OutInstanceTransforms.Reset( UsdInstanceTransforms.size() );

		for ( pxr::GfMatrix4d& UsdMatrix : UsdInstanceTransforms )
		{
			if ( ProtoIndices[ Index ] == ProtoIndex )
			{
				OutInstanceTransforms.Add( UsdToUnreal::ConvertMatrix( StageInfo, UsdMatrix ) );
			}

			++Index;
		}

		return true;
	}

	void ApplyPointInstanceTransforms( UInstancedStaticMeshComponent* Component, TArray<FTransform>& InstanceTransforms )
	{
		if ( Component )
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(ApplyPointInstanceTransforms);

			Component->AddInstances( InstanceTransforms, false );
		}
	}

	void SetStaticMesh( UStaticMesh* StaticMesh, UStaticMeshComponent& MeshComponent )
	{
		if ( StaticMesh == MeshComponent.GetStaticMesh() )
		{
			return;
		}

		if ( MeshComponent.IsRegistered() )
		{
			MeshComponent.UnregisterComponent();
		}

		if ( StaticMesh )
		{
			StaticMesh->CreateBodySetup(); // BodySetup is required for HISM component
		}

		MeshComponent.SetStaticMesh( StaticMesh );

		MeshComponent.RegisterComponent();
	}
}

FUsdGeomPointInstancerCreateAssetsTaskChain::FUsdGeomPointInstancerCreateAssetsTaskChain( const TSharedRef< FUsdSchemaTranslationContext >& InContext, const UE::FSdfPath& InPrimPath )
	: FBuildStaticMeshTaskChain( InContext, InPrimPath )
{
	SetupTasks();
}

void FUsdGeomPointInstancerCreateAssetsTaskChain::SetupTasks()
{
	FScopedUnrealAllocs UnrealAllocs;

	// Create mesh description (Async)
	Do( ESchemaTranslationLaunchPolicy::Async,
		[this]() -> bool
		{
			// TODO: Restore support for LOD prototypes
			LODIndexToMeshDescription.Reset( 1 );
			LODIndexToMaterialInfo.Reset( 1 );

			FMeshDescription& AddedMeshDescription = LODIndexToMeshDescription.Emplace_GetRef();
			UsdUtils::FUsdPrimMaterialAssignmentInfo& AssignmentInfo = LODIndexToMaterialInfo.Emplace_GetRef();

			TMap< FString, TMap< FString, int32 > > Unused;
			TMap< FString, TMap< FString, int32 > >* MaterialToPrimvarToUVIndex = Context->MaterialToPrimvarToUVIndex ? Context->MaterialToPrimvarToUVIndex : &Unused;

			pxr::TfToken RenderContextToken = pxr::UsdShadeTokens->universalRenderContext;
			if ( !Context->RenderContext.IsNone() )
			{
				RenderContextToken = UnrealToUsd::ConvertToken( *Context->RenderContext.ToString() ).Get();
			}

			// We want to bake even the top level prim's transform and visibility into the combined mesh, as we'll only apply the instancing
			// transform later
			const bool bSkipRootPrimTransformAndVis = false;

			UsdToUnreal::ConvertGeomMeshHierarchy(
				GetPrim(),
				pxr::UsdTimeCode( Context->Time ),
				Context->PurposesToLoad,
				RenderContextToken,
				*MaterialToPrimvarToUVIndex,
				AddedMeshDescription,
				AssignmentInfo,
				bSkipRootPrimTransformAndVis,
				Context->bMergeIdenticalMaterialSlots
			);

			return !AddedMeshDescription.IsEmpty();
		});

	FBuildStaticMeshTaskChain::SetupTasks();
}

void FUsdGeomPointInstancerTranslator::CreateAssets()
{
	TRACE_CPUPROFILER_EVENT_SCOPE( FUsdGeomPointInstancerTranslator::CreateAssets );

	FScopedUsdAllocs Allocs;

	pxr::UsdPrim Prim = GetPrim();
	pxr::UsdGeomPointInstancer PointInstancer( Prim );

	if ( !PointInstancer )
	{
		return;
	}

	const pxr::UsdRelationship& Prototypes = PointInstancer.GetPrototypesRel();

	pxr::SdfPathVector PrototypePaths;
	if ( !Prototypes.GetTargets( &PrototypePaths ) )
	{
		return;
	}

	for ( const pxr::SdfPath& PrototypePath : PrototypePaths )
	{
		pxr::UsdPrim PrototypeUsdPrim = Prim.GetStage()->GetPrimAtPath( PrototypePath );
		if ( !PrototypeUsdPrim )
		{
			UE_LOG( LogUsd, Warning, TEXT( "Failed to find prototype '%s' for PointInstancer '%s' when collapsing assets" ), *UsdToUnreal::ConvertPath( PrototypePath ), *PrimPath.GetString() );
			continue;
		}

		Context->TranslatorTasks.Add( MakeShared< FUsdGeomPointInstancerCreateAssetsTaskChain >( Context, UE::FSdfPath{ *UsdToUnreal::ConvertPath( PrototypePath ) } ) );
	}
}

USceneComponent* FUsdGeomPointInstancerTranslator::CreateComponents()
{
	TRACE_CPUPROFILER_EVENT_SCOPE( FUsdGeomPointInstancerTranslator::CreateComponents );

	// The plan here is to create an USceneComponent that corresponds to the PointInstancer prim itself, and then spawn a child
	// HISM component for each prototype.
	// We always request a scene component here explicitly or else we'll be upgraded to a static mesh component by the mechanism that
	// handles collapsed Meshes/static mesh components for the GeomXFormable translator.
	USceneComponent* MainSceneComponent = CreateComponentsEx( { USceneComponent::StaticClass() }, {} );
	if ( !MainSceneComponent )
	{
		return nullptr;
	}

	FScopedUsdAllocs UsdAllocs;

	pxr::UsdPrim Prim = GetPrim();
	pxr::UsdGeomPointInstancer PointInstancer( Prim );
	if ( !PointInstancer )
	{
		return MainSceneComponent;
	}

	pxr::SdfPathVector PrototypePaths;
	if ( !PointInstancer.GetPrototypesRel().GetTargets( &PrototypePaths ) )
	{
		return MainSceneComponent;
	}

	UUsdAssetCache& AssetCache = *Context->AssetCache.Get();

	// Lets pretend ParentComponent is pointing to the parent USceneComponent while we create the child HISMs, so they get
	// automatically attached to it as children
	TGuardValue< USceneComponent* > ParentComponentGuard{ Context->ParentComponent, MainSceneComponent };

	TArray<TFuture<TTuple<UHierarchicalInstancedStaticMeshComponent*, TArray<FTransform>>>> Tasks;
	FScopedSlowTask PrototypePathsSlowTask( ( float ) PrototypePaths.size(), LOCTEXT( "GeomPointCreateComponents", "Creating HierarchicalInstancedStaticMeshComponents for point instancers" ) );
	for ( int32 PrototypeIndex = 0; PrototypeIndex < PrototypePaths.size(); ++PrototypeIndex )
	{
		PrototypePathsSlowTask.EnterProgressFrame();

		const pxr::SdfPath& PrototypePath = PrototypePaths[ PrototypeIndex ];
		const FString PrototypePathStr = UsdToUnreal::ConvertPath( PrototypePath );

		pxr::UsdPrim PrototypeUsdPrim = Prim.GetStage()->GetPrimAtPath( PrototypePath );
		if ( !PrototypeUsdPrim )
		{
			UE_LOG( LogUsd, Warning, TEXT( "Failed to find prototype '%s' for PointInstancer '%s' when creating components" ), *PrototypePathStr, *PrimPath.GetString() );
			continue;
		}

		using UHISMComponent = UHierarchicalInstancedStaticMeshComponent;

		FUsdGeomXformableTranslator XformableTranslator{ UHISMComponent::StaticClass(), Context, UE::FUsdTyped( PrototypeUsdPrim ) };

		const bool bNeedsActor = false;
		UHISMComponent* HISMComponent = Cast<UHISMComponent>( XformableTranslator.CreateComponentsEx( { UHISMComponent::StaticClass() }, bNeedsActor ) );
		if ( !HISMComponent )
		{
			UE_LOG( LogUsd, Warning, TEXT( "Failed to generate HISM component for prototype '%s' for PointInstancer '%s'" ), *PrototypePathStr, *PrimPath.GetString() );
			continue;
		}

		UStaticMesh* StaticMesh = Cast< UStaticMesh >( AssetCache.GetAssetForPrim( PrototypePathStr ) );
		UsdGeomPointInstancerTranslatorImpl::SetStaticMesh( StaticMesh, *HISMComponent );

		// Evaluating point instancer can take a long time and is thread-safe. Move to async task while we work on something else.
		pxr::UsdTimeCode TimeCode{ Context->Time };
		Tasks.Emplace(
			Async(
				EAsyncExecution::ThreadPool,
				[ TimeCode, Stage = Prim.GetStage(), PointInstancer, PrototypeIndex, HISMComponent ]()
				{
					TArray<FTransform> InstanceTransforms;
					UsdGeomPointInstancerTranslatorImpl::GetPointInstancerTransforms( Stage, PointInstancer, PrototypeIndex, TimeCode, InstanceTransforms );

					return MakeTuple( HISMComponent, MoveTemp( InstanceTransforms ) );
				}
			)
		);

		// Handle material overrides
		if ( StaticMesh )
		{
#if WITH_EDITOR
			// If the prim paths match, it means that it was this prim that created (and so "owns") the static mesh,
			// so its material assignments will already be directly on the mesh. If they differ, we're using some other prim's mesh,
			// so we may need material overrides on our component
			UUsdAssetImportData* UsdImportData = Cast<UUsdAssetImportData>( StaticMesh->AssetImportData );
			if ( UsdImportData && UsdImportData->PrimPath != PrototypePathStr )
#endif // WITH_EDITOR
			{
				TArray<UMaterialInterface*> ExistingAssignments;
				for ( FStaticMaterial& StaticMaterial : StaticMesh->GetStaticMaterials() )
				{
					ExistingAssignments.Add( StaticMaterial.MaterialInterface );
				}

				MeshTranslationImpl::SetMaterialOverrides(
					PrototypeUsdPrim,
					ExistingAssignments,
					*HISMComponent,
					AssetCache,
					Context->Time,
					Context->ObjectFlags,
					Context->bAllowInterpretingLODs,
					Context->RenderContext
				);
			}
		}
	}

	// Wait on and assign results of the point instancer.
	for ( auto& Future : Tasks )
	{
		TTuple<UHierarchicalInstancedStaticMeshComponent*, TArray<FTransform>> Result{ Future.Get() };
		UsdGeomPointInstancerTranslatorImpl::ApplyPointInstanceTransforms( Result.Key, Result.Value );
	}

	return MainSceneComponent;
}

void FUsdGeomPointInstancerTranslator::UpdateComponents( USceneComponent* PointInstancerRootComponent )
{
	Super::UpdateComponents( PointInstancerRootComponent );
}

#undef LOCTEXT_NAMESPACE

#endif // #if USE_USD_SDK

