// Copyright Epic Games, Inc. All Rights Reserved.

#include "USDGeomXformableTranslator.h"

#include "UnrealUSDWrapper.h"
#include "USDConversionUtils.h"
#include "USDGeomMeshConversion.h"
#include "USDGeomMeshTranslator.h"
#include "USDPrimConversion.h"
#include "USDSchemasModule.h"
#include "USDTypesConversion.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Modules/ModuleManager.h"
#include "StaticMeshAttributes.h"

#if USE_USD_SDK

#include "USDIncludesStart.h"
	#include "pxr/usd/kind/registry.h"
	#include "pxr/usd/usd/modelAPI.h"
	#include "pxr/usd/usd/prim.h"
	#include "pxr/usd/usd/primRange.h"
	#include "pxr/usd/usdGeom/xformable.h"
#include "USDIncludesEnd.h"

namespace UsdGeomXformableTranslatorImpl
{
	FMeshDescription LoadMeshDescription( const pxr::UsdGeomXformable& UsdGeomXformable, const EUsdPurpose PurposesToLoad, const pxr::UsdTimeCode TimeCode )
	{
		TRACE_CPUPROFILER_EVENT_SCOPE( UsdGeomXformableTranslatorImpl::LoadMeshDescription );

		FMeshDescription MeshDescription;
		FStaticMeshAttributes StaticMeshAttributes( MeshDescription );
		StaticMeshAttributes.Register();

		MeshDescription.PolygonGroupAttributes().RegisterAttribute< FName >( TEXT("UsdPrimPath") );

		TFunction< void( FMeshDescription& MeshDescription, const pxr::UsdPrim& ParentPrim, const FTransform CurrentTransform, const pxr::UsdTimeCode& TimeCode ) > RecursiveChildCollapsing;
		RecursiveChildCollapsing = [ &RecursiveChildCollapsing, PurposesToLoad ]( FMeshDescription& MeshDescription, const pxr::UsdPrim& ParentPrim, const FTransform CurrentTransform, const pxr::UsdTimeCode& TimeCode )
		{
			for ( const pxr::UsdPrim& ChildPrim : ParentPrim.GetFilteredChildren( pxr::UsdTraverseInstanceProxies() ) )
			{
				// Ignore meshes from disabled purposes
				if ( !EnumHasAllFlags( PurposesToLoad, IUsdPrim::GetPurpose( ChildPrim ) ) )
				{
					continue;
				}

				// Ignore invisible prims
				if ( pxr::UsdGeomImageable UsdGeomImageable = pxr::UsdGeomImageable( ChildPrim ) )
				{
					if ( UsdGeomImageable.ComputeVisibility() == pxr::UsdGeomTokens->invisible )
					{
						continue;
					}
				}

				FTransform ChildTransform = CurrentTransform;

				if ( pxr::UsdGeomXformable ChildXformable = pxr::UsdGeomXformable( ChildPrim ) )
				{
					FTransform LocalChildTransform;
					UsdToUnreal::ConvertXformable( ChildPrim.GetStage(), ChildXformable, LocalChildTransform, TimeCode );

					ChildTransform = LocalChildTransform * CurrentTransform;
				}

				if ( pxr::UsdGeomMesh ChildMesh = pxr::UsdGeomMesh( ChildPrim ) )
				{
					UsdToUnreal::ConvertGeomMesh( ChildMesh, MeshDescription, ChildTransform, TimeCode );
				}

				RecursiveChildCollapsing( MeshDescription, ChildPrim, ChildTransform, TimeCode );
			}
		};

		// Collapse the children
		RecursiveChildCollapsing( MeshDescription, UsdGeomXformable.GetPrim(), FTransform::Identity, TimeCode );

		return MeshDescription;
	}
}

class FUsdGeomXformableCreateAssetsTaskChain : public FBuildStaticMeshTaskChain
{
public:
	explicit FUsdGeomXformableCreateAssetsTaskChain( const TSharedRef< FUsdSchemaTranslationContext >& InContext, const TUsdStore< pxr::UsdGeomXformable >& InGeomXformable )
		: FBuildStaticMeshTaskChain( InContext, TUsdStore< pxr::UsdTyped >( InGeomXformable.Get() ) )
	{
		SetupTasks();
	}

protected:
	virtual void SetupTasks() override;
};

void FUsdGeomXformableCreateAssetsTaskChain::SetupTasks()
{
	FScopedUnrealAllocs UnrealAllocs;

	// Create mesh description (Async)
	constexpr bool bIsAsyncTask = true;
	Do( bIsAsyncTask,
		[ this ]() -> bool
		{
			MeshDescription = UsdGeomXformableTranslatorImpl::LoadMeshDescription( pxr::UsdGeomXformable( Schema.Get() ), Context->PurposesToLoad, pxr::UsdTimeCode( Context->Time ) );

			return !MeshDescription.IsEmpty();
		} );

	FBuildStaticMeshTaskChain::SetupTasks();
}

void FUsdGeomXformableTranslator::CreateAssets()
{
	if ( !CollapsesChildren( ECollapsingType::Assets ) )
	{
		// We only have to create assets when our children are collapsed together
		return;
	}

	TRACE_CPUPROFILER_EVENT_SCOPE( FUsdGeomMeshTranslator::CreateAssets );

	Context->TranslatorTasks.Add( MakeShared< FUsdGeomXformableCreateAssetsTaskChain >( Context, pxr::UsdGeomXformable( Schema.Get() ) ) );
}

FUsdGeomXformableTranslator::FUsdGeomXformableTranslator( TSubclassOf< USceneComponent > InComponentTypeOverride, TSharedRef< FUsdSchemaTranslationContext > InContext, const pxr::UsdTyped& InSchema )
	: FUsdSchemaTranslator( InContext, InSchema )
	, ComponentTypeOverride( InComponentTypeOverride )
{
}

USceneComponent* FUsdGeomXformableTranslator::CreateComponents()
{
	return CreateComponentsEx( {}, {} );
}

USceneComponent* FUsdGeomXformableTranslator::CreateComponentsEx( TOptional< TSubclassOf< USceneComponent > > ComponentType, TOptional< bool > bNeedsActor )
{
	if ( !Context->IsValid() )
	{
		return nullptr;
	}

	TUsdStore< pxr::UsdPrim > Prim = Schema.Get().GetPrim();

	if ( !Prim.Get() )
	{
		return nullptr;
	}

	FScopedUnrealAllocs UnrealAllocs;

	if ( !bNeedsActor.IsSet() )
	{
		// We should only add components to our transient/instanced actors, never to the AUsdStageActor or any other permanent actor
		bool bOwnerActorIsPersistent = false;
		if ( USceneComponent* RootComponent = Context->ParentComponent )
		{
			if ( AActor* Actor = RootComponent->GetOwner() )
			{
				bOwnerActorIsPersistent = !Actor->HasAnyFlags(RF_Transient);
			}
		}

		bNeedsActor =
		(
			bOwnerActorIsPersistent ||
			Context->ParentComponent == nullptr ||
			Prim.Get().IsPseudoRoot() ||
			Prim.Get().IsModel() ||
			Prim.Get().IsGroup() ||
			UsdUtils::HasCompositionArcs( Prim.Get() )
		);
	}

	USceneComponent* SceneComponent = nullptr;
	UObject* ComponentOuter = nullptr;

	if ( bNeedsActor.GetValue() )
	{
		// Spawn actor
		FActorSpawnParameters SpawnParameters;
		SpawnParameters.ObjectFlags = Context->ObjectFlags;
		SpawnParameters.OverrideLevel =  Context->Level;

		UClass* ActorClass = UsdUtils::GetActorTypeForPrim( Prim.Get() );
		AActor* SpawnedActor = Context->Level->GetWorld()->SpawnActor( ActorClass, nullptr, SpawnParameters );

		if ( SpawnedActor )
		{
			SpawnedActor->SetActorLabel( Prim.Get().GetName().GetText() );
			SpawnedActor->Tags.AddUnique( TEXT("SequencerActor") );	// Hack to show transient actors in world outliner

			SceneComponent = SpawnedActor->GetRootComponent();

			ComponentOuter = SpawnedActor;
		}
	}
	else
	{
		ComponentOuter = Context->ParentComponent;
	}

	if ( !SceneComponent )
	{
		if ( !ComponentType.IsSet() )
		{
			if ( ComponentTypeOverride.IsSet() )
			{
				ComponentType = ComponentTypeOverride.GetValue();
			}
			else
			{
				ComponentType = UsdUtils::GetComponentTypeForPrim( Schema.Get().GetPrim() );

				if ( CollapsesChildren( ECollapsingType::Assets ) )
				{
					if ( UStaticMesh* PrimStaticMesh = Cast< UStaticMesh >( Context->PrimPathsToAssets.FindRef( UsdToUnreal::ConvertPath( Schema.Get().GetPath() ) ) ) )
					{
						// At this time, we only support collapsing static meshes together
						ComponentType = UStaticMeshComponent::StaticClass();
					}
				}
			}
		}

		if ( ComponentType.IsSet() && ComponentType.GetValue() != nullptr )
		{
			SceneComponent = NewObject< USceneComponent >( ComponentOuter, ComponentType.GetValue(), FName( Prim.Get().GetName().GetText() ), Context->ObjectFlags );
		
			if ( AActor* Owner = SceneComponent->GetOwner() )
			{
				Owner->AddInstanceComponent( SceneComponent );
			}
		}
	}

	if ( SceneComponent )
	{
		if ( !SceneComponent->GetOwner()->GetRootComponent() )
		{
			SceneComponent->GetOwner()->SetRootComponent( SceneComponent );
		}

		UpdateComponents( SceneComponent );

		// Don't call SetMobility as it would trigger a reregister, queuing unnecessary rhi commands since this is a brand new component
		if ( Context->ParentComponent && Context->ParentComponent->Mobility == EComponentMobility::Movable )
		{
			SceneComponent->Mobility = EComponentMobility::Movable;
		}
		else
		{
			SceneComponent->Mobility = UsdUtils::IsAnimated( Prim.Get() ) ? EComponentMobility::Movable : EComponentMobility::Static;
		}

		// Attach to parent
		SceneComponent->AttachToComponent( Context->ParentComponent, FAttachmentTransformRules::KeepRelativeTransform );

		if ( !SceneComponent->IsRegistered() )
		{
			SceneComponent->RegisterComponent();
		}
	}

	return SceneComponent;
}

void FUsdGeomXformableTranslator::UpdateComponents( USceneComponent* SceneComponent )
{
	if ( SceneComponent )
	{
		UsdToUnreal::ConvertXformable( Schema.Get().GetPrim().GetStage(), pxr::UsdGeomXformable( Schema.Get() ), *SceneComponent, pxr::UsdTimeCode( Context->Time ) );

		// If the user modified a mesh parameter (e.g. vertex color), the hash will be different and it will become a separate asset
		// so we must check for this and assign the new StaticMesh
		if ( UStaticMeshComponent* StaticMeshComponent = Cast< UStaticMeshComponent >( SceneComponent ) )
		{
			UStaticMesh* PrimStaticMesh = Cast< UStaticMesh >( Context->PrimPathsToAssets.FindRef( UsdToUnreal::ConvertPath( Schema.Get().GetPath() ) ) );

			if ( PrimStaticMesh != StaticMeshComponent->GetStaticMesh() )
			{
				// Need to make sure the mesh's resources are initialized here as it may have just been built in another thread
				// Only do this if required though, as this mesh could using these resources currently (e.g. PIE and editor world sharing the mesh)
				if ( PrimStaticMesh && !PrimStaticMesh->AreRenderingResourcesInitialized() )
				{
					PrimStaticMesh->InitResources();
				}

				if ( StaticMeshComponent->IsRegistered() )
				{
					StaticMeshComponent->UnregisterComponent();
				}

				StaticMeshComponent->SetStaticMesh( PrimStaticMesh );

				StaticMeshComponent->RegisterComponent();
			}
		}
	}
}

bool FUsdGeomXformableTranslator::CollapsesChildren( ECollapsingType CollapsingType ) const
{
	bool bCollapsesChildren = false;

	FScopedUsdAllocs UsdAllocs;

	pxr::UsdModelAPI Model( Schema.Get() );

	if ( Model )
	{
		bCollapsesChildren = Model.IsKind( pxr::KindTokens->component );

		if ( !bCollapsesChildren )
		{
			// Temp support for the prop kind
			bCollapsesChildren = Model.IsKind( pxr::TfToken( "prop" ), pxr::UsdModelAPI::KindValidationNone );
		}

		if ( bCollapsesChildren )
		{
			IUsdSchemasModule& UsdSchemasModule = FModuleManager::Get().LoadModuleChecked< IUsdSchemasModule >( TEXT("USDSchemas") );

			TArray< TUsdStore< pxr::UsdPrim > > ChildXformPrims = UsdUtils::GetAllPrimsOfType( Schema.Get().GetPrim(), pxr::TfType::Find< pxr::UsdGeomXformable >() );

			for ( const TUsdStore< pxr::UsdPrim >& ChildXformPrim : ChildXformPrims )
			{
				if ( TSharedPtr< FUsdSchemaTranslator > SchemaTranslator = UsdSchemasModule.GetTranslatorRegistry().CreateTranslatorForSchema( Context, pxr::UsdTyped( ChildXformPrim.Get() ) ) )
				{
					if ( !SchemaTranslator->CanBeCollapsed( CollapsingType ) )
					{
						return false;
					}
				}
			}
		}
	}

	if ( bCollapsesChildren )
	{
		TArray< TUsdStore< pxr::UsdPrim > > ChildGeomMeshes = UsdUtils::GetAllPrimsOfType( Schema.Get().GetPrim(), pxr::TfType::Find< pxr::UsdGeomMesh >() );

		// We only support collapsing GeomMeshes for now and we only want to do it when there are multiple meshes as the resulting mesh is considered unique
		if ( ChildGeomMeshes.Num() < 2 )
		{
			bCollapsesChildren = false;
		}
		else
		{
			const int32 MaxVertices = 500000;
			int32 NumVertices = 0;

			for ( const TUsdStore< pxr::UsdPrim >& ChildPrim : ChildGeomMeshes )
			{
				pxr::UsdGeomMesh ChildGeomMesh( ChildPrim.Get() );

				if ( pxr::UsdAttribute Points = ChildGeomMesh.GetPointsAttr() )
				{
					pxr::VtArray< pxr::GfVec3f > PointsArray;
					Points.Get( &PointsArray, pxr::UsdTimeCode( Context->Time ) );

					NumVertices += PointsArray.size();

					if ( NumVertices > MaxVertices )
					{
						bCollapsesChildren = false;
						break;
					}
				}
			}
		}
	}

	return bCollapsesChildren;
}

#endif // #if USE_USD_SDK
