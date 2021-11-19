// Copyright Epic Games, Inc. All Rights Reserved.

#include "USDGeomPointInstancerTranslator.h"

#include "MeshTranslationImpl.h"
#include "USDAssetCache.h"
#include "USDAssetImportData.h"
#include "USDConversionUtils.h"
#include "USDLog.h"
#include "USDSchemasModule.h"
#include "USDTypesConversion.h"

#include "UsdWrappers/UsdTyped.h"

#if WITH_EDITOR
#include "Editor.h"
#endif // WITH_EDITOR

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

	// The components that we will spawn in here will not have prim twins, and so would otherwise never be destroyed.
	// Here we make sure we get rid of the old ones because we will create new ones right afterwards
	PointInstancerRootComponent->Modify();
	TArray<USceneComponent*> Children;
	const bool bIncludeAllDescendants = true;
	PointInstancerRootComponent->GetChildrenComponents( bIncludeAllDescendants, Children );
	for ( USceneComponent* Child : Children )
	{
		// Attempt at making sure it's one of our components just in case the user happened to put something in there
		if ( !Child->ComponentTags.Contains( UnrealIdentifiers::Invisible) &&
			 !Child->ComponentTags.Contains( UnrealIdentifiers::Inherited) )
		{
			continue;
		}

		Child->Modify();

		const bool bPromoteChildren = true;
		Child->DestroyComponent( bPromoteChildren );
	}

#if WITH_EDITOR
	// If we have this actor selected we need to refresh the details panel manually on the next frame because
	// deleting those components will leave it in some type of invalid state
	if ( PointInstancerRootComponent->GetOwner()->IsSelected() )
	{
		GEditor->GetTimerManager()->SetTimerForNextTick( []()
		{
			GEditor->NoteSelectionChange();
		} );
	}
#endif // WITH_EDITOR

	// We have to manage the visibilities of the components that we spawn ourselves, because they will not have prim twins
	// and so will not be handled by the stage actor. Plus there are some weird rules involved, like how we don't care about the
	// visibilities of *parents* of prototype prims, only theirs and down from there
	const bool bPointInstancerVisible = UsdUtils::IsVisible( UE::FUsdPrim{ Prim }, Context->Time );

	// For each prototype
	const pxr::UsdRelationship& Prototypes = PointInstancer.GetPrototypesRel();
	pxr::SdfPathVector PrototypesPaths;

	TSet< FString > ProcessedPrims;

	if ( Prototypes.GetTargets( &PrototypesPaths ) )
	{
		TGuardValue< USceneComponent* > ParentComponentGuard( Context->ParentComponent, PointInstancerRootComponent );

		for ( int32 PrototypeIndex = 0; PrototypeIndex < PrototypesPaths.size(); ++PrototypeIndex)
		{
			pxr::UsdPrim PrototypeUsdPrim = Prim.GetStage()->GetPrimAtPath( PrototypesPaths[PrototypeIndex] );
			if ( !PrototypeUsdPrim )
			{
				UE_LOG( LogUsd, Warning, TEXT( "Failed to find prototype '%s' for PointInstancer '%s'" ), *UsdToUnreal::ConvertPath( PrototypesPaths[ PrototypeIndex ] ), *PrimPath.GetString() );
				continue;
			}
			UE::FUsdPrim PrototypePrim{ PrototypeUsdPrim };

			USceneComponent* PrototypeParentComponent = Context->ParentComponent;

			bool bPrototypeRootVisible = true;

			// Temp fix to prevent us from creating yet another UStaticMeshComponent as a parent prim to the HISM component
			const bool bNeedsActor = false;
			TOptional<TGuardValue< USceneComponent* >> ParentComponentGuard2;
			if ( !PrototypeUsdPrim.IsA<pxr::UsdGeomMesh>() )
			{
				FUsdGeomXformableTranslator PrototypeXformTranslator( Context, UE::FUsdTyped( PrototypeUsdPrim ) );

				if ( USceneComponent* PrototypeXformComponent = PrototypeXformTranslator.CreateComponentsEx( {}, bNeedsActor ) )
				{
					// We have to be careful with prototype visibilities and can't defer to just using ComputeVisibility() because sometimes source apps
					// can mark the prototype parent prim as invisible, as an attempt to prevent them from being parsed directly as scene meshes.
					// We will handle the visibility of all the components we spawn manually
					bPrototypeRootVisible = UsdUtils::HasInheritedVisibility( PrototypePrim, Context->Time );
					PrototypeXformComponent->SetHiddenInGame( !bPointInstancerVisible || !bPrototypeRootVisible );

					PrototypeParentComponent = PrototypeXformComponent;
				}

				ParentComponentGuard2.Emplace(Context->ParentComponent, PrototypeParentComponent );
			}

			TArray< TUsdStore< pxr::UsdPrim > > ChildGeomMeshPrims = UsdUtils::GetAllPrimsOfType( PrototypeUsdPrim, pxr::TfType::Find< pxr::UsdGeomMesh >() );

			for ( const TUsdStore< pxr::UsdPrim >& PrototypeGeomMeshPrim : ChildGeomMeshPrims )
			{
				TUsdStore< pxr::SdfPath > PrototypeTargetPrimPath = UsdGeomPointInstancerTranslatorImpl::UnwindToNonCollapsedPrim( FUsdSchemaTranslator::ECollapsingType::Assets, PrototypeGeomMeshPrim, Context );

				const FString UEPrototypeTargetPrimPath = UsdToUnreal::ConvertPath( PrototypeTargetPrimPath.Get() );

				if ( !ProcessedPrims.Contains( UEPrototypeTargetPrimPath ) )
				{
					ProcessedPrims.Add( UEPrototypeTargetPrimPath );

					if ( pxr::UsdPrim PrototypeTargetUsdPrim = Prim.GetStage()->GetPrimAtPath( PrototypeTargetPrimPath.Get() ) )
					{
						pxr::UsdGeomMesh PrototypeGeomMesh( PrototypeTargetUsdPrim );
						FUsdGeomXformableTranslator PrototypeGeomXformableTranslator( UHierarchicalInstancedStaticMeshComponent::StaticClass(), Context, UE::FUsdTyped( PrototypeGeomMesh ) );

						USceneComponent* UsdGeomPrimComponent = PrototypeGeomXformableTranslator.CreateComponentsEx( {}, bNeedsActor );

						if ( UHierarchicalInstancedStaticMeshComponent* HismComponent = Cast< UHierarchicalInstancedStaticMeshComponent >( UsdGeomPrimComponent ) )
						{
							UE::FUsdPrim PrototypeTargetPrim{ PrototypeTargetUsdPrim };

							// Traverse up every time because we have no idea how far deep each UsdGeomMesh prim is
							const bool bHasInvisibleParent = UsdUtils::HasInvisibleParent( PrototypeTargetPrim, PrototypePrim, Context->Time );
							const bool bPrototypeMeshVisible = UsdUtils::HasInheritedVisibility( PrototypeTargetPrim, Context->Time );
							HismComponent->SetHiddenInGame( !bPointInstancerVisible || !bPrototypeRootVisible || !bPrototypeMeshVisible || bHasInvisibleParent );

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
										PrototypeTargetUsdPrim,
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

