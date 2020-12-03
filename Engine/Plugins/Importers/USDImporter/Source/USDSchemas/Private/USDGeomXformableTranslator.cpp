// Copyright Epic Games, Inc. All Rights Reserved.

#include "USDGeomXformableTranslator.h"

#include "UnrealUSDWrapper.h"
#include "USDConversionUtils.h"
#include "USDErrorUtils.h"
#include "USDGeomMeshConversion.h"
#include "USDGeomMeshTranslator.h"
#include "USDLog.h"
#include "USDMemory.h"
#include "USDPrimConversion.h"
#include "USDSchemasModule.h"
#include "USDTypesConversion.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/RendererSettings.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Modules/ModuleManager.h"
#include "StaticMeshAttributes.h"
#include "Widgets/Notifications/SNotificationList.h"

#include "UsdWrappers/SdfPath.h"
#include "UsdWrappers/UsdGeomXformable.h"
#include "UsdWrappers/UsdPrim.h"
#include "UsdWrappers/UsdStage.h"

#if USE_USD_SDK

#include "USDIncludesStart.h"
	#include "pxr/usd/kind/registry.h"
	#include "pxr/usd/usd/modelAPI.h"
	#include "pxr/usd/usd/prim.h"
	#include "pxr/usd/usd/primRange.h"
	#include "pxr/usd/usd/variantSets.h"
	#include "pxr/usd/usdGeom/mesh.h"
	#include "pxr/usd/usdGeom/subset.h"
	#include "pxr/usd/usdGeom/xformable.h"
	#include "pxr/usd/usdShade/materialBindingAPI.h"
#include "USDIncludesEnd.h"

namespace UsdGeomXformableTranslatorImpl
{
	void LoadMeshDescription( const pxr::UsdGeomXformable& UsdGeomXformable, const EUsdPurpose PurposesToLoad, const TMap< FString, TMap<FString, int32> >& MaterialToPrimvarToUVIndex, const pxr::UsdTimeCode TimeCode, FMeshDescription& OutMeshdescription, UsdUtils::FUsdPrimMaterialAssignmentInfo& OutMaterialAssignments )
	{
		TRACE_CPUPROFILER_EVENT_SCOPE( UsdGeomXformableTranslatorImpl::LoadMeshDescription );

		FStaticMeshAttributes StaticMeshAttributes( OutMeshdescription );
		StaticMeshAttributes.Register();

		TFunction< void( FMeshDescription&, UsdUtils::FUsdPrimMaterialAssignmentInfo&, const pxr::UsdPrim&, const FTransform, const pxr::UsdTimeCode& ) > RecursiveChildCollapsing;
		RecursiveChildCollapsing = [ &RecursiveChildCollapsing, &MaterialToPrimvarToUVIndex, PurposesToLoad ]( FMeshDescription& MeshDescription, UsdUtils::FUsdPrimMaterialAssignmentInfo& MaterialAssignments, const pxr::UsdPrim& ParentPrim, const FTransform CurrentTransform, const pxr::UsdTimeCode& TimeCode )
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
					UsdToUnreal::ConvertXformable( ChildPrim.GetStage(), ChildXformable, LocalChildTransform, TimeCode.GetValue() );

					ChildTransform = LocalChildTransform * CurrentTransform;
				}

				if ( pxr::UsdGeomMesh ChildMesh = pxr::UsdGeomMesh( ChildPrim ) )
				{
					UsdToUnreal::ConvertGeomMesh( ChildMesh, MeshDescription, MaterialAssignments, ChildTransform, MaterialToPrimvarToUVIndex, TimeCode );
				}

				RecursiveChildCollapsing( MeshDescription, MaterialAssignments, ChildPrim, ChildTransform, TimeCode );
			}
		};

		// Collapse the children
		RecursiveChildCollapsing( OutMeshdescription, OutMaterialAssignments, UsdGeomXformable.GetPrim(), FTransform::Identity, TimeCode );
	}
}

class FUsdGeomXformableCreateAssetsTaskChain : public FBuildStaticMeshTaskChain
{
public:
	explicit FUsdGeomXformableCreateAssetsTaskChain( const TSharedRef< FUsdSchemaTranslationContext >& InContext, const UE::FSdfPath& InPrimPath )
		: FBuildStaticMeshTaskChain( InContext, InPrimPath )
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
	Do( ESchemaTranslationLaunchPolicy::Async,
		[ this ]() -> bool
		{
			// We will never have multiple LODs of meshes that were collapsed together, as LOD'd meshes don't collapse. So just parse the mesh we get as LOD0
			LODIndexToMeshDescription.Reset(1);
			LODIndexToMaterialInfo.Reset(1);

			FMeshDescription& AddedMeshDescription = LODIndexToMeshDescription.Emplace_GetRef();
			UsdUtils::FUsdPrimMaterialAssignmentInfo& AssignmentInfo = LODIndexToMaterialInfo.Emplace_GetRef();

			TMap< FString, TMap< FString, int32 > > Unused;
			TMap< FString, TMap< FString, int32 > >* MaterialToPrimvarToUVIndex = Context->MaterialToPrimvarToUVIndex ? Context->MaterialToPrimvarToUVIndex : &Unused;

			UsdGeomXformableTranslatorImpl::LoadMeshDescription(
				pxr::UsdGeomXformable( GetPrim() ),
				Context->PurposesToLoad,
				*MaterialToPrimvarToUVIndex,
				pxr::UsdTimeCode( Context->Time ),
				AddedMeshDescription,
				AssignmentInfo
			);

			return !AddedMeshDescription.IsEmpty();
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

	Context->TranslatorTasks.Add( MakeShared< FUsdGeomXformableCreateAssetsTaskChain >( Context, PrimPath ) );
}

FUsdGeomXformableTranslator::FUsdGeomXformableTranslator( TSubclassOf< USceneComponent > InComponentTypeOverride, TSharedRef< FUsdSchemaTranslationContext > InContext, const UE::FUsdTyped& InSchema )
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

	UE::FUsdPrim Prim = GetPrim();
	if ( !Prim )
	{
		return nullptr;
	}

	FScopedUnrealAllocs UnrealAllocs;

	if ( !bNeedsActor.IsSet() )
	{
		// Don't add components to the AUsdStageActor or the USDStageImport 'scene actor'
		UE::FUsdPrim ParentPrim = Prim.GetParent();
		bool bIsTopLevelPrim = ParentPrim.IsValid() && ParentPrim.IsPseudoRoot();

		// If we don't have any parent prim with a type that generates a component, we are still technically a top-level prim
		if ( !bIsTopLevelPrim )
		{
			bool bHasParentComponent = false;
			while ( ParentPrim.IsValid() )
			{
				if ( UE::FUsdGeomXformable( ParentPrim ) )
				{
					bHasParentComponent = true;
					break;
				}

				ParentPrim = ParentPrim.GetParent();
			}
			if ( !bHasParentComponent )
			{
				bIsTopLevelPrim = true;
			}
		}

		auto PrimNeedsActor = []( const UE::FUsdPrim& UsdPrim ) -> bool
		{
			return  UsdPrim.IsPseudoRoot() ||
					UsdPrim.IsModel() ||
					UsdPrim.IsGroup() ||
					UsdUtils::HasCompositionArcs( UsdPrim );
		};

		bNeedsActor =
		(
			bIsTopLevelPrim ||
			Context->ParentComponent == nullptr ||
			PrimNeedsActor( Prim )
		);

		// We don't want to start a component hierarchy if one of our child will break it by being an actor
		if ( !bNeedsActor.GetValue() )
		{
			const bool bTraverseInstanceProxies= true;
			for ( const pxr::UsdPrim& Child : Prim.GetFilteredChildren( bTraverseInstanceProxies ) )
			{
				if ( PrimNeedsActor( UE::FUsdPrim( Child ) ) )
				{
					bNeedsActor = true;
					break;
				}
			}
		}
	}

	USceneComponent* SceneComponent = nullptr;
	UObject* ComponentOuter = nullptr;

	if ( bNeedsActor.GetValue() )
	{
		// Spawn actor
		FActorSpawnParameters SpawnParameters;
		SpawnParameters.ObjectFlags = Context->ObjectFlags;
		SpawnParameters.OverrideLevel =  Context->Level;

		UClass* ActorClass = UsdUtils::GetActorTypeForPrim( Prim );
		AActor* SpawnedActor = Context->Level->GetWorld()->SpawnActor( ActorClass, nullptr, SpawnParameters );

		if ( SpawnedActor )
		{
#if WITH_EDITOR
			SpawnedActor->SetActorLabel( Prim.GetName().ToString() );
#endif // WITH_EDITOR

			// Hack to show transient actors in world outliner
			if (SpawnedActor->HasAnyFlags(EObjectFlags::RF_Transient))
			{
				SpawnedActor->Tags.AddUnique( TEXT("SequencerActor") );
			}

			SceneComponent = SpawnedActor->GetRootComponent();

			ComponentOuter = SpawnedActor;
		}
	}
	else
	{
		ComponentOuter = Context->ParentComponent;
	}

	if ( !ComponentOuter )
	{
		UE_LOG( LogUsd, Warning, TEXT("Invalid outer when trying to create SceneComponent for prim (%s)"), *PrimPath.GetString() );
		return nullptr;
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
				ComponentType = UsdUtils::GetComponentTypeForPrim( Prim );

				if ( CollapsesChildren( ECollapsingType::Assets ) )
				{
					if ( UStaticMesh* PrimStaticMesh = Cast< UStaticMesh >( Context->PrimPathsToAssets.FindRef( PrimPath.GetString() ) ) )
					{
						// At this time, we only support collapsing static meshes together
						ComponentType = UStaticMeshComponent::StaticClass();
					}
				}
			}
		}

		if ( ComponentType.IsSet() && ComponentType.GetValue() != nullptr )
		{
			const FName ComponentName = MakeUniqueObjectName( ComponentOuter, ComponentType.GetValue(), FName( Prim.GetName() ) );
			SceneComponent = NewObject< USceneComponent >( ComponentOuter, ComponentType.GetValue(), ComponentName, Context->ObjectFlags );

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

		// Don't call SetMobility as it would trigger a reregister, queuing unnecessary rhi commands since this is a brand new component
		if ( Context->ParentComponent && Context->ParentComponent->Mobility == EComponentMobility::Movable )
		{
			SceneComponent->Mobility = EComponentMobility::Movable;
		}
		else
		{
			SceneComponent->Mobility = UsdUtils::IsAnimated( Prim ) ? EComponentMobility::Movable : EComponentMobility::Static;
		}

		UpdateComponents( SceneComponent );

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
		UsdToUnreal::ConvertXformable( Context->Stage, pxr::UsdGeomXformable( GetPrim() ), *SceneComponent, Context->Time );

		// If the user modified a mesh parameter (e.g. vertex color), the hash will be different and it will become a separate asset
		// so we must check for this and assign the new StaticMesh
		if ( UStaticMeshComponent* StaticMeshComponent = Cast< UStaticMeshComponent >( SceneComponent ) )
		{
			UStaticMesh* PrimStaticMesh = Cast< UStaticMesh >( Context->PrimPathsToAssets.FindRef( PrimPath.GetString() ) );

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
	if ( !Context->bAllowCollapsing )
	{
		return false;
	}

	bool bCollapsesChildren = false;

	FScopedUsdAllocs UsdAllocs;

	pxr::UsdPrim Prim = GetPrim();
	pxr::UsdModelAPI Model{ pxr::UsdTyped( Prim ) };

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

			TArray< TUsdStore< pxr::UsdPrim > > ChildXformPrims = UsdUtils::GetAllPrimsOfType( Prim, pxr::TfType::Find< pxr::UsdGeomXformable >() );

			for ( const TUsdStore< pxr::UsdPrim >& ChildXformPrim : ChildXformPrims )
			{
				if ( TSharedPtr< FUsdSchemaTranslator > SchemaTranslator = UsdSchemasModule.GetTranslatorRegistry().CreateTranslatorForSchema( Context, UE::FUsdTyped( ChildXformPrim.Get() ) ) )
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
		TArray< TUsdStore< pxr::UsdPrim > > ChildGeomMeshes = UsdUtils::GetAllPrimsOfType( Prim, pxr::TfType::Find< pxr::UsdGeomMesh >() );

		// We only support collapsing GeomMeshes for now and we only want to do it when there are multiple meshes as the resulting mesh is considered unique
		if ( ChildGeomMeshes.Num() < 2 )
		{
			bCollapsesChildren = false;
		}
		else
		{
			const bool bUsesRaytracing = GetDefault<URendererSettings>()->bEnableRayTracing;

			const int32 MaxVertices = 500000;
			int32 NumMaxExpectedMaterialSlots = 0;
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

				if ( bUsesRaytracing )
				{
					// We can't generate a mesh with more than 255 material slots as raytracing expects the material index to be uint8
					std::vector<pxr::UsdGeomSubset> GeomSubsets = pxr::UsdShadeMaterialBindingAPI( ChildPrim.Get() ).GetMaterialBindSubsets();
					NumMaxExpectedMaterialSlots += FMath::Max<int32>(1, GeomSubsets.size() + 1); // +1 because we may create an additional slot if it's not properly partitioned
					if ( NumMaxExpectedMaterialSlots > 255 )
					{
						static bool bShowedRaytracingWarning = false;
						if ( !bShowedRaytracingWarning )
						{
							FNotificationInfo ErrorToast( NSLOCTEXT( "USDXformableTranslator", "USDRaytracingToast", "USD mesh collapsing will be capped.\nSee the Output Log for details." ) );
							ErrorToast.ExpireDuration = 5.0f;
							ErrorToast.bFireAndForget = true;
							ErrorToast.Image = FCoreStyle::Get().GetBrush( TEXT( "MessageLog.Warning" ) );
							FSlateNotificationManager::Get().AddNotification( ErrorToast );

							FUsdLogManager::LogMessage( EMessageSeverity::Warning, NSLOCTEXT("USDXformableTranslator", "USDRaytracingWarning", "Mesh collapsing will be capped to generating meshes with at most 255 material slots, as it is required for Raytracing. Meshes could be collapsed further if Raytracing were disabled.") );
							bShowedRaytracingWarning = true;
						}

						bCollapsesChildren = false;
						break;
					}
				}
			}
		}
	}

	return bCollapsesChildren;
}

bool FUsdGeomXformableTranslator::CanBeCollapsed( ECollapsingType CollapsingType ) const
{
	// Don't collapse animated prims
	return Context->bAllowCollapsing && !UsdUtils::IsAnimated( GetPrim() );
}

#endif // #if USE_USD_SDK
