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
	TSubclassOf< USceneComponent > ComponentType;

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

	USceneComponent* RootComponent = CreateComponents( ComponentType );

	return RootComponent;
}

USceneComponent* FUsdGeomXformableTranslator::CreateComponents( TSubclassOf< USceneComponent > ComponentType )
{
	TUsdStore< pxr::UsdPrim > Prim = Schema.Get().GetPrim();

	// We should only add components to our transient/instanced actors, never to the AUsdStageActor or any other permanent actor
	bool bOwnerActorIsPersistent = false;
	if (USceneComponent* RootComponent = Context->ParentComponent)
	{
		if (AActor* Actor = RootComponent->GetOwner())
		{
			bOwnerActorIsPersistent = !Actor->HasAnyFlags(RF_Transient);
		}
	}

	const bool bNeedsActor =
		(
			bOwnerActorIsPersistent ||
			Context->ParentComponent == nullptr ||
			Prim.Get().IsPseudoRoot() ||
			Prim.Get().IsModel() ||
			Prim.Get().IsGroup() ||
			UsdUtils::HasCompositionArcs( Prim.Get() )
		);

	return CreateComponents( ComponentType, bNeedsActor );
}

USceneComponent* FUsdGeomXformableTranslator::CreateComponents( TSubclassOf< USceneComponent > ComponentType, const bool bNeedsActor )
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

	USceneComponent* SceneComponent = nullptr;
	UObject* ComponentOuter = nullptr;

	if ( bNeedsActor )
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
		SceneComponent = NewObject< USceneComponent >( ComponentOuter, ComponentType, FName( Prim.Get().GetName().GetText() ), Context->ObjectFlags );

		if ( AActor* Owner = SceneComponent->GetOwner() )
		{
			Owner->AddInstanceComponent( SceneComponent );
		}
	}

	if ( SceneComponent )
	{
		UpdateComponents( SceneComponent );

		if ( Context->ParentComponent && Context->ParentComponent->Mobility == EComponentMobility::Movable )
		{
			SceneComponent->SetMobility( EComponentMobility::Movable );
		}
		else
		{
			SceneComponent->SetMobility( UsdUtils::IsAnimated( Prim.Get() ) ? EComponentMobility::Movable : EComponentMobility::Static ); /*PrimsToAnimate.Contains( UsdToUnreal::ConvertPath( Path.GetPrimPath() ) */
		}

		// Attach to parent
		SceneComponent->AttachToComponent( Context->ParentComponent, FAttachmentTransformRules::KeepRelativeTransform );

		if ( !SceneComponent->GetOwner()->GetRootComponent() )
		{
			SceneComponent->GetOwner()->SetRootComponent( SceneComponent );
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
				if ( PrimStaticMesh )
				{
					// Need to make sure the mesh's resources are initialized here as it may have just been built in another thread
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

		// Set purpose as a tag so its visibility can be toggled later
		TUsdStore< pxr::UsdPrim > Prim = Schema.Get().GetPrim();
		SceneComponent->ComponentTags.RemoveAll([&](const FName& Tag){ return Tag.ToString().EndsWith(TEXT("Purpose")); });
		SceneComponent->ComponentTags.Add(IUsdPrim::GetPurposeName(IUsdPrim::GetPurpose(Prim.Get())));
	}
}

bool FUsdGeomXformableTranslator::CollapsesChildren( ECollapsingType CollapsingType ) const
{
	bool bIsCollapsableKind = false;

	FScopedUsdAllocs UsdAllocs;

	pxr::UsdModelAPI Model( Schema.Get() );

	if ( Model )
	{
		bIsCollapsableKind = Model.IsKind( pxr::KindTokens->component );

		if ( !bIsCollapsableKind )
		{
			// Temp support for the prop kind
			bIsCollapsableKind = Model.IsKind( pxr::TfToken( "prop" ), pxr::UsdModelAPI::KindValidationNone );
		}

		if ( bIsCollapsableKind )
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

	return bIsCollapsableKind;
}

#endif // #if USE_USD_SDK
