// Copyright Epic Games, Inc. All Rights Reserved.

#include "USDStageActor.h"

#include "UnrealUSDWrapper.h"
#include "USDAssetCache.h"
#include "USDAssetImportData.h"
#include "USDConversionUtils.h"
#include "USDErrorUtils.h"
#include "USDGeomMeshTranslator.h"
#include "USDGeomXformableTranslator.h"
#include "USDLayerUtils.h"
#include "USDLightConversion.h"
#include "USDListener.h"
#include "USDLog.h"
#include "USDPrimConversion.h"
#include "USDSchemasModule.h"
#include "USDSchemaTranslator.h"
#include "USDSkelRootTranslator.h"
#include "USDTransactor.h"
#include "USDTypesConversion.h"

#include "UsdWrappers/UsdAttribute.h"
#include "UsdWrappers/UsdGeomXformable.h"
#include "UsdWrappers/SdfLayer.h"
#include "UsdWrappers/SdfChangeBlock.h"

#include "Async/ParallelFor.h"
#include "CineCameraActor.h"
#include "CineCameraComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/LightComponent.h"
#include "Components/LightComponentBase.h"
#include "Components/PointLightComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/RectLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/Light.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "LevelSequence.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialInterface.h"
#include "MeshDescription.h"
#include "Misc/Paths.h"
#include "Misc/ScopedSlowTask.h"
#include "Modules/ModuleManager.h"
#include "PhysicsEngine/BodySetup.h"
#include "Rendering/SkeletalMeshLODImporterData.h"
#include "StaticMeshAttributes.h"
#include "StaticMeshOperations.h"
#include "Tracks/MovieScene3DTransformTrack.h"

#if WITH_EDITOR
#include "Editor.h"
#include "Editor/TransBuffer.h"
#include "Editor/UnrealEdEngine.h"
#include "LevelEditor.h"
#include "PropertyEditorModule.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "UnrealEdGlobals.h"
#endif // WITH_EDITOR

#define LOCTEXT_NAMESPACE "USDStageActor"

static const EObjectFlags DefaultObjFlag = EObjectFlags::RF_Transactional | EObjectFlags::RF_Transient;

AUsdStageActor::FOnActorLoaded AUsdStageActor::OnActorLoaded;

struct FUsdStageActorImpl
{
	static TSharedRef< FUsdSchemaTranslationContext > CreateUsdSchemaTranslationContext( AUsdStageActor* StageActor, const FString& PrimPath )
	{
		TSharedRef< FUsdSchemaTranslationContext > TranslationContext = MakeShared< FUsdSchemaTranslationContext >(
			StageActor->GetOrLoadUsdStage(),
			*StageActor->AssetCache
		);

		TranslationContext->Level = StageActor->GetLevel();
		TranslationContext->ObjectFlags = DefaultObjFlag;
		TranslationContext->Time = StageActor->GetTime();
		TranslationContext->PurposesToLoad = (EUsdPurpose) StageActor->PurposesToLoad;
		TranslationContext->RenderContext = StageActor->RenderContext;
		TranslationContext->MaterialToPrimvarToUVIndex = &StageActor->MaterialToPrimvarToUVIndex;
		TranslationContext->BlendShapesByPath = &StageActor->BlendShapesByPath;

		// Its more convenient to toggle between variants using the USDStage window, as opposed to parsing LODs
		TranslationContext->bAllowInterpretingLODs = false;

		// No point in baking these UAnimSequence assets if we're going to be sampling the stage in real time anyway
		TranslationContext->bAllowParsingSkeletalAnimations = false;

		UE::FSdfPath UsdPrimPath( *PrimPath );
		UUsdPrimTwin* ParentUsdPrimTwin = StageActor->GetRootPrimTwin()->Find( UsdPrimPath.GetParentPath().GetString() );

		if ( !ParentUsdPrimTwin )
		{
			ParentUsdPrimTwin = StageActor->RootUsdTwin;
		}

		TranslationContext->ParentComponent = ParentUsdPrimTwin ? ParentUsdPrimTwin->SceneComponent.Get() : nullptr;

		if ( !TranslationContext->ParentComponent )
		{
			TranslationContext->ParentComponent = StageActor->RootComponent;
		}

		return TranslationContext;
	}

	// Workaround some issues where the details panel will crash when showing a property of a component we'll force-delete
	static void DeselectActorsAndComponents( AUsdStageActor* StageActor )
	{
#if WITH_EDITOR
		if ( !StageActor )
		{
			return;
		}

		// This can get called when an actor is being destroyed due to GC.
		// Don't do this during garbage collecting if we need to delay-create the root twin (can't NewObject during garbage collection).
		// If we have no root twin we don't have any tracked spawned actors and components, so we don't need to deselect anything in the first place
		if ( !IsGarbageCollecting() || StageActor->RootUsdTwin )
		{
			TArray<UObject*> ObjectsToDelete;
			const bool bRecursive = true;
			StageActor->GetRootPrimTwin()->Iterate( [ &ObjectsToDelete ]( UUsdPrimTwin& PrimTwin )
			{
				if ( AActor* ReferencedActor = PrimTwin.SpawnedActor.Get() )
				{
					ObjectsToDelete.Add( ReferencedActor );
				}
				if ( USceneComponent* ReferencedComponent = PrimTwin.SceneComponent.Get() )
				{
					ObjectsToDelete.Add( ReferencedComponent );
				}
			}, bRecursive );

			FPropertyEditorModule& PropertyEditorModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>( "PropertyEditor" );
			PropertyEditorModule.RemoveDeletedObjects( ObjectsToDelete );
		}

		if ( GIsEditor && GEditor ) // Make sure we're not in standalone either
		{
			GEditor->NoteSelectionChange();
		}
#endif // WITH_EDITOR
	}

	static void CloseEditorsForAssets( const TMap< FString, UObject* >& AssetsCache )
	{
#if WITH_EDITOR
		if ( GIsEditor && GEditor )
		{
			if ( UAssetEditorSubsystem* AssetEditorSubsysttem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>() )
			{
				for ( const TPair<FString, UObject*>& Pair : AssetsCache )
				{
					if ( UObject* Asset = Pair.Value )
					{
						AssetEditorSubsysttem->CloseAllEditorsForAsset( Asset );
					}
				}
			}
		}
#endif // WITH_EDITOR
	}

	static void DiscardStage( const UE::FUsdStage& Stage, AUsdStageActor* DiscardingActor )
	{
		if ( !Stage || !DiscardingActor )
		{
			return;
		}

		UE::FSdfLayer RootLayer = Stage.GetRootLayer();
		if ( RootLayer && RootLayer.IsAnonymous() )
		{
			// Erasing an anonymous stage would fully delete it. If we later undo/redo into a path that referenced
			// one of those anonymous layers, we wouldn't be able to load it back again.
			// To prevent that, for now we don't actually erase anonymous stages when discarding them. This shouldn't be
			// so bad as these stages are likely to be pretty small anyway... in the future we may have some better way of
			// undo/redoing USD operations that could eliminate this issue
			return;
		}

		TArray<UObject*> Instances;
		AUsdStageActor::StaticClass()->GetDefaultObject()->GetArchetypeInstances( Instances );
		for ( UObject* Instance : Instances )
		{
			if ( Instance == DiscardingActor || !Instance || Instance->IsPendingKill() || Instance->IsTemplate() )
			{
				continue;
			}

			// Need to use the const version here or we may inadvertently load the stage
			if ( const AUsdStageActor* Actor = Cast<const AUsdStageActor>( Instance ) )
			{
				const UE::FUsdStage& OtherStage = Actor->GetUsdStage();
				if ( OtherStage && RootLayer == OtherStage )
				{
					// Some other actor is still using our stage, so don't close it
					return;
				}
			}
		}

		UnrealUSDWrapper::EraseStageFromCache( Stage );
	}

	static UE::FSdfPath UnwindToNonCollapsedPrim( AUsdStageActor* StageActor, const FString& InPrimPath, FUsdSchemaTranslator::ECollapsingType CollapsingType )
	{
		IUsdSchemasModule& UsdSchemasModule = FModuleManager::Get().LoadModuleChecked< IUsdSchemasModule >( TEXT( "USDSchemas" ) );

		TSharedRef< FUsdSchemaTranslationContext > TranslationContext = FUsdStageActorImpl::CreateUsdSchemaTranslationContext( StageActor, InPrimPath );

		UE::FUsdStage UsdStage = StageActor->GetOrLoadUsdStage();
		UE::FSdfPath UsdPrimPath( *InPrimPath );
		UE::FUsdPrim UsdPrim = UsdStage.GetPrimAtPath( UsdPrimPath );

		if ( TSharedPtr< FUsdSchemaTranslator > SchemaTranslator = UsdSchemasModule.GetTranslatorRegistry().CreateTranslatorForSchema( TranslationContext, UE::FUsdTyped( UsdPrim ) ) )
		{
			while ( SchemaTranslator->IsCollapsed( CollapsingType ) )
			{
				UE::FSdfPath ParentUsdPrimPath = UsdPrimPath.GetParentPath();
				UE::FUsdPrim ParentUsdPrim = UsdStage.GetPrimAtPath( ParentUsdPrimPath );
				if ( ParentUsdPrim.IsPseudoRoot() )
				{
					// It doesn't matter if we're collapsed when our parent is the root: We'll be a separate component/asset anyway.
					// At that point we don't want to return "/" from this function though, so break here
					break;
				}

				UsdPrimPath = ParentUsdPrimPath;
				UsdPrim = ParentUsdPrim;

				TranslationContext = FUsdStageActorImpl::CreateUsdSchemaTranslationContext( StageActor, UsdPrimPath.GetString() );
				TSharedPtr< FUsdSchemaTranslator > ParentSchemaTranslator = UsdSchemasModule.GetTranslatorRegistry().CreateTranslatorForSchema( TranslationContext, UE::FUsdTyped( UsdPrim ) );

				// Note how we continue looping with the child translator when the parent has an invalid translator.
				// This is intentional: If our parent here has no valid translator, it will always be collapsed, so we need to check
				// whether we have a valid schema translator for our grandparent. If that one is valid and collapses, both us and our parent will
				// be collapsed into the grandparent, and that's the path we need to return
				if ( ParentSchemaTranslator.IsValid() )
				{
					SchemaTranslator = ParentSchemaTranslator;
				}
			}
		}

		return UsdPrimPath;
	};

	static bool ObjectNeedsMultiUserTag( UObject* Object, AUsdStageActor* StageActor )
	{
		// Don't need to tag non-transient stuff
		if ( !Object->HasAnyFlags( RF_Transient ) )
		{
			return false;
		}

		// Object already has tag
		if ( AActor* Actor = Cast<AActor>( Object ) )
		{
			if ( Actor->Tags.Contains( UE::UsdTransactor::ConcertSyncEnableTag ) )
			{
				return false;
			}
		}
		else if ( USceneComponent* Component = Cast<USceneComponent>( Object ) )
		{
			if ( Component->ComponentTags.Contains( UE::UsdTransactor::ConcertSyncEnableTag ) )
			{
				return false;
			}
		}

		// Only care about objects that the same actor spawned
		bool bOwnedByStageActor = false;
		if ( StageActor->ObjectsToWatch.Contains( Object ) )
		{
			bOwnedByStageActor = true;
		}
		if ( AActor* Actor = Cast<AActor>( Object ) )
		{
			if ( StageActor->ObjectsToWatch.Contains( Actor->GetRootComponent() ) )
			{
				bOwnedByStageActor = true;
			}
		}
		else if ( AActor* Outer = Object->GetTypedOuter<AActor>() )
		{
			if ( StageActor->ObjectsToWatch.Contains( Outer->GetRootComponent() ) )
			{
				bOwnedByStageActor = true;
			}
		}
		if ( !bOwnedByStageActor )
		{
			return false;
		}

		return bOwnedByStageActor;
	}

	static void WhitelistComponentHierarchy( USceneComponent* Component, TSet<UObject*>& VisitedObjects )
	{
		if ( !Component || VisitedObjects.Contains( Component ) )
		{
			return;
		}

		VisitedObjects.Add( Component );

		if ( Component->HasAnyFlags( RF_Transient ) )
		{
			Component->ComponentTags.AddUnique( UE::UsdTransactor::ConcertSyncEnableTag );
		}

		if ( AActor* Owner = Component->GetOwner() )
		{
			if ( !VisitedObjects.Contains( Owner ) && Owner->HasAnyFlags( RF_Transient ) )
			{
				Owner->Tags.AddUnique( UE::UsdTransactor::ConcertSyncEnableTag );
			}

			VisitedObjects.Add( Owner );
		}

		// Iterate the attachment hierarchy directly because maybe some of those actors have additional components that aren't being
		// tracked by a prim twin
		for ( USceneComponent* Child : Component->GetAttachChildren() )
		{
			WhitelistComponentHierarchy( Child, VisitedObjects );
		}
	}

	// Checks if a project-relative file path refers to a layer. It requires caution because anonymous layers need to be handled differently.
	// WARNING: This will break if FilePath is a relative path relative to anything else other than the Project directory (i.e. engine binary)
	static bool DoesPathPointToLayer( FString FilePath, const UE::FSdfLayer& Layer )
	{
#if USE_USD_SDK
		if ( !Layer )
		{
			return false;
		}

		if ( !FilePath.IsEmpty() && !FPaths::IsRelative( FilePath ) && !FilePath.StartsWith( UnrealIdentifiers::IdentifierPrefix ) )
		{
			FilePath = UsdUtils::MakePathRelativeToProjectDir( FilePath );
		}

		// Special handling for anonymous layers as the RealPath is empty
		if ( Layer.IsAnonymous() )
		{
			// Something like "anon:0000022F9E194D50:tmp.usda"
			const FString LayerIdentifier = Layer.GetIdentifier();

			// Something like "@identifier:anon:0000022F9E194D50:tmp.usda" if we're also pointing at an anonymous layer
			if ( FilePath.RemoveFromStart( UnrealIdentifiers::IdentifierPrefix ) )
			{
				// Same anonymous layers
				if ( FilePath == LayerIdentifier )
				{
					return true;
				}
			}
			// RootLayer.FilePath is not an anonymous layer but the stage is
			else
			{
				return false;
			}
		}
		else
		{
			return FPaths::IsSamePath( UsdUtils::MakePathRelativeToProjectDir( Layer.GetRealPath() ), FilePath );
		}
#endif // USE_USD_SDK

		return false;
	}

	/**
	 * Uses USD's MakeVisible to handle the visible/inherited update logic as it is a bit complex.
	 * Will update a potentially large chunk of the component hierarchy to having/not the `invisible` component tag, as well as the
	 * correct value of bVisible.
	 * Note that bVisible corresponds to computed visibility, and the component tags correspond to individual prim-level visibilities
	 */
	static void MakeVisible( UUsdPrimTwin& UsdPrimTwin, UE::FUsdStage& Stage )
	{
		// Find the highest invisible prim parent: Nothing above this can possibly change with what we're doing
		UUsdPrimTwin* Iter = &UsdPrimTwin;
		UUsdPrimTwin* HighestInvisibleParent = nullptr;
		while ( Iter )
		{
			if ( USceneComponent* Component = Iter->GetSceneComponent() )
			{
				if ( Component->ComponentTags.Contains( UnrealIdentifiers::Invisible ) )
				{
					HighestInvisibleParent = Iter;
				}
			}

			Iter = Iter->GetParent();
		}

		// No parent (not even UsdPrimTwin's prim directly) was invisible, so we should already be visible and there's nothing to do
		if ( !HighestInvisibleParent )
		{
			return;
		}

		UE::FUsdPrim Prim = Stage.GetPrimAtPath( UE::FSdfPath( *UsdPrimTwin.PrimPath ) );
		if ( !Prim )
		{
			return;
		}
		UsdUtils::MakeVisible( Prim );

		TFunction<void(UUsdPrimTwin&, bool)> RecursiveResyncVisibility;
		RecursiveResyncVisibility = [ &Stage, &RecursiveResyncVisibility ]( UUsdPrimTwin& PrimTwin, bool bPrimHasInvisibleParent )
		{
			USceneComponent* Component = PrimTwin.GetSceneComponent();
			if ( !Component )
			{
				return;
			}

			UE::FUsdPrim CurrentPrim = Stage.GetPrimAtPath( UE::FSdfPath( *PrimTwin.PrimPath ) );
			if ( !CurrentPrim )
			{
				return;
			}

			const bool bPrimHasInheritedVisibility = UsdUtils::HasInheritedVisibility( CurrentPrim );
			const bool bPrimIsVisible = bPrimHasInheritedVisibility && !bPrimHasInvisibleParent;

			const bool bComponentHasInvisibleTag = Component->ComponentTags.Contains( UnrealIdentifiers::Invisible );
			const bool bComponentIsVisible = Component->IsVisible();

			const bool bTagIsCorrect = bComponentHasInvisibleTag == !bPrimHasInheritedVisibility;
			const bool bComputedVisibilityIsCorrect = bPrimIsVisible == bComponentIsVisible;

			// Stop recursing as this prim's or its children couldn't possibly need to be updated
			if ( bTagIsCorrect && bComputedVisibilityIsCorrect )
			{
				return;
			}

			if ( !bTagIsCorrect )
			{
				if ( bPrimHasInheritedVisibility )
				{
					Component->ComponentTags.Remove( UnrealIdentifiers::Invisible );
					Component->ComponentTags.AddUnique( UnrealIdentifiers::Inherited );
				}
				else
				{
					Component->ComponentTags.AddUnique( UnrealIdentifiers::Invisible );
					Component->ComponentTags.Remove( UnrealIdentifiers::Inherited );
				}
			}

			if ( !bComputedVisibilityIsCorrect )
			{
				const bool bPropagateToChildren = false;
				Component->Modify();
				Component->SetVisibility( bPrimIsVisible, bPropagateToChildren );
			}

			for ( const TPair<FString, UUsdPrimTwin*>& ChildPair : PrimTwin.GetChildren() )
			{
				if ( UUsdPrimTwin* ChildTwin = ChildPair.Value )
				{
					RecursiveResyncVisibility( *ChildTwin, !bPrimIsVisible );
				}
			}
		};

		const bool bHasInvisibleParent = false;
		RecursiveResyncVisibility( *HighestInvisibleParent, bHasInvisibleParent );
	}

	/**
	 * Sets this prim to 'invisible', and force all of the child components
	 * to bVisible = false. Leave their individual prim-level visibilities intact though.
	 * Note that bVisible corresponds to computed visibility, and the component tags correspond to individual prim-level visibilities
	 */
	static void MakeInvisible( UUsdPrimTwin& UsdPrimTwin )
	{
		USceneComponent* PrimSceneComponent = UsdPrimTwin.GetSceneComponent();
		if ( !PrimSceneComponent )
		{
			return;
		}

		PrimSceneComponent->ComponentTags.AddUnique( UnrealIdentifiers::Invisible );
		PrimSceneComponent->ComponentTags.Remove( UnrealIdentifiers::Inherited );

		const bool bPropagateToChildren = true;
		const bool bVisible = false;
		PrimSceneComponent->SetVisibility( bVisible, bPropagateToChildren );
	}
};

/**
 * Class that helps us know when a blueprint that derives from AUsdStageActor is being compiled.
 * Crucially this includes the process where existing instances of that blueprint are being reinstantiated and replaced.
 *
 * Recompiling a blueprint is not a transaction, which means we can't ever load a new stage during the process of
 * recompilation, or else the spawned assets/actors wouldn't be accounted for in the undo buffer and would lead to undo/redo bugs.
 *
 * This is a problem because we use PostActorCreated to load the stage whenever a blueprint is first placed on a level,
 * but that function also gets called during the reinstantiation process (where we can't load the stage). This means we need to be
 * able to tell from PostActorCreated when we're a new actor being dropped on the level, or just a reinstantiating actor
 * replacing an existing one, which is what this class provides.
 */
#if WITH_EDITOR
struct FRecompilationTracker
{
	static void SetupEvents()
	{
		if ( bEventIsSetup || !GIsEditor || !GEditor )
		{
			return;
		}

		GEditor->OnBlueprintPreCompile().AddStatic( &FRecompilationTracker::OnCompilationStarted );
		bEventIsSetup = true;
	}

	static bool IsBeingCompiled( UBlueprint* BP )
	{
		return FRecompilationTracker::RecompilingBlueprints.Contains( BP );
	}

	static void OnCompilationStarted( UBlueprint* BP )
	{
		if ( !BP ||
			 !BP->GeneratedClass ||
			 !BP->GeneratedClass->IsChildOf( AUsdStageActor::StaticClass() ) ||
			 RecompilingBlueprints.Contains( BP ) )
		{
			return;
		}

		FDelegateHandle Handle = BP->OnCompiled().AddStatic( &FRecompilationTracker::OnCompilationEnded );
		FRecompilationTracker::RecompilingBlueprints.Add( BP, Handle );
	}

	static void OnCompilationEnded( UBlueprint* BP )
	{
		if ( !BP )
		{
			return;
		}

		FDelegateHandle RemovedHandle;
		if ( FRecompilationTracker::RecompilingBlueprints.RemoveAndCopyValue( BP, RemovedHandle ) )
		{
			BP->OnCompiled().Remove( RemovedHandle );
		}
	}

private:
	static bool bEventIsSetup;
	static TMap<UBlueprint*, FDelegateHandle> RecompilingBlueprints;
};
bool FRecompilationTracker::bEventIsSetup = false;
TMap<UBlueprint*, FDelegateHandle> FRecompilationTracker::RecompilingBlueprints;
#endif // WITH_EDITOR

AUsdStageActor::AUsdStageActor()
	: InitialLoadSet( EUsdInitialLoadSet::LoadAll )
	, PurposesToLoad( (int32) EUsdPurpose::Proxy )
	, Time( 0.0f )
	, bIsTransitioningIntoPIE( false )
	, bIsModifyingAProperty( false )
	, bIsUndoRedoing( false )
{
	SceneComponent = CreateDefaultSubobject< USceneComponent >( TEXT("SceneComponent0") );
	SceneComponent->Mobility = EComponentMobility::Static;

	RootComponent = SceneComponent;

	AssetCache = CreateDefaultSubobject< UUsdAssetCache >( TEXT("AssetCache") );

	// Note: We can't construct our RootUsdTwin as a default subobject here, it needs to be built on-demand.
	// Even if we NewObject'd one it will work as a subobject in some contexts (maybe because the CDO will have a dedicated root twin?).
	// As far as the engine is concerned, our prim twins are static assets like meshes or textures. However, they live on the transient
	// package and we are the only strong reference to them, so the lifetime works out about the same, except we get to keep them during
	// some transitions like reinstantiation.
	// c.f. doc comment on FRecompilationTracker for more info.

	IUsdSchemasModule& UsdSchemasModule = FModuleManager::Get().LoadModuleChecked< IUsdSchemasModule >( TEXT("USDSchemas") );
	RenderContext = UsdSchemasModule.GetRenderContextRegistry().GetUniversalRenderContext();

	Transactor = NewObject<UUsdTransactor>( this, TEXT( "Transactor" ), EObjectFlags::RF_Transactional );
	Transactor->Initialize( this );

	if ( HasAuthorityOverStage() )
	{
#if WITH_EDITOR
		// Update the supported filetypes in our RootPath property
		for ( TFieldIterator<FProperty> PropertyIterator( AUsdStageActor::StaticClass() ); PropertyIterator; ++PropertyIterator )
		{
			FProperty* Property = *PropertyIterator;
			if ( Property && Property->GetFName() == GET_MEMBER_NAME_CHECKED( AUsdStageActor, RootLayer ) )
			{
				TArray< FString > SupportedExtensions = UnrealUSDWrapper::GetAllSupportedFileFormats();
				if ( SupportedExtensions.Num() > 0 )
				{
					FString JoinedExtensions = FString::Join( SupportedExtensions, TEXT( "; *." ) ); // Combine "usd" and "usda" into "usd; *.usda"
					Property->SetMetaData( TEXT("FilePathFilter"), FString::Printf( TEXT( "usd files (*.%s)|*.%s" ), *JoinedExtensions, *JoinedExtensions ) );
				}
				break;
			}
		}

		FEditorDelegates::BeginPIE.AddUObject(this, &AUsdStageActor::OnBeginPIE);
		FEditorDelegates::PostPIEStarted.AddUObject(this, &AUsdStageActor::OnPostPIEStarted);

		FUsdDelegates::OnPostUsdImport.AddUObject( this, &AUsdStageActor::OnPostUsdImport );
		FUsdDelegates::OnPreUsdImport.AddUObject( this, &AUsdStageActor::OnPreUsdImport );

		GEngine->OnLevelActorDeleted().AddUObject( this, &AUsdStageActor::OnLevelActorDeleted );

		// When another client of a multi-user session modifies their version of this actor, the transaction will be replicated here.
		// The multi-user system uses "redo" to apply those transactions, so this is our best chance to respond to events as e.g. neither
		// PostTransacted nor Destroyed get called when the other user deletes the actor
		if ( UTransBuffer* TransBuffer = GUnrealEd ? Cast<UTransBuffer>( GUnrealEd->Trans ) : nullptr )
		{
			TransBuffer->OnTransactionStateChanged().AddUObject( this, &AUsdStageActor::HandleTransactionStateChanged );

			// We can't use AddUObject here as we may specifically want to respond *after* we're marked as pending kill
			OnRedoHandle = TransBuffer->OnRedo().AddLambda(
				[ this ]( const FTransactionContext& TransactionContext, bool bSucceeded )
				{
					// This text should match the one in ConcertClientTransactionBridge.cpp
					if ( this &&
						 HasAuthorityOverStage() &&
						 TransactionContext.Title.EqualTo( LOCTEXT( "ConcertTransactionEvent", "Concert Transaction Event" ) ) &&
						 !RootLayer.FilePath.IsEmpty() )
					{
						// Other user deleted us
						if ( this->IsPendingKill() )
						{
							Reset();
						}
						// We have a valid filepath but no objects/assets spawned, so it's likely we were just spawned on the
						// other client, and were replicated here with our RootLayer path already filled out, meaning we should just load that stage
						// Note that now our UUsdTransactor may have already caused the stage itself to be loaded, but we may still need to call LoadUsdStage on our end.
						else if ( ObjectsToWatch.Num() == 0 && ( !AssetCache || AssetCache->GetNumAssets() == 0 ) )
						{
							this->LoadUsdStage();
							AUsdStageActor::OnActorLoaded.Broadcast( this );
						}
					}
				}
			);
		}

		FCoreUObjectDelegates::OnObjectPropertyChanged.AddUObject( this, &AUsdStageActor::OnObjectPropertyChanged );

		// Also prevent standalone from doing this
		if ( GIsEditor && GEditor )
		{
			if ( UBlueprintGeneratedClass* BPClass = Cast<UBlueprintGeneratedClass>( GetClass() ) )
			{
				FRecompilationTracker::SetupEvents();
				GEditor->OnObjectsReplaced().AddUObject( this, &AUsdStageActor::OnObjectsReplaced );
			}
		}

#endif // WITH_EDITOR

		OnTimeChanged.AddUObject( this, &AUsdStageActor::AnimatePrims );

		UsdListener.GetOnObjectsChanged().AddUObject( this, &AUsdStageActor::OnUsdObjectsChanged );

		UsdListener.GetOnLayersChanged().AddLambda(
			[&]( const TArray< FString >& ChangeVec )
			{
				if ( !IsListeningToUsdNotices() )
				{
					return;
				}

				TOptional< TGuardValue< ITransaction* > > SuppressTransaction;
				if ( this->GetOutermost()->HasAnyPackageFlags( PKG_PlayInEditor ) )
				{
					SuppressTransaction.Emplace( GUndo, nullptr );
				}

				// Check to see if any layer reloaded. If so, rebuild all of our animations as a single layer changing
				// might propagate timecodes through all level sequences
				for ( const FString& ChangeVecItem : ChangeVec )
				{
					UE_LOG( LogUsd, Verbose, TEXT("Reloading animations because layer '%s' was added/removed/reloaded"), *ChangeVecItem );
					ReloadAnimations();

					// Make sure our PrimsToAnimate and the LevelSequenceHelper are kept in sync, because we'll use PrimsToAnimate to
					// check whether we need to call LevelSequenceHelper::AddPrim within AUsdStageActor::ExpandPrim. Without this reset
					// our prims would already be in here by the time we're checking if we need to add tracks or not, and we wouldn't re-add
					// the tracks
					PrimsToAnimate.Reset();
					return;
				}
			}
		);
	}
}

void AUsdStageActor::OnUsdObjectsChanged( const UsdUtils::FObjectChangesByPath& InfoChanges, const UsdUtils::FObjectChangesByPath& ResyncChanges )
{
	if ( !IsListeningToUsdNotices() )
	{
		return;
	}

	// Only update the transactor if we're listening to USD notices. Within OnObjectPropertyChanged we will stop listening when writing stage changes
	// from our component changes, and this will also make sure we're not duplicating the events we store and replicate via multi-user: If a modification
	// can be described purely via UObject changes, then those changes will be responsible for the whole modification and we won't record the corresponding
	// stage changes. The intent is that when undo/redo/replicating that UObject change, it will automatically generate the corresponding stage changes
	if ( Transactor )
	{
		Transactor->Update( InfoChanges, ResyncChanges );
	}

	// If the stage was closed in a big transaction (e.g. undo open) a random UObject may be transacting before us and triggering USD changes,
	// and the UE::FUsdStage will still be opened and valid (even though we intend on closing/changing it when we transact). It could be problematic/wasteful if we
	// responded to those notices, so just early out here. We can do this check because our RootLayer property will already have the new value
	{
		const UE::FUsdStage& Stage = GetOrLoadUsdStage();
		if ( !Stage )
		{
			return;
		}

		const UE::FSdfLayer& StageRoot = Stage.GetRootLayer();
		if ( !StageRoot )
		{
			return;
		}

		if ( !FUsdStageActorImpl::DoesPathPointToLayer( RootLayer.FilePath, StageRoot ) )
		{
			return;
		}
	}

	// We may update our levelsequence objects (tracks, moviescene, sections, etc.) due to these changes. We definitely don't want to write anything
	// back to USD when these objects change though
	LevelSequenceHelper.BlockMonitoringChangesForThisTransaction();

	// The most important thing here is to iterate in parent to child order, so build SortedPrimsChangedList
	TMap< FString, bool > SortedPrimsChangedList;
	for ( const TPair<FString, TArray<UsdUtils::FObjectChangeNotice>>& InfoChange : InfoChanges )
	{
		const FString& PrimPath = InfoChange.Key;

		// Some stage info should trigger some resyncs (even though technically info changes) because they should trigger reparsing of geometry
		bool bIsResync = false;
		if ( PrimPath == TEXT( "/" ) )
		{
			for ( const UsdUtils::FObjectChangeNotice& ObjectChange : InfoChange.Value )
			{
				for ( const UsdUtils::FAttributeChange& AttributeChange : ObjectChange.AttributeChanges )
				{
					static const TSet<FString> ResyncProperties = { TEXT( "metersPerUnit" ), TEXT( "upAxis" ) };
					if ( ResyncProperties.Contains( AttributeChange.PropertyName ) )
					{
						bIsResync = true;
					}
				}
			}
		}

		// We may need the full spec path with variant selections later, but for traversal and retrieving prims from the stage we always need
		// the prim path without any variant selections in it (i.e. GetPrimAtPath("/Root{Varset=Var}Child") doesn't work, we need GetPrimAtPath("/Root/Child")),
		// and USD sometimes emits changes with the variant selection path (like during renames).
		SortedPrimsChangedList.Add( UE::FSdfPath( *InfoChange.Key ).StripAllVariantSelections().GetString(), bIsResync );
	}
	// Do Resyncs after so that they overwrite pure info changes if we have any
	for ( const TPair<FString, TArray<UsdUtils::FObjectChangeNotice>>& ResyncChange : ResyncChanges )
	{
		const bool bIsResync = true;
		SortedPrimsChangedList.Add( UE::FSdfPath( *ResyncChange.Key ).StripAllVariantSelections().GetString(), bIsResync );
	}

	SortedPrimsChangedList.KeySort([]( const FString& A, const FString& B ) -> bool { return A.Len() < B.Len(); } );

	// During PIE, the PIE and the editor world will respond to notices. We have to prevent any PIE
	// objects from being added to the transaction however, or else it will be discarded when finalized.
	// We need to keep the transaction, or else we may end up with actors outside of the transaction
	// system that want to use assets that will be destroyed by it on an undo.
	// Note that we can't just make the spawned components/assets nontransactional because the PIE world will transact too
	TOptional<TGuardValue<ITransaction*>> SuppressTransaction;
	if ( this->GetOutermost()->HasAnyPackageFlags( PKG_PlayInEditor ) )
	{
		SuppressTransaction.Emplace(GUndo, nullptr);
	}

	FScopedSlowTask RefreshStageTask( SortedPrimsChangedList.Num(), LOCTEXT( "RefreshingUSDStage", "Refreshing USD Stage" ) );
	RefreshStageTask.MakeDialog();

	FScopedUsdMessageLog ScopedMessageLog;

	TSet< FString > UpdatedAssets;
	TSet< FString > ResyncedAssets;
	TSet< FString > UpdatedComponents;
	TSet< FString > ResyncedComponents;

	bool bDeselected = false;

	for ( const TPair< FString, bool >& PrimChangedInfo : SortedPrimsChangedList )
	{
		RefreshStageTask.EnterProgressFrame();

		const bool bIsResync = PrimChangedInfo.Value;

		if ( bIsResync && !bDeselected )
		{
			FUsdStageActorImpl::DeselectActorsAndComponents( this );
			bDeselected = true;
		}

		// Return if the path or any of its higher level paths are already processed
		auto IsPathAlreadyProcessed = []( TSet< FString >& PathsProcessed, FString PathToProcess ) -> bool
		{
			FString SubPath;
			FString ParentPath;

			if ( PathsProcessed.Contains( TEXT("/") ) )
			{
				return true;
			}

			while ( !PathToProcess.IsEmpty() && !PathsProcessed.Contains( PathToProcess ) )
			{
				if ( PathToProcess.Split( TEXT("/"), &ParentPath, &SubPath, ESearchCase::IgnoreCase, ESearchDir::FromEnd ) )
				{
					PathToProcess = ParentPath;
				}
				else
				{
					return false;
				}
			}

			return !PathToProcess.IsEmpty() && PathsProcessed.Contains( PathToProcess );
		};

		// Reload assets
		{
			UE::FSdfPath AssetsPrimPath = FUsdStageActorImpl::UnwindToNonCollapsedPrim( this, PrimChangedInfo.Key, FUsdSchemaTranslator::ECollapsingType::Assets );

			TSet< FString >& RefreshedAssets = bIsResync ? ResyncedAssets : UpdatedAssets;

			if ( !IsPathAlreadyProcessed( RefreshedAssets, AssetsPrimPath.GetString() ) )
			{
				TSharedRef< FUsdSchemaTranslationContext > TranslationContext = FUsdStageActorImpl::CreateUsdSchemaTranslationContext( this, AssetsPrimPath.GetString() );

				if ( bIsResync )
				{
					LoadAssets( *TranslationContext, GetOrLoadUsdStage().GetPrimAtPath( AssetsPrimPath ) );

					// Resyncing also includes "updating" the prim
					UpdatedAssets.Add( AssetsPrimPath.GetString() );
				}
				else
				{
					LoadAsset( *TranslationContext, GetOrLoadUsdStage().GetPrimAtPath( AssetsPrimPath ) );
				}

				RefreshedAssets.Add( AssetsPrimPath.GetString() );
			}
		}

		// Update components
		{
			UE::FSdfPath ComponentsPrimPath = FUsdStageActorImpl::UnwindToNonCollapsedPrim( this, PrimChangedInfo.Key, FUsdSchemaTranslator::ECollapsingType::Components );

			TSet< FString >& RefreshedComponents = bIsResync ? ResyncedComponents : UpdatedComponents;

			if ( !IsPathAlreadyProcessed( RefreshedComponents, ComponentsPrimPath.GetString() ) )
			{
				TSharedRef< FUsdSchemaTranslationContext > TranslationContext = FUsdStageActorImpl::CreateUsdSchemaTranslationContext( this, ComponentsPrimPath.GetString() );
				UpdatePrim( ComponentsPrimPath, bIsResync, *TranslationContext );
				TranslationContext->CompleteTasks();

				RefreshedComponents.Add( ComponentsPrimPath.GetString() );

				if ( bIsResync )
				{
					// Consider that the path has been updated in the case of a resync
					UpdatedComponents.Add( ComponentsPrimPath.GetString() );
				}
			}
		}

		if ( HasAuthorityOverStage() )
		{
			OnPrimChanged.Broadcast( PrimChangedInfo.Key, PrimChangedInfo.Value );
		}
	}
}

AUsdStageActor::~AUsdStageActor()
{
#if WITH_EDITOR
	if ( !IsEngineExitRequested() && HasAuthorityOverStage() )
	{
		FEditorDelegates::BeginPIE.RemoveAll(this);
		FEditorDelegates::PostPIEStarted.RemoveAll(this);
		FUsdDelegates::OnPostUsdImport.RemoveAll(this);
		FUsdDelegates::OnPreUsdImport.RemoveAll(this);
		if ( UTransBuffer* TransBuffer = GUnrealEd ? Cast<UTransBuffer>( GUnrealEd->Trans ) : nullptr )
		{
			TransBuffer->OnTransactionStateChanged().RemoveAll( this );
			TransBuffer->OnRedo().Remove( OnRedoHandle );
		}

		GEngine->OnLevelActorDeleted().AddUObject( this, &AUsdStageActor::OnLevelActorDeleted );

		// This clears the SUSDStage window whenever the level we're currently in gets destroyed.
		// Note that this is not called when deleting from the Editor, as the actor goes into the undo buffer.
		OnActorDestroyed.Broadcast();
		CloseUsdStage();

		if ( RootUsdTwin )
		{
			RootUsdTwin->Clear();
		}

		GEditor->OnObjectsReplaced().RemoveAll( this );
	}
#endif // WITH_EDITOR
}

USDSTAGE_API void AUsdStageActor::Reset()
{
	Super::Reset();

	UnloadUsdStage();

	Time = 0.f;
	RootLayer.FilePath.Empty();
}

void AUsdStageActor::StopListeningToUsdNotices()
{
	IsBlockedFromUsdNotices.Increment();
}

void AUsdStageActor::ResumeListeningToUsdNotices()
{
	IsBlockedFromUsdNotices.Decrement();
}

bool AUsdStageActor::IsListeningToUsdNotices() const
{
	return IsBlockedFromUsdNotices.GetValue() == 0;
}

void AUsdStageActor::StopMonitoringLevelSequence()
{
	LevelSequenceHelper.StopMonitoringChanges();
}

void AUsdStageActor::ResumeMonitoringLevelSequence()
{
	LevelSequenceHelper.StartMonitoringChanges();
}

void AUsdStageActor::BlockMonitoringLevelSequenceForThisTransaction()
{
	LevelSequenceHelper.BlockMonitoringChangesForThisTransaction();
}

UUsdPrimTwin* AUsdStageActor::GetOrCreatePrimTwin( const UE::FSdfPath& UsdPrimPath )
{
	const FString PrimPath = UsdPrimPath.GetString();
	const FString ParentPrimPath = UsdPrimPath.GetParentPath().GetString();

	UUsdPrimTwin* RootTwin = GetRootPrimTwin();
	UUsdPrimTwin* UsdPrimTwin = RootTwin->Find( PrimPath );
	UUsdPrimTwin* ParentUsdPrimTwin = RootTwin->Find( ParentPrimPath );

	const UE::FUsdPrim Prim = GetOrLoadUsdStage().GetPrimAtPath( UsdPrimPath );

	if ( !Prim )
	{
		return nullptr;
	}

	if ( !ParentUsdPrimTwin )
	{
		ParentUsdPrimTwin = RootUsdTwin;
	}

	if ( !UsdPrimTwin )
	{
		UsdPrimTwin = &ParentUsdPrimTwin->AddChild( *PrimPath );

		UsdPrimTwin->OnDestroyed.AddUObject( this, &AUsdStageActor::OnUsdPrimTwinDestroyed );
	}

	return UsdPrimTwin;
}

UUsdPrimTwin* AUsdStageActor::ExpandPrim( const UE::FUsdPrim& Prim, FUsdSchemaTranslationContext& TranslationContext )
{
	// "Active" is the non-destructive deletion used in USD. Sometimes when we rename/remove a prim in a complex stage it may remain in
	// an inactive state, but its otherwise effectively deleted
	if ( !Prim || !Prim.IsActive() )
	{
		return nullptr;
	}

	TRACE_CPUPROFILER_EVENT_SCOPE( AUsdStageActor::ExpandPrim );

	UUsdPrimTwin* UsdPrimTwin = GetOrCreatePrimTwin( Prim.GetPrimPath() );

	if ( !UsdPrimTwin )
	{
		return nullptr;
	}

	bool bExpandChildren = true;

	IUsdSchemasModule& UsdSchemasModule = FModuleManager::Get().LoadModuleChecked< IUsdSchemasModule >( TEXT("USDSchemas") );

	if ( TSharedPtr< FUsdSchemaTranslator > SchemaTranslator = UsdSchemasModule.GetTranslatorRegistry().CreateTranslatorForSchema( TranslationContext.AsShared(), UE::FUsdTyped( Prim ) ) )
	{
		if ( !UsdPrimTwin->SceneComponent.IsValid() )
		{
			UsdPrimTwin->SceneComponent = SchemaTranslator->CreateComponents();
		}
		else
		{
			ObjectsToWatch.Remove( UsdPrimTwin->SceneComponent.Get() );
			SchemaTranslator->UpdateComponents( UsdPrimTwin->SceneComponent.Get() );
		}

		bExpandChildren = !SchemaTranslator->CollapsesChildren( FUsdSchemaTranslator::ECollapsingType::Components );
	}

	if ( bExpandChildren )
	{
		USceneComponent* ContextParentComponent = TranslationContext.ParentComponent;

		if ( UsdPrimTwin->SceneComponent.IsValid() )
		{
			ContextParentComponent = UsdPrimTwin->SceneComponent.Get();
		}

		TGuardValue< USceneComponent* > ParentComponentGuard( TranslationContext.ParentComponent, ContextParentComponent );

		const bool bTraverseInstanceProxies = true;
		const TArray< UE::FUsdPrim > PrimChildren = Prim.GetFilteredChildren( bTraverseInstanceProxies );

		for ( const UE::FUsdPrim& ChildPrim : PrimChildren )
		{
			ExpandPrim( ChildPrim, TranslationContext );
		}
	}

	if ( UsdPrimTwin->SceneComponent.IsValid() )
	{
#if WITH_EDITOR
		UsdPrimTwin->SceneComponent->PostEditChange();
#endif // WITH_EDITOR

		if ( !UsdPrimTwin->SceneComponent->IsRegistered() )
		{
			UsdPrimTwin->SceneComponent->RegisterComponent();
		}

		ObjectsToWatch.Add( UsdPrimTwin->SceneComponent.Get(), UsdPrimTwin->PrimPath );
	}

	// Update the prim animated status
	if ( UsdUtils::IsAnimated( Prim ) )
	{
		if ( !PrimsToAnimate.Contains( UsdPrimTwin->PrimPath ) )
		{
			PrimsToAnimate.Add( UsdPrimTwin->PrimPath );
			LevelSequenceHelper.AddPrim( *UsdPrimTwin );
		}
	}
	else
	{
		if ( PrimsToAnimate.Contains( UsdPrimTwin->PrimPath ) )
		{
			PrimsToAnimate.Remove( UsdPrimTwin->PrimPath );
			LevelSequenceHelper.RemovePrim( *UsdPrimTwin );
		}
	}

	return UsdPrimTwin;
}

void AUsdStageActor::UpdatePrim( const UE::FSdfPath& InUsdPrimPath, bool bResync, FUsdSchemaTranslationContext& TranslationContext )
{
	TRACE_CPUPROFILER_EVENT_SCOPE( AUsdStageActor::UpdatePrim );

	FScopedSlowTask SlowTask( 1.f, LOCTEXT( "UpdatingUSDPrim", "Updating USD Prim" ) );
	SlowTask.MakeDialog();
	SlowTask.EnterProgressFrame();

	UE::FSdfPath UsdPrimPath = InUsdPrimPath;

	if ( !UsdPrimPath.IsAbsoluteRootOrPrimPath() )
	{
		UsdPrimPath = UsdPrimPath.GetAbsoluteRootOrPrimPath();
	}

	if ( UsdPrimPath.IsAbsoluteRootOrPrimPath() )
	{
		if ( bResync )
		{
			FString PrimPath = UsdPrimPath.GetString();
			if ( UUsdPrimTwin* UsdPrimTwin = GetRootPrimTwin()->Find( PrimPath ) )
			{
				UsdPrimTwin->Clear();
			}
		}

		UE::FUsdPrim PrimToExpand = GetOrLoadUsdStage().GetPrimAtPath( UsdPrimPath );
		UUsdPrimTwin* UsdPrimTwin = ExpandPrim( PrimToExpand, TranslationContext );

#if WITH_EDITOR
		if ( GIsEditor && GEditor ) // Make sure we're not in standalone either
		{
			GEditor->BroadcastLevelActorListChanged();
			GEditor->RedrawLevelEditingViewports();
		}
#endif // WITH_EDITOR
	}
}

UE::FUsdStage& AUsdStageActor::GetUsdStage()
{
	OpenUsdStage();

	return UsdStage;
}

const UE::FUsdStage& AUsdStageActor::GetUsdStage() const
{
	return UsdStage;
}

UE::FUsdStage& AUsdStageActor::GetOrLoadUsdStage()
{
	OpenUsdStage();

	return UsdStage;
}

void AUsdStageActor::SetRootLayer( const FString& RootFilePath )
{
	FString RelativeFilePath = RootFilePath;
#if USE_USD_SDK
	if ( !RelativeFilePath.IsEmpty() && !FPaths::IsRelative( RelativeFilePath ) && !RelativeFilePath.StartsWith( UnrealIdentifiers::IdentifierPrefix ) )
	{
		RelativeFilePath = UsdUtils::MakePathRelativeToProjectDir( RootFilePath );
	}
#endif // USE_USD_SDK

	// See if we're talking about the stage that is already loaded
	if ( UsdStage )
	{
		const UE::FSdfLayer& StageRootLayer = UsdStage.GetRootLayer();
		if ( StageRootLayer )
		{
			if ( FUsdStageActorImpl::DoesPathPointToLayer( RelativeFilePath, StageRootLayer ) )
			{
				return;
			}
		}
	}

	UnloadUsdStage();
	RootLayer.FilePath = RelativeFilePath;
	LoadUsdStage();
}

void AUsdStageActor::SetInitialLoadSet( EUsdInitialLoadSet NewLoadSet )
{
	InitialLoadSet = NewLoadSet;
	LoadUsdStage();
}

void AUsdStageActor::SetPurposesToLoad( int32 NewPurposesToLoad )
{
	PurposesToLoad = NewPurposesToLoad;
	LoadUsdStage();
}

void AUsdStageActor::SetRenderContext( const FName& NewRenderContext )
{
	RenderContext = NewRenderContext;
	LoadUsdStage();
}

void AUsdStageActor::SetTime(float InTime)
{
	Time = InTime;
	Refresh();
}

USceneComponent* AUsdStageActor::GetGeneratedComponent( const FString& PrimPath )
{
	FString UncollapsedPath = FUsdStageActorImpl::UnwindToNonCollapsedPrim( this, PrimPath, FUsdSchemaTranslator::ECollapsingType::Components ).GetString();

	if ( UUsdPrimTwin* UsdPrimTwin = GetRootPrimTwin()->Find( UncollapsedPath ) )
	{
		return UsdPrimTwin->GetSceneComponent();
	}

	return nullptr;
}

TArray<UObject*> AUsdStageActor::GetGeneratedAssets( const FString& PrimPath )
{
	if ( !AssetCache )
	{
		return {};
	}

	FString UncollapsedPath = FUsdStageActorImpl::UnwindToNonCollapsedPrim( this, PrimPath, FUsdSchemaTranslator::ECollapsingType::Assets ).GetString();

	TSet<UObject*> Result;

	if ( UObject* FoundAsset = AssetCache->GetAssetForPrim( UncollapsedPath ) )
	{
		Result.Add( FoundAsset );
	}

	for ( const TPair<FString, UObject*>& HashToAsset : AssetCache->GetCachedAssets() )
	{
		if ( UUsdAssetImportData* ImportData = UsdUtils::GetAssetImportData( HashToAsset.Value ) )
		{
			if ( ImportData->PrimPath == UncollapsedPath )
			{
				Result.Add( HashToAsset.Value );
			}
		}
	}

	return Result.Array();
}

FString AUsdStageActor::GetSourcePrimPath( UObject* Object )
{
	if ( USceneComponent* Component = Cast<USceneComponent>( Object ) )
	{
		if ( UUsdPrimTwin* UsdPrimTwin = GetRootPrimTwin()->Find( Component ) )
		{
			return UsdPrimTwin->PrimPath;
		}
	}

	if ( AssetCache )
	{
		for ( const TPair<FString, UObject*>& PrimPathToAsset : AssetCache->GetAssetPrimLinks() )
		{
			if ( PrimPathToAsset.Value == Object )
			{
				return PrimPathToAsset.Key;
			}
		}

		for ( const TPair<FString, UObject*>& HashToAsset : AssetCache->GetCachedAssets() )
		{
			if ( HashToAsset.Value == Object )
			{
				if ( UUsdAssetImportData* ImportData = UsdUtils::GetAssetImportData( HashToAsset.Value ) )
				{
					return ImportData->PrimPath;
				}
			}
		}
	}

	return FString();
}

void AUsdStageActor::OpenUsdStage()
{
	// Early exit if stage is already opened
	if ( UsdStage || RootLayer.FilePath.IsEmpty() )
	{
		return;
	}

	TRACE_CPUPROFILER_EVENT_SCOPE( AUsdStageActor::OpenUsdStage );

	UsdUtils::StartMonitoringErrors();

	FString AbsPath;
	if ( !RootLayer.FilePath.StartsWith( UnrealIdentifiers::IdentifierPrefix ) && FPaths::IsRelative( RootLayer.FilePath ) )
	{
		// The RootLayer property is marked as RelativeToGameDir, and UsdUtils::BrowseUsdFile will also emit paths relative to the project's directory
		FString ProjectDir = FPaths::ConvertRelativePathToFull( FPaths::ProjectDir() );
		AbsPath = FPaths::ConvertRelativePathToFull( FPaths::Combine( ProjectDir, RootLayer.FilePath ) );
	}
	else
	{
		AbsPath = RootLayer.FilePath;
	}

	UsdStage = UnrealUSDWrapper::OpenStage( *AbsPath, InitialLoadSet );
	if ( UsdStage )
	{
		UsdStage.SetEditTarget( UsdStage.GetRootLayer() );

		UsdListener.Register( UsdStage );

#if USE_USD_SDK
		// Try loading a UE-state session layer if we can find one
		const bool bCreateIfNeeded = false;
		UsdUtils::GetUEPersistentStateSublayer( UsdStage, bCreateIfNeeded );
#endif // #if USE_USD_SDK

		OnStageChanged.Broadcast();
	}

	UsdUtils::ShowErrorsAndStopMonitoring( FText::Format( LOCTEXT("USDOpenError", "Encountered some errors opening USD file at path '{0}!\nCheck the Output Log for details."), FText::FromString( RootLayer.FilePath ) ) );
}

void AUsdStageActor::CloseUsdStage()
{
	FUsdStageActorImpl::DiscardStage( UsdStage, this );
	UsdStage = UE::FUsdStage();
	LevelSequenceHelper.Init( UE::FUsdStage() ); // Drop the helper's reference to the stage
	OnStageChanged.Broadcast();
}

#if WITH_EDITOR
void AUsdStageActor::OnBeginPIE( bool bIsSimulating )
{
	// Remove transient flag from our spawned actors and components so they can be duplicated for PIE
	const bool bTransient = false;
	UpdateSpawnedObjectsTransientFlag( bTransient );

	bIsTransitioningIntoPIE = true;

	// Take ownership of our RootTwin and pretend our entire prim tree is a subobject so that it's duplicated over with us into PIE
	if ( UUsdPrimTwin* RootTwin = GetRootPrimTwin() )
	{
		RootTwin->Rename( nullptr, this );

		if ( FProperty* Prop = GetClass()->FindPropertyByName( GET_MEMBER_NAME_CHECKED( AUsdStageActor, RootUsdTwin ) ) )
		{
			Prop->ClearPropertyFlags( CPF_Transient );
		}

		if ( FProperty* Prop = UUsdPrimTwin::StaticClass()->FindPropertyByName( UUsdPrimTwin::GetChildrenPropertyName() ) )
		{
			Prop->ClearPropertyFlags( CPF_Transient );
		}
	}
}

void AUsdStageActor::OnPostPIEStarted( bool bIsSimulating )
{
	// Restore transient flags to our spawned actors and components so they aren't saved otherwise
	const bool bTransient = true;
	UpdateSpawnedObjectsTransientFlag( bTransient );

	bIsTransitioningIntoPIE = false;

	// Put our RootTwin back on the transient package so that if our blueprint is compiled it doesn't get reconstructed with us
	if ( UUsdPrimTwin* RootTwin = GetRootPrimTwin() )
	{
		RootTwin->Rename( nullptr, GetTransientPackage() );

		if ( FProperty* Prop = GetClass()->FindPropertyByName( GET_MEMBER_NAME_CHECKED( AUsdStageActor, RootUsdTwin ) ) )
		{
			Prop->SetPropertyFlags( CPF_Transient );
		}

		if ( FProperty* Prop = UUsdPrimTwin::StaticClass()->FindPropertyByName( UUsdPrimTwin::GetChildrenPropertyName() ) )
		{
			Prop->SetPropertyFlags( CPF_Transient );
		}
	}
}

void AUsdStageActor::OnObjectsReplaced( const TMap<UObject*, UObject*>& ObjectReplacementMap )
{
	UBlueprintGeneratedClass* BPClass = Cast<UBlueprintGeneratedClass>( GetClass() );
	if ( !BPClass )
	{
		return;
	}

	UBlueprint* BP = Cast<UBlueprint>( BPClass->ClassGeneratedBy );
	if ( !BP )
	{
		return;
	}

	// We are a replacement actor: Anything that is a property was already copied over,
	// and the spawned actors and components are still alive. We just need to move over any remaining non-property data
	if ( AUsdStageActor* NewActor = Cast<AUsdStageActor>( ObjectReplacementMap.FindRef( this ) ) )
	{
		// If our BP has changes and we're going into PIE, we'll get automatically recompiled. Sadly OnBeginPIE will trigger
		// before we're duplicated for the reinstantiation process, which is a problem because our prim twins will then be
		// owned by us by the time we're duplicated, which will clear them. This handles that case, and just duplicates the prim
		// twins from the old actor, which is what the reinstantiation process should have done instead anyway. Note that only
		// later will the components and actors being pointed to by this duplicated prim twin be moved to the PIE world, so those
		// references would be updated correctly.
		if ( RootUsdTwin->GetOuter() == this )
		{
			NewActor->RootUsdTwin = DuplicateObject( RootUsdTwin, NewActor );
		}

		if ( FRecompilationTracker::IsBeingCompiled( BP ) )
		{
			// Can't just move out of this one as TUsdStore expects its TOptional to always contain a value, and we may
			// still need to use the bool operator on it to test for validity
			NewActor->UsdStage = UsdStage;

			NewActor->LevelSequenceHelper = MoveTemp( LevelSequenceHelper );
			NewActor->LevelSequence = LevelSequence;
			NewActor->BlendShapesByPath = MoveTemp( BlendShapesByPath );
			NewActor->MaterialToPrimvarToUVIndex = MoveTemp( MaterialToPrimvarToUVIndex );

			NewActor->UsdListener.Register( NewActor->UsdStage );

			// This does not look super safe...
			NewActor->OnActorDestroyed = OnActorDestroyed;
			NewActor->OnActorLoaded = OnActorLoaded;
			NewActor->OnStageChanged = OnStageChanged;
			NewActor->OnPrimChanged = OnPrimChanged;

			// UEngine::CopyPropertiesForUnrelatedObjects won't copy over the cache's transient assets, but we still
			// need to ensure their lifetime here, so just take the previous asset cache instead, which still that has the transient assets
			AssetCache->Rename( nullptr, NewActor );
			NewActor->AssetCache = AssetCache;
		}
	}
}

void AUsdStageActor::OnLevelActorDeleted( AActor* DeletedActor )
{
	// Check for this here because it could be that we tried to delete this actor before changing any of its
	// properties, in which case our similar check within OnObjectPropertyChange hasn't had the chance to tag this actor
	if ( RootLayer.FilePath == OldRootLayer.FilePath && FUsdStageActorImpl::ObjectNeedsMultiUserTag( DeletedActor, this ) )
	{
		// DeletedActor is already detached from our hierarchy, so we must tag it directly
		TSet<UObject*> VisitedObjects;
		FUsdStageActorImpl::WhitelistComponentHierarchy( DeletedActor->GetRootComponent(), VisitedObjects );
	}
}

#endif // WITH_EDITOR

void AUsdStageActor::LoadUsdStage()
{
	TRACE_CPUPROFILER_EVENT_SCOPE( AUsdStageActor::LoadUsdStage );

	double StartTime = FPlatformTime::Cycles64();

	FScopedSlowTask SlowTask( 1.f, LOCTEXT( "LoadingUDStage", "Loading USD Stage") );
	SlowTask.MakeDialog();

	if ( !AssetCache )
	{
		AssetCache = NewObject< UUsdAssetCache >( this, TEXT("AssetCache"), GetMaskedFlags( RF_PropagateToSubObjects ) );
	}

	ObjectsToWatch.Reset();

	FUsdStageActorImpl::DeselectActorsAndComponents( this );

	UUsdPrimTwin* RootTwin = GetRootPrimTwin();
	RootTwin->Clear();
	RootTwin->PrimPath = TEXT( "/" );

	FScopedUsdMessageLog ScopedMessageLog;

	// If we're in here we don't expect our current stage to be the same as the new stage we're trying to load, so
	// get rid of it so that OpenUsdStage can open it
	UsdStage = UE::FUsdStage();

	OpenUsdStage();
	if ( !UsdStage )
	{
		OnStageChanged.Broadcast();
		return;
	}

	ReloadAnimations();

	// Make sure our PrimsToAnimate and the LevelSequenceHelper are kept in sync, because we'll use PrimsToAnimate to
	// check whether we need to call LevelSequenceHelper::AddPrim within AUsdStageActor::ExpandPrim. Without this reset
	// our prims would already be in here by the time we're checking if we need to add tracks or not, and we wouldn't re-add
	// the tracks
	PrimsToAnimate.Reset();

	TSharedRef< FUsdSchemaTranslationContext > TranslationContext = FUsdStageActorImpl::CreateUsdSchemaTranslationContext( this, RootTwin->PrimPath );

	SlowTask.EnterProgressFrame( 0.8f );
	LoadAssets( *TranslationContext, UsdStage.GetPseudoRoot() );

	SlowTask.EnterProgressFrame( 0.2f );
	UpdatePrim( UsdStage.GetPseudoRoot().GetPrimPath(), true, *TranslationContext );

	TranslationContext->CompleteTasks();

	if ( UsdStage.GetRootLayer() )
	{
		SetTime( UsdStage.GetRootLayer().GetStartTimeCode() );

		// Our CDO will never load the stage, so it will remain with some other Time value. If we don't update it, it will desync with the
		// the Time value of the instance on the preview editor (because the instance will load the stage and update its Time), and so our
		// manipulation of the CDO's Time value on the blueprint editor won't be propagated to the instance.
		// This means that we wouldn't be able to animate the preview actor at all. Here we fix that by resyncing our Time with the CDO
		if ( UBlueprintGeneratedClass* BPClass = Cast<UBlueprintGeneratedClass>( GetClass() ) )
		{
			// Note: CDO is an instance of a BlueprintGeneratedClass here and this is just a base class pointer. We're not changing the actual AUsdStageActor's CDO
			if ( AUsdStageActor* CDO = Cast<AUsdStageActor>( GetClass()->GetDefaultObject() ) )
			{
				CDO->SetTime( GetTime() );
			}
		}
	}

#if WITH_EDITOR
	if ( GIsEditor && GEditor )
	{
		GEditor->BroadcastLevelActorListChanged();
	}
#endif // WITH_EDITOR

	// Log time spent to load the stage
	double ElapsedSeconds = FPlatformTime::ToSeconds64(FPlatformTime::Cycles64() - StartTime);

	int ElapsedMin = int(ElapsedSeconds / 60.0);
	ElapsedSeconds -= 60.0 * (double)ElapsedMin;

	UE_LOG( LogUsd, Log, TEXT("%s %s in [%d min %.3f s]"), TEXT("Stage loaded"), *FPaths::GetBaseFilename( RootLayer.FilePath ), ElapsedMin, ElapsedSeconds );
}

void AUsdStageActor::UnloadUsdStage()
{
	Modify();

	FUsdStageActorImpl::DeselectActorsAndComponents( this );

	// Stop listening because we'll discard LevelSequence assets, which may trigger transactions
	// and could lead to stage changes
	BlockMonitoringLevelSequenceForThisTransaction();

	if ( AssetCache )
	{
		FUsdStageActorImpl::CloseEditorsForAssets( AssetCache->GetCachedAssets() );
		AssetCache->Reset();
	}

	ObjectsToWatch.Reset();
	BlendShapesByPath.Reset();
	MaterialToPrimvarToUVIndex.Reset();

	if ( LevelSequence )
	{
#if WITH_EDITOR
		if ( GEditor )
		{
			GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->CloseAllEditorsForAsset( LevelSequence );
		}
#endif // WITH_EDITOR
		LevelSequence = nullptr;
	}
	LevelSequenceHelper.Clear();

	if ( RootUsdTwin )
	{
		RootUsdTwin->Clear();
		RootUsdTwin->PrimPath = TEXT( "/" );
	}

#if WITH_EDITOR
	if ( GEditor )
	{
		GEditor->BroadcastLevelActorListChanged();
	}
#endif // WITH_EDITOR

	CloseUsdStage();

	OnStageChanged.Broadcast();
}

UUsdPrimTwin* AUsdStageActor::GetRootPrimTwin()
{
	if ( !RootUsdTwin )
	{
		FScopedUnrealAllocs Allocs;

		// Be careful not to give it a name, as there could be multiple of these on the transient package.
		// It needs to be public or else FArchiveReplaceOrClearExternalReferences will reset our property
		// whenever it is used from UEngine::CopyPropertiesForUnrelatedObjects for blueprint recompilation (if we're a blueprint class)
		RootUsdTwin = NewObject<UUsdPrimTwin>( GetTransientPackage(), NAME_None, DefaultObjFlag | RF_Public );
	}

	return RootUsdTwin;
}

void AUsdStageActor::Refresh() const
{
	OnTimeChanged.Broadcast();
}

void AUsdStageActor::ReloadAnimations()
{
	TRACE_CPUPROFILER_EVENT_SCOPE( AUsdStageActor::ReloadAnimations );

	if ( !UsdStage )
	{
		return;
	}

	// Don't check for full authority here because even if we can't write back to the stage (i.e. during PIE) we still
	// want to listen to it and have valid level sequences
	if ( !IsTemplate() )
	{
		bool bLevelSequenceEditorWasOpened = false;
		if (LevelSequence)
		{
			// The sequencer won't update on its own, so let's at least force it closed
#if WITH_EDITOR
			if ( GIsEditor && GEditor )
			{
				bLevelSequenceEditorWasOpened = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->CloseAllEditorsForAsset(LevelSequence) > 0;
			}
#endif // WITH_EDITOR
		}

		// We need to guarantee we'll record our change of LevelSequence into the transaction, as Init() will create a new one
		Modify();

		LevelSequence = LevelSequenceHelper.Init( UsdStage );
		LevelSequenceHelper.BindToUsdStageActor( this );

#if WITH_EDITOR
		if (GIsEditor && GEditor && LevelSequence && bLevelSequenceEditorWasOpened)
		{
			GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(LevelSequence);
		}
#endif // WITH_EDITOR
	}
}

#if WITH_EDITOR

void AUsdStageActor::PostEditChangeProperty( FPropertyChangedEvent& PropertyChangedEvent )
{
	// For handling root layer changes via direct changes to properties we want to go through OnObjectPropertyChanged -> HandlePropertyChangedEvent ->
	// -> SetRootLayer (which checks whether this stage is already opened or not) -> PostRegisterAllComponents.
	// We need to intercept PostEditChangeProperty too because in the editor any call to PostEditChangeProperty can also *directly* trigger
	// PostRegister/UnregisterAllComponents which would have sidestepped our checks in SetRootLayer.
	// Note that any property change event would also end up calling our intended path via OnObjectPropertyChanged, this just prevents us from loading
	// the same stage again if we don't need to.

	bIsModifyingAProperty = true;
	Super::PostEditChangeProperty( PropertyChangedEvent );
}

void AUsdStageActor::PostTransacted(const FTransactionObjectEvent& TransactionEvent)
{
	const TArray<FName>& ChangedProperties = TransactionEvent.GetChangedProperties();

	if ( TransactionEvent.HasPendingKillChange() )
	{
		// Fires when being deleted in editor, redo delete
		if ( IsPendingKill() )
		{
			CloseUsdStage();
		}
		// This fires when being spawned in an existing level, undo delete, redo spawn
		else
		{
			OpenUsdStage();
		}
	}

	// If we're in the persistent level don't do anything, because hiding/showing the persistent level doesn't
	// cause actors to load/unload like it does if they're in sublevels
	ULevel* CurrentLevel = GetLevel();
	if ( CurrentLevel && !CurrentLevel->IsPersistentLevel() )
	{
		// If we're in a sublevel that is hidden, we'll respond to the generated PostUnregisterAllComponent call
		// and unload our spawned actors/assets, so let's close/open the stage too
		if ( ChangedProperties.Contains( GET_MEMBER_NAME_CHECKED( AActor, bHiddenEdLevel ) ) ||
			 ChangedProperties.Contains( GET_MEMBER_NAME_CHECKED( AActor, bHiddenEdLayer ) ) ||
			 ChangedProperties.Contains( GET_MEMBER_NAME_CHECKED( AActor, bHiddenEd ) ) )
		{
			if ( IsHiddenEd() )
			{
				CloseUsdStage();
			}
			else
			{
				OpenUsdStage();
			}
		}
	}

	if (TransactionEvent.GetEventType() == ETransactionObjectEventType::UndoRedo)
	{
		// PostTransacted marks the end of the undo/redo cycle, so reset this bool so that we can resume
		// listening to PostRegister/PostUnregister calls
		bIsUndoRedoing = false;

		// UsdStageStore can't be a UPROPERTY, so we have to make sure that it
		// is kept in sync with the state of RootLayer, because LoadUsdStage will
		// do the job of clearing our instanced actors/components if the path is empty
		if (ChangedProperties.Contains(GET_MEMBER_NAME_CHECKED(AUsdStageActor, RootLayer)))
		{
			// Changed the path, so we need to reopen the correct stage
			CloseUsdStage();
			OpenUsdStage();
			ReloadAnimations();
		}
		else if (ChangedProperties.Contains(GET_MEMBER_NAME_CHECKED(AUsdStageActor, Time)))
		{
			Refresh();

			// Sometimes when we undo/redo changes that modify SkinnedMeshComponents, their render state is not correctly updated which can show some
			// very garbled meshes. Here we workaround that by recreating all those render states manually
			const bool bRecurive = true;
			GetRootPrimTwin()->Iterate([](UUsdPrimTwin& PrimTwin)
			{
				if ( USkinnedMeshComponent* Component = Cast<USkinnedMeshComponent>( PrimTwin.GetSceneComponent() ) )
				{
					FRenderStateRecreator RecreateRenderState{ Component };
				}
			}, bRecurive);
		}
	}

	// Fire OnObjectTransacted so that multi-user can track our transactions
	Super::PostTransacted( TransactionEvent );
}

void AUsdStageActor::PreEditChange( FProperty* PropertyThatWillChange )
{
	// If we're just editing some other actor property like Time or anything else, we will get
	// PostRegister/Unregister calls in the editor due to AActor::PostEditChangeProperty *and* AActor::PreEditChange.
	// Here we determine in which cases we should ignore those PostRegister/Unregister calls by using the
	// bIsModifyingAProperty flag
	if ( !IsActorBeingDestroyed() )
	{
		if ( ( GEditor && GEditor->bIsSimulatingInEditor && GetWorld() != nullptr ) || ReregisterComponentsWhenModified() )
		{
			// PreEditChange gets called for actor lifecycle functions too (like if the actor transacts on undo/redo).
			// In those cases we will have nullptr PropertyThatWillChange, and we don't want to block our PostRegister/Unregister
			// functions. We only care about blocking the calls triggered by AActor::PostEditChangeProperty and AActor::PreEditChange
			if ( PropertyThatWillChange )
			{
				bIsModifyingAProperty = true;
			}
		}
	}

	Super::PreEditChange( PropertyThatWillChange );
}

void AUsdStageActor::PreEditUndo()
{
	bIsUndoRedoing = true;

	Super::PreEditUndo();
}

void AUsdStageActor::HandleTransactionStateChanged( const FTransactionContext& InTransactionContext, const ETransactionStateEventType InTransactionState )
{
	if ( InTransactionState == ETransactionStateEventType::TransactionFinalized ||
		 InTransactionState == ETransactionStateEventType::UndoRedoFinalized ||
		 InTransactionState == ETransactionStateEventType::TransactionCanceled )
	{
		OldRootLayer = RootLayer;
	}
}

#endif // WITH_EDITOR

void AUsdStageActor::PostDuplicate( bool bDuplicateForPIE )
{
	Super::PostDuplicate( bDuplicateForPIE );

	// Setup for the very first frame when we duplicate into PIE, or else we will just show a T-pose
	if ( bDuplicateForPIE )
	{
		OpenUsdStage();
		AnimatePrims();
	}
}

void AUsdStageActor::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

	if (Ar.GetPortFlags() & PPF_DuplicateForPIE)
	{
		// We want to duplicate these properties for PIE only, as they are required to animate and listen to notices
		Ar << LevelSequence;
		Ar << RootUsdTwin;
		Ar << PrimsToAnimate;
		Ar << ObjectsToWatch;
		Ar << BlendShapesByPath;
		Ar << MaterialToPrimvarToUVIndex;
		Ar << bIsTransitioningIntoPIE;
	}
}

void AUsdStageActor::Destroyed()
{
	// This is fired before the actor is actually deleted or components/actors are detached.
	// We modify our child actors here because they will be detached by UWorld::DestroyActor before they're modified. Later,
	// on AUsdStageActor::Reset (called from PostTransacted), we would Modify() these actors, but if their first modify is in
	// this detached state, they're saved to the transaction as being detached from us. If we undo that transaction,
	// they will be restored as detached, which we don't want, so here we make sure they are first recorded as attached.

	TArray<AActor*> ChildActors;
	GetAttachedActors( ChildActors );

	for ( AActor* Child : ChildActors )
	{
		Child->Modify();
	}

	Super::Destroyed();
}

void AUsdStageActor::PostActorCreated()
{
	Super::PostActorCreated();

#if WITH_EDITOR
	// We can't load stage when recompiling our blueprint because blueprint recompilation is not a transaction. We're forced
	// to reuse the existing spawned components, actors and prim twins instead ( which we move over on OnObjectsReplaced ), or
	// we'd get tons of undo/redo bugs.
	if ( UBlueprintGeneratedClass* BPClass = Cast<UBlueprintGeneratedClass>( GetClass() ) )
	{
		if ( FRecompilationTracker::IsBeingCompiled( Cast<UBlueprint>( BPClass->ClassGeneratedBy ) ) )
		{
			return;
		}
	}
#endif // WITH_EDITOR

	// This is in charge of:
	// - Loading the stage when we open a blueprint editor for a blueprint that derives the AUsdStageActor
	// - Loading the stage when we release the mouse and drop the blueprint onto the level
	if ( HasAuthorityOverStage()
#if WITH_EDITOR
		&& !bIsEditorPreviewActor
#endif // WITH_EDITOR
	)
	{
		LoadUsdStage();
	}
}

void AUsdStageActor::PostRegisterAllComponents()
{
	Super::PostRegisterAllComponents();

#if WITH_EDITOR
	// Prevent loading on bHiddenEdLevel because PostRegisterAllComponents gets called in the process of hiding our level, if we're in the persistent level.
	if ( bIsEditorPreviewActor || bHiddenEdLevel )
	{
		return;
	}
#endif // WITH_EDITOR

	// When we add a sublevel the very first time (i.e. when it is associating) it may still be invisible, but we should load the stage anyway because by
	// default it will become visible shortly after this call. On subsequent postregisters, if our level is invisible there is no point to loading our stage,
	// as our spawned actors/components should be invisible too
	ULevel* Level = GetLevel();
	const bool bIsLevelHidden = !Level || ( !Level->bIsVisible && !Level->bIsAssociatingLevel );

	// This may say fail if our stage happened to not spawn any components, actors or assets, but by that
	// point "being loaded" doesn't really mean anything anyway
	const bool bStageIsLoaded = UsdStage && ( ( RootUsdTwin && RootUsdTwin->GetSceneComponent() != nullptr ) || ( AssetCache && AssetCache->GetNumAssets() > 0 ) );

	// Blocks loading stage when going into PIE, if we already have something loaded (we'll want to duplicate stuff instead).
	// We need to allow loading when going into PIE when we have nothing loaded yet because the MovieRenderQueue (or other callers)
	// may directly trigger PIE sessions providing an override world. Without this exception a map saved with a loaded stage
	// wouldn't load it at all when opening the level in that way
	UWorld* World = GetWorld();
	if ( bIsTransitioningIntoPIE && bStageIsLoaded && ( !World || World->WorldType == EWorldType::PIE ) )
	{
		return;
	}

	// We get an inactive world when dragging a ULevel asset
	// This is just hiding though, so we shouldn't actively load/unload anything
	if ( IsTemplate() || !World || World->WorldType == EWorldType::Inactive || bIsLevelHidden || bIsModifyingAProperty || bIsUndoRedoing )
	{
		return;
	}

	// Send this before we load the stage so that we know SUSDStage is synced to a potential OnStageChanged broadcast
	OnActorLoaded.Broadcast( this );

	LoadUsdStage();
}

void AUsdStageActor::PostUnregisterAllComponents()
{
	Super::PostUnregisterAllComponents();

#if WITH_EDITOR
	if ( bIsEditorPreviewActor )
	{
		return;
	}
#endif // WITH_EDITOR

	const bool bStageIsLoaded = UsdStage && ( ( RootUsdTwin && RootUsdTwin->GetSceneComponent() != nullptr ) || ( AssetCache && AssetCache->GetNumAssets() > 0 ) );

	UWorld* World = GetWorld();
	if ( bIsTransitioningIntoPIE && bStageIsLoaded && ( !World || World->WorldType == EWorldType::PIE ) )
	{
		return;
	}

	// We get an inactive world when dragging a ULevel asset
	// Unlike on PostRegister, we still want to unload our stage if our world is nullptr, as that likely means we were in
	// a sublevel that got unloaded
	if ( IsTemplate() || IsEngineExitRequested() || ( World && World->WorldType == EWorldType::Inactive ) || bIsModifyingAProperty || bIsUndoRedoing )
	{
		return;
	}

	UnloadUsdStage();
}

void AUsdStageActor::OnPreUsdImport( FString FilePath )
{
	if ( !UsdStage || !HasAuthorityOverStage() )
	{
		return;
	}

	// Stop listening to events because a USD import may temporarily modify the stage (e.g. when importing with
	// a different MetersPerUnit value), and we don't want to respond to the notices in the meantime
	FString RootPath = UsdStage.GetRootLayer().GetRealPath();
	FPaths::NormalizeFilename( RootPath );
	if ( RootPath == FilePath )
	{
		StopListeningToUsdNotices();
	}
}

void AUsdStageActor::OnPostUsdImport( FString FilePath )
{
	if ( !UsdStage || !HasAuthorityOverStage() )
	{
		return;
	}

	// Resume listening to events
	FString RootPath = UsdStage.GetRootLayer().GetRealPath();
	FPaths::NormalizeFilename( RootPath );
	if ( RootPath == FilePath )
	{
		ResumeListeningToUsdNotices();
	}
}

void AUsdStageActor::UpdateSpawnedObjectsTransientFlag(bool bTransient)
{
	if (!RootUsdTwin)
	{
		return;
	}

	EObjectFlags Flag = bTransient ? EObjectFlags::RF_Transient : EObjectFlags::RF_NoFlags;
	TFunction<void(UUsdPrimTwin&)> UpdateTransient = [=](UUsdPrimTwin& PrimTwin)
	{
		if (AActor* SpawnedActor = PrimTwin.SpawnedActor.Get())
		{
			SpawnedActor->ClearFlags(EObjectFlags::RF_Transient);
			SpawnedActor->SetFlags(Flag);
		}

		if (USceneComponent* Component = PrimTwin.SceneComponent.Get())
		{
			Component->ClearFlags(EObjectFlags::RF_Transient);
			Component->SetFlags(Flag);

			if (AActor* ComponentOwner = Component->GetOwner())
			{
				ComponentOwner->ClearFlags(EObjectFlags::RF_Transient);
				ComponentOwner->SetFlags(Flag);
			}
		}
	};

	const bool bRecursive = true;
	GetRootPrimTwin()->Iterate(UpdateTransient, bRecursive);
}

void AUsdStageActor::OnUsdPrimTwinDestroyed( const UUsdPrimTwin& UsdPrimTwin )
{
	PrimsToAnimate.Remove( UsdPrimTwin.PrimPath );

	UObject* WatchedObject = UsdPrimTwin.SpawnedActor.IsValid() ? (UObject*)UsdPrimTwin.SpawnedActor.Get() : (UObject*)UsdPrimTwin.SceneComponent.Get();
	ObjectsToWatch.Remove( WatchedObject );

	LevelSequenceHelper.RemovePrim( UsdPrimTwin );
}

void AUsdStageActor::OnObjectPropertyChanged( UObject* ObjectBeingModified, FPropertyChangedEvent& PropertyChangedEvent )
{
	if ( ObjectBeingModified == this )
	{
		HandlePropertyChangedEvent( PropertyChangedEvent );
		return;
	}

	// Don't modify the stage if we're in PIE
	if ( !HasAuthorityOverStage() )
	{
		return;
	}

	// This transient object is owned by us but it doesn't have the multi user tag. If we're not in a transaction
	// where we're spawning objects and components, traverse our hierarchy and tag everything that needs it.
	// We avoid the RootLayer change transaction because if we tagged our spawns then the actual spawning would be
	// replicated, and we want other clients to spawn their own actors and components instead
	if ( RootLayer.FilePath == OldRootLayer.FilePath && FUsdStageActorImpl::ObjectNeedsMultiUserTag( ObjectBeingModified, this ) )
	{
		TSet<UObject*> VisitedObjects;
		FUsdStageActorImpl::WhitelistComponentHierarchy( GetRootComponent(), VisitedObjects );
	}

	UObject* PrimObject = ObjectBeingModified;

	if ( !ObjectsToWatch.Contains( ObjectBeingModified ) )
	{
		if ( AActor* ActorBeingModified = Cast< AActor >( ObjectBeingModified ) )
		{
			if ( !ObjectsToWatch.Contains( ActorBeingModified->GetRootComponent() ) )
			{
				return;
			}
			else
			{
				PrimObject = ActorBeingModified->GetRootComponent();
			}
		}
		else
		{
			return;
		}
	}

	FString PrimPath = ObjectsToWatch[ PrimObject ];

	if ( UUsdPrimTwin* UsdPrimTwin = GetRootPrimTwin()->Find( PrimPath ) )
	{
		// Update prim from UE
		USceneComponent* PrimSceneComponent = UsdPrimTwin->SceneComponent.Get();

		if ( !PrimSceneComponent && UsdPrimTwin->SpawnedActor.IsValid() )
		{
			PrimSceneComponent = UsdPrimTwin->SpawnedActor->GetRootComponent();
		}

		if ( PrimSceneComponent )
		{
			if ( UsdStage )
			{
				// This block is important, as it not only prevents us from getting into infinite loops with the USD notices,
				// but it also guarantees that if we have an object property change, the corresponding stage notice is not also
				// independently saved to the transaction via the UUsdTransactor, which would be duplication
				FScopedBlockNoticeListening BlockNotices( this );

				UE::FUsdPrim UsdPrim = UsdStage.GetPrimAtPath( UE::FSdfPath( *PrimPath ) );

				// We want to keep component visibilities in sync with USD, which uses inherited visibilities
				// To accomplish that while blocking notices we must always propagate component visibility changes manually.
				// This part is effectively the same as calling pxr::UsdGeomImageable::MakeVisible/Invisible.
				if ( PropertyChangedEvent.GetPropertyName() == TEXT( "bVisible" ) )
				{
					PrimSceneComponent->Modify();

					const bool bVisible = PrimSceneComponent->GetVisibleFlag();
					if ( bVisible )
					{
						FUsdStageActorImpl::MakeVisible( *UsdPrimTwin, UsdStage );
					}
					else
					{
						FUsdStageActorImpl::MakeInvisible( *UsdPrimTwin );
					}
				}

#if USE_USD_SDK
				UnrealToUsd::ConvertSceneComponent( UsdStage, PrimSceneComponent, UsdPrim );

				if ( UMeshComponent* MeshComponent = Cast< UMeshComponent >( PrimSceneComponent ) )
				{
					UnrealToUsd::ConvertMeshComponent( UsdStage, MeshComponent, UsdPrim );
				}
				else if ( UsdPrim && UsdPrim.IsA( TEXT( "Camera" ) ) )
				{
					// Our component may be pointing directly at a camera component in case we recreated an exported
					// ACineCameraActor (see UE-120826)
					if ( UCineCameraComponent* RecreatedCameraComponent = Cast<UCineCameraComponent>( PrimSceneComponent ) )
					{
						UnrealToUsd::ConvertCameraComponent( UsdStage, RecreatedCameraComponent, UsdPrim );
					}
					// Or it could have been just a generic Camera prim, at which case we'll have spawned an entire new
					// ACineCameraActor for it. In this scenario our prim twin is pointing at the root component, so we need
					// to dig to the actual UCineCameraComponent to write out the camera data.
					// We should only do this when the Prim actually corresponds to the Camera though, or else we'll also catch
					// the prim/component pair that corresponds to the root scene component in case we recreated an exported
					// ACineCameraActor.
					else if ( ACineCameraActor* CameraActor = Cast<ACineCameraActor>( PrimSceneComponent->GetOwner() ) )
					{
						if ( UCineCameraComponent* CameraComponent = CameraActor->GetCineCameraComponent() )
						{
							UnrealToUsd::ConvertCameraComponent( UsdStage, CameraComponent, UsdPrim );
						}
					}
				}
				else if ( ALight* LightActor = Cast<ALight>( PrimSceneComponent->GetOwner() ) )
				{
					if ( ULightComponent* LightComponent = LightActor->GetLightComponent() )
					{
						UnrealToUsd::ConvertLightComponent( *LightComponent, UsdPrim, UsdUtils::GetDefaultTimeCode() );

						if ( UDirectionalLightComponent* DirectionalLight = Cast<UDirectionalLightComponent>( LightComponent ) )
						{
							UnrealToUsd::ConvertDirectionalLightComponent( *DirectionalLight, UsdPrim, UsdUtils::GetDefaultTimeCode() );
						}
						else if ( URectLightComponent* RectLight = Cast<URectLightComponent>( LightComponent ) )
						{
							UnrealToUsd::ConvertRectLightComponent( *RectLight, UsdPrim, UsdUtils::GetDefaultTimeCode() );
						}
						else if ( UPointLightComponent* PointLight = Cast<UPointLightComponent>( LightComponent ) )
						{
							UnrealToUsd::ConvertPointLightComponent( *PointLight, UsdPrim, UsdUtils::GetDefaultTimeCode() );

							if ( USpotLightComponent* SpotLight = Cast<USpotLightComponent>( LightComponent ) )
							{
								UnrealToUsd::ConvertSpotLightComponent( *SpotLight, UsdPrim, UsdUtils::GetDefaultTimeCode() );
							}
						}
					}
				}
				// In contrast to the other light types, the USkyLightComponent is the root component of the ASkyLight
				else if ( USkyLightComponent* SkyLightComponent = Cast<USkyLightComponent>( PrimSceneComponent ) )
				{
					UnrealToUsd::ConvertLightComponent( *SkyLightComponent, UsdPrim, UsdUtils::GetDefaultTimeCode() );
					UnrealToUsd::ConvertSkyLightComponent( *SkyLightComponent, UsdPrim, UsdUtils::GetDefaultTimeCode() );
				}
#endif // #if USE_USD_SDK

				// Update stage window in case any of our component changes trigger USD stage changes
				this->OnPrimChanged.Broadcast( PrimPath, false );
			}
		}
	}
}

void AUsdStageActor::HandlePropertyChangedEvent( FPropertyChangedEvent& PropertyChangedEvent )
{
	// Handle property changed events with this function (called from our OnObjectPropertyChanged delegate) instead of overriding PostEditChangeProperty because replicated
	// multi-user transactions directly broadcast OnObjectPropertyChanged on the properties that were changed, instead of making PostEditChangeProperty events.
	// Note that UObject::PostEditChangeProperty ends up broadcasting OnObjectPropertyChanged anyway, so this works just the same as before.
	// see ConcertClientTransactionBridge.cpp, function ConcertClientTransactionBridgeUtil::ProcessTransactionEvent

	FProperty* PropertyThatChanged = PropertyChangedEvent.MemberProperty;
	const FName PropertyName = PropertyThatChanged ? PropertyThatChanged->GetFName() : NAME_None;

	if ( PropertyName == GET_MEMBER_NAME_CHECKED( AUsdStageActor, RootLayer ) )
	{
		SetRootLayer( RootLayer.FilePath );
	}
	else if ( PropertyName == GET_MEMBER_NAME_CHECKED( AUsdStageActor, Time ) )
	{
		SetTime( Time );
	}
	else if ( PropertyName == GET_MEMBER_NAME_CHECKED( AUsdStageActor, InitialLoadSet ) )
	{
		SetInitialLoadSet( InitialLoadSet );
	}
	else if ( PropertyName == GET_MEMBER_NAME_CHECKED( AUsdStageActor, PurposesToLoad ) )
	{
		SetPurposesToLoad( PurposesToLoad );
	}
	else if ( PropertyName == GET_MEMBER_NAME_CHECKED( AUsdStageActor, RenderContext ) )
	{
		SetRenderContext( RenderContext );
	}

	bIsModifyingAProperty = false;
}

bool AUsdStageActor::HasAuthorityOverStage() const
{
#if WITH_EDITOR
	if ( GIsEditor ) // Don't check for world in Standalone: The game world is the only one there, so it's OK if we have authority while in it
	{
		// In the editor we have to prevent actors in PIE worlds from having authority
		return !IsTemplate() && ( !GetWorld() || !GetWorld()->IsGameWorld() );
	}
#endif // WITH_EDITOR

	return !IsTemplate();
}

void AUsdStageActor::LoadAsset( FUsdSchemaTranslationContext& TranslationContext, const UE::FUsdPrim& Prim )
{
	TRACE_CPUPROFILER_EVENT_SCOPE( AUsdStageActor::LoadAsset );

	// Mark the assets as non transactional so that they don't get serialized in the transaction buffer
	TGuardValue< EObjectFlags > ContextFlagsGuard( TranslationContext.ObjectFlags, TranslationContext.ObjectFlags & ~RF_Transactional );

	FString PrimPath;
#if USE_USD_SDK
	PrimPath = UsdToUnreal::ConvertPath( Prim.GetPrimPath() );
#endif // #if USE_USD_SDK

	if ( AssetCache )
	{
		AssetCache->RemoveAssetPrimLink( PrimPath );
	}

	IUsdSchemasModule& UsdSchemasModule = FModuleManager::Get().LoadModuleChecked< IUsdSchemasModule >( TEXT("USDSchemas") );
	if ( TSharedPtr< FUsdSchemaTranslator > SchemaTranslator = UsdSchemasModule.GetTranslatorRegistry().CreateTranslatorForSchema( TranslationContext.AsShared(), UE::FUsdTyped( Prim ) ) )
	{
		TRACE_CPUPROFILER_EVENT_SCOPE( AUsdStageActor::CreateAssetsForPrim );
		SchemaTranslator->CreateAssets();
	}

	TranslationContext.CompleteTasks(); // Finish the asset tasks before moving on
}

void AUsdStageActor::LoadAssets( FUsdSchemaTranslationContext& TranslationContext, const UE::FUsdPrim& StartPrim )
{
	TRACE_CPUPROFILER_EVENT_SCOPE( AUsdStageActor::LoadAssets );

	// Mark the assets as non transactional so that they don't get serialized in the transaction buffer
	TGuardValue< EObjectFlags > ContextFlagsGuard( TranslationContext.ObjectFlags, TranslationContext.ObjectFlags & ~RF_Transactional );

	// Clear existing prim/asset association
	if ( AssetCache )
	{
		FString StartPrimPath = StartPrim.GetPrimPath().GetString();
		TSet<FString> PrimPathsToRemove;
		for ( const TPair< FString, UObject* >& PrimPathToAssetIt : AssetCache->GetAssetPrimLinks() )
		{
			const FString& PrimPath = PrimPathToAssetIt.Key;
			if ( PrimPath.StartsWith( StartPrimPath ) || PrimPath == StartPrimPath )
			{
				PrimPathsToRemove.Add( PrimPath );
			}
		}
		for ( const FString& PrimPathToRemove : PrimPathsToRemove )
		{
			AssetCache->RemoveAssetPrimLink( PrimPathToRemove );
		}
	}

	IUsdSchemasModule& UsdSchemasModule = FModuleManager::Get().LoadModuleChecked< IUsdSchemasModule >( TEXT("USDSchemas") );

	auto CreateAssetsForPrims = [ &UsdSchemasModule, &TranslationContext ]( const TArray< UE::FUsdPrim >& AllPrimAssets, FSlowTask& Progress )
	{
		TRACE_CPUPROFILER_EVENT_SCOPE( AUsdStageActor::CreateAssetsForPrims );

		for ( const UE::FUsdPrim& UsdPrim : AllPrimAssets )
		{
			Progress.EnterProgressFrame(1.f);

			if ( TSharedPtr< FUsdSchemaTranslator > SchemaTranslator = UsdSchemasModule.GetTranslatorRegistry().CreateTranslatorForSchema( TranslationContext.AsShared(), UE::FUsdTyped( UsdPrim ) ) )
			{
				TRACE_CPUPROFILER_EVENT_SCOPE( AUsdStageActor::CreateAssetsForPrim );
				SchemaTranslator->CreateAssets();
			}
		}

		TranslationContext.CompleteTasks(); // Finish the assets tasks before moving on
	};

	auto PruneChildren = [ &UsdSchemasModule, &TranslationContext ]( const UE::FUsdPrim& UsdPrim ) -> bool
	{
		if ( TSharedPtr< FUsdSchemaTranslator > SchemaTranslator = UsdSchemasModule.GetTranslatorRegistry().CreateTranslatorForSchema( TranslationContext.AsShared(), UE::FUsdTyped( UsdPrim ) ) )
		{
			return SchemaTranslator->CollapsesChildren( FUsdSchemaTranslator::ECollapsingType::Assets );
		}

		return false;
	};

	// Load materials first since meshes are referencing them
	TArray< UE::FUsdPrim > AllPrimAssets = UsdUtils::GetAllPrimsOfType( StartPrim, TEXT( "UsdShadeMaterial" ) );
	{
		FScopedSlowTask MaterialsProgress( AllPrimAssets.Num(), LOCTEXT("CreateMaterials", "Creating materials"));
		CreateAssetsForPrims( AllPrimAssets, MaterialsProgress );
	}

	// Load everything else (including meshes)
	AllPrimAssets = UsdUtils::GetAllPrimsOfType( StartPrim, TEXT( "UsdSchemaBase" ), PruneChildren, { TEXT( "UsdShadeMaterial" ) } );
	{
		FScopedSlowTask AssetsProgress( AllPrimAssets.Num(), LOCTEXT("CreateAssets", "Creating assets"));
		CreateAssetsForPrims( AllPrimAssets, AssetsProgress );
	}
}

void AUsdStageActor::AnimatePrims()
{
	// Don't try to animate if we don't have a stage opened
	if ( !UsdStage )
	{
		return;
	}

	TSharedRef< FUsdSchemaTranslationContext > TranslationContext = FUsdStageActorImpl::CreateUsdSchemaTranslationContext( this, GetRootPrimTwin()->PrimPath );

	for ( const FString& PrimToAnimate : PrimsToAnimate )
	{
		UE::FSdfPath PrimPath( *PrimToAnimate );

		IUsdSchemasModule& SchemasModule = FModuleManager::Get().LoadModuleChecked< IUsdSchemasModule >( "USDSchemas" );
		if ( TSharedPtr< FUsdSchemaTranslator > SchemaTranslator = SchemasModule.GetTranslatorRegistry().CreateTranslatorForSchema( TranslationContext, UE::FUsdTyped( UsdStage.GetPrimAtPath( PrimPath ) ) ) )
		{
			if ( UUsdPrimTwin* UsdPrimTwin = GetRootPrimTwin()->Find( PrimToAnimate ) )
			{
				SchemaTranslator->UpdateComponents( UsdPrimTwin->SceneComponent.Get() );
			}
		}
	}

	TranslationContext->CompleteTasks();

#if WITH_EDITOR
	if ( GIsEditor && GEditor )
	{
		GEditor->BroadcastLevelActorListChanged();
		GEditor->RedrawLevelEditingViewports();
	}
#endif
}

FScopedBlockNoticeListening::FScopedBlockNoticeListening( AUsdStageActor* InStageActor )
{
	StageActor = InStageActor;
	if ( InStageActor )
	{
		StageActor->StopListeningToUsdNotices();
	}
}

FScopedBlockNoticeListening::~FScopedBlockNoticeListening()
{
	if ( AUsdStageActor* StageActorPtr = StageActor.Get() )
	{
		StageActorPtr->ResumeListeningToUsdNotices();
	}
}

#undef LOCTEXT_NAMESPACE