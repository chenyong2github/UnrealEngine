// Copyright Epic Games, Inc. All Rights Reserved.

#include "USDStageActor.h"

#include "USDConversionUtils.h"
#include "USDErrorUtils.h"
#include "USDGeomMeshTranslator.h"
#include "USDGeomXformableTranslator.h"
#include "USDListener.h"
#include "USDLog.h"
#include "USDPrimConversion.h"
#include "USDSchemaTranslator.h"
#include "USDSchemasModule.h"
#include "USDSkelRootTranslator.h"
#include "USDTypesConversion.h"
#include "UnrealUSDWrapper.h"

#include "UsdWrappers/UsdAttribute.h"
#include "UsdWrappers/UsdGeomXformable.h"
#include "UsdWrappers/SdfLayer.h"

#include "Async/ParallelFor.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
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
			StageActor->GetUsdStage(),
			StageActor->PrimPathsToAssets,
			StageActor->AssetsCache,
			&StageActor->BlendShapesByPath);

		TranslationContext->Level = StageActor->GetLevel();
		TranslationContext->ObjectFlags = DefaultObjFlag;
		TranslationContext->Time = StageActor->GetTime();
		TranslationContext->PurposesToLoad = (EUsdPurpose) StageActor->PurposesToLoad;
		TranslationContext->MaterialToPrimvarToUVIndex = &StageActor->MaterialToPrimvarToUVIndex;

		// Its more convenient to toggle between variants using the USDStage window, as opposed to parsing LODs
		TranslationContext->bAllowInterpretingLODs = false;

		// No point in baking these UAnimSequence assets if we're going to be sampling the stage in real time anyway
		TranslationContext->bAllowParsingSkeletalAnimations = false;

		UE::FSdfPath UsdPrimPath( *PrimPath );
		UUsdPrimTwin* ParentUsdPrimTwin = StageActor->RootUsdTwin->Find( UsdPrimPath.GetParentPath().GetString() );

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

		TArray<UObject*> ObjectsToDelete;
		const bool bRecursive = true;
		StageActor->RootUsdTwin->Iterate( [ &ObjectsToDelete ]( UUsdPrimTwin& PrimTwin )
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

		if ( GIsEditor && GEditor ) // Make sure we're not in standalone either
		{
			GEditor->NoteSelectionChange();
		}
#endif // WITH_EDITOR
	}

	static void CloseEditorsForAssets( const TMap< FString, UObject* >& AssetsCache )
	{
#if WITH_EDITOR
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
#endif // WITH_EDITOR
	}
};

AUsdStageActor::AUsdStageActor()
	: InitialLoadSet( EUsdInitialLoadSet::LoadAll )
	, PurposesToLoad((int32) EUsdPurpose::Proxy)
	, Time( 0.0f )
	, LevelSequenceHelper(this)
{
	SceneComponent = CreateDefaultSubobject< USceneComponent >( TEXT("SceneComponent0") );
	SceneComponent->Mobility = EComponentMobility::Static;

	RootComponent = SceneComponent;

	RootUsdTwin = NewObject<UUsdPrimTwin>(this, TEXT("RootUsdTwin"), DefaultObjFlag);
	RootUsdTwin->PrimPath = TEXT( "/" );

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

		if ( GIsEditor ) // Make sure we're not in standalone either
		{
			// We can't use PostLoad to trigger LoadUsdStage when first loading a saved level because LoadUsdStage may trigger
			// Rename() calls, and that is not allowed.
			FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor");
			LevelEditorModule.OnMapChanged().AddUObject(this, &AUsdStageActor::OnMapChanged);
		}
		FEditorDelegates::BeginPIE.AddUObject(this, &AUsdStageActor::OnBeginPIE);
		FEditorDelegates::PostPIEStarted.AddUObject(this, &AUsdStageActor::OnPostPIEStarted);

		FWorldDelegates::LevelAddedToWorld.AddUObject( this, &AUsdStageActor::OnLevelAddedToWorld );
		FWorldDelegates::LevelRemovedFromWorld.AddUObject( this, &AUsdStageActor::OnLevelRemovedFromWorld );

		FUsdDelegates::OnPostUsdImport.AddUObject( this, &AUsdStageActor::OnPostUsdImport );
		FUsdDelegates::OnPreUsdImport.AddUObject( this, &AUsdStageActor::OnPreUsdImport );

		// When another client of a multi-user session modifies their version of this actor, the transaction will be replicated here.
		// The multi-user system uses "redo" to apply those transactions, so this is our best chance to respond to events as e.g. neither
		// PostTransacted nor Destroyed get called when the other user deletes the actor
		if ( UTransBuffer* TransBuffer = GUnrealEd ? Cast<UTransBuffer>( GUnrealEd->Trans ) : nullptr )
		{
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
						else if ( ObjectsToWatch.Num() == 0 && AssetsCache.Num() == 0 )
						{
							bool bNeedLoad = true;

							TArray<UE::FUsdStage> OpenedStages = UnrealUSDWrapper::GetAllStagesFromCache();
							for ( const UE::FUsdStage& Stage : OpenedStages )
							{
								if ( FPaths::IsSamePath( Stage.GetRootLayer().GetRealPath(), RootLayer.FilePath ) )
								{
									bNeedLoad = false;
									break;
								}
							}

							if ( bNeedLoad )
							{
								this->LoadUsdStage();
								AUsdStageActor::OnActorLoaded.Broadcast( this );
							}
						}
					}
				}
			);
		}

		FCoreUObjectDelegates::OnObjectPropertyChanged.AddUObject( this, &AUsdStageActor::OnObjectPropertyChanged );
#endif // WITH_EDITOR

		OnTimeChanged.AddUObject( this, &AUsdStageActor::AnimatePrims );

		UsdListener.GetOnPrimsChanged().AddUObject( this, &AUsdStageActor::OnPrimsChanged );

		UsdListener.GetOnLayersChanged().AddLambda(
			[&]( const TArray< FString >& ChangeVec )
			{
				TUniquePtr< TGuardValue< ITransaction* > > SuppressTransaction = nullptr;
				if ( this->GetOutermost()->HasAnyPackageFlags( PKG_PlayInEditor ) )
				{
					SuppressTransaction = MakeUnique< TGuardValue< ITransaction* > >( GUndo, nullptr );
				}

				// Check to see if any layer reloaded. If so, rebuild all of our animations as a single layer changing
				// might propagate timecodes through all level sequences
				for ( const FString& ChangeVecItem : ChangeVec )
				{
					UE_LOG( LogUsd, Verbose, TEXT("Reloading animations because layer '%s' was added/removed/reloaded"), *ChangeVecItem );
					ReloadAnimations();
					return;
				}
			}
		);
	}
}

void AUsdStageActor::OnPrimsChanged( const TMap< FString, bool >& PrimsChangedList )
{
	// Sort paths by length so that we parse the root paths first
	TMap< FString, bool > SortedPrimsChangedList = PrimsChangedList;
	SortedPrimsChangedList.KeySort( []( const FString& A, const FString& B ) -> bool { return A.Len() < B.Len(); } );

	// During PIE, the PIE and the editor world will respond to notices. We have to prevent any PIE
	// objects from being added to the transaction however, or else it will be discarded when finalized.
	// We need to keep the transaction, or else we may end up with actors outside of the transaction
	// system that want to use assets that will be destroyed by it on an undo.
	// Note that we can't just make the spawned components/assets nontransactional because the PIE world will transact too
	TUniquePtr<TGuardValue<ITransaction*>> SuppressTransaction = nullptr;
	if ( this->GetOutermost()->HasAnyPackageFlags( PKG_PlayInEditor ) )
	{
		SuppressTransaction = MakeUnique<TGuardValue<ITransaction*>>(GUndo, nullptr);
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

		auto UnwindToNonCollapsedPrim = [ &PrimChangedInfo, this ]( FUsdSchemaTranslator::ECollapsingType CollapsingType ) -> UE::FSdfPath
		{
			IUsdSchemasModule& UsdSchemasModule = FModuleManager::Get().LoadModuleChecked< IUsdSchemasModule >( TEXT("USDSchemas") );

			TSharedRef< FUsdSchemaTranslationContext > TranslationContext = FUsdStageActorImpl::CreateUsdSchemaTranslationContext( this, PrimChangedInfo.Key );

			UE::FSdfPath UsdPrimPath( *PrimChangedInfo.Key );
			UE::FUsdPrim UsdPrim = GetUsdStage().GetPrimAtPath( UsdPrimPath );

			if ( TSharedPtr< FUsdSchemaTranslator > SchemaTranslator = UsdSchemasModule.GetTranslatorRegistry().CreateTranslatorForSchema( TranslationContext, UE::FUsdTyped( UsdPrim ) ) )
			{
				while ( SchemaTranslator->IsCollapsed( CollapsingType ) )
				{
					UsdPrimPath = UsdPrimPath.GetParentPath();
					UsdPrim = GetUsdStage().GetPrimAtPath( UsdPrimPath );

					TranslationContext = FUsdStageActorImpl::CreateUsdSchemaTranslationContext( this, UsdPrimPath.GetString() );
					SchemaTranslator = UsdSchemasModule.GetTranslatorRegistry().CreateTranslatorForSchema( TranslationContext, UE::FUsdTyped( UsdPrim ) );

					if ( !SchemaTranslator.IsValid() )
					{
						break;
					}
				}
			}

			return UsdPrimPath;
		};

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
			UE::FSdfPath AssetsPrimPath = UnwindToNonCollapsedPrim( FUsdSchemaTranslator::ECollapsingType::Assets );

			TSet< FString >& RefreshedAssets = bIsResync ? ResyncedAssets : UpdatedAssets;

			if ( !IsPathAlreadyProcessed( RefreshedAssets, AssetsPrimPath.GetString() ) )
			{
				TSharedRef< FUsdSchemaTranslationContext > TranslationContext = FUsdStageActorImpl::CreateUsdSchemaTranslationContext( this, AssetsPrimPath.GetString() );

				if ( bIsResync )
				{
					LoadAssets( *TranslationContext, GetUsdStage().GetPrimAtPath( AssetsPrimPath ) );

					// Resyncing also includes "updating" the prim
					UpdatedAssets.Add( AssetsPrimPath.GetString() );
				}
				else
				{
					LoadAsset( *TranslationContext, GetUsdStage().GetPrimAtPath( AssetsPrimPath ) );
				}

				RefreshedAssets.Add( AssetsPrimPath.GetString() );
			}
		}

		// Update components
		{
			UE::FSdfPath ComponentsPrimPath = UnwindToNonCollapsedPrim( FUsdSchemaTranslator::ECollapsingType::Components );

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
		FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor");
		LevelEditorModule.OnMapChanged().RemoveAll(this);
		FEditorDelegates::BeginPIE.RemoveAll(this);
		FEditorDelegates::PostPIEStarted.RemoveAll(this);
		FWorldDelegates::LevelAddedToWorld.RemoveAll(this);
		FWorldDelegates::LevelRemovedFromWorld.RemoveAll(this);
		FUsdDelegates::OnPostUsdImport.RemoveAll(this);
		FUsdDelegates::OnPreUsdImport.RemoveAll(this);
		if ( UTransBuffer* TransBuffer = GUnrealEd ? Cast<UTransBuffer>( GUnrealEd->Trans ) : nullptr )
		{
			TransBuffer->OnRedo().Remove( OnRedoHandle );
		}

		// This clears the SUSDStage window whenever the level we're currently in gets destroyed.
		// Note that this is not called when deleting from the Editor, as the actor goes into the undo buffer.
		// Also note that we don't handle clearing SUSDStage when unloading a level in OnMapChanged because in some situations
		// (e.g. when changing to a level that also uses this level as sublevel) the engine will reuse the level and this
		// same actor, so we can't reset our properties and cache, as they will be reused on the new level.
		OnActorDestroyed.Broadcast();
		UnrealUSDWrapper::EraseStageFromCache( UsdStage );
	}
#endif // WITH_EDITOR
}

USDSTAGE_API void AUsdStageActor::Reset()
{
	Super::Reset();

	Modify();

	FUsdStageActorImpl::DeselectActorsAndComponents( this );
	FUsdStageActorImpl::CloseEditorsForAssets( AssetsCache );

	Clear();
	AssetsCache.Reset();
	BlendShapesByPath.Reset();
	MaterialToPrimvarToUVIndex.Reset();

	if ( LevelSequence )
	{
#if WITH_EDITOR
		GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->CloseAllEditorsForAsset(LevelSequence);
#endif // WITH_EDITOR
		LevelSequence = nullptr;
	}
	Time = 0.f;

	RootUsdTwin->Clear();
	RootUsdTwin->PrimPath = TEXT("/");

#if WITH_EDITOR
	GEditor->BroadcastLevelActorListChanged();
#endif // WITH_EDITOR

	RootLayer.FilePath.Empty();

	UnrealUSDWrapper::EraseStageFromCache( UsdStage );
	UsdStage = UE::FUsdStage();

	OnStageChanged.Broadcast();
}

void AUsdStageActor::StopMonitoringLevelSequence()
{
	LevelSequenceHelper.StopMonitoringChanges();
}

void AUsdStageActor::ResumeMonitoringLevelSequence()
{
	LevelSequenceHelper.StartMonitoringChanges();
}

UUsdPrimTwin* AUsdStageActor::GetOrCreatePrimTwin( const UE::FSdfPath& UsdPrimPath )
{
	const FString PrimPath = UsdPrimPath.GetString();
	const FString ParentPrimPath = UsdPrimPath.GetParentPath().GetString();

	UUsdPrimTwin* UsdPrimTwin = RootUsdTwin->Find( PrimPath );
	UUsdPrimTwin* ParentUsdPrimTwin = RootUsdTwin->Find( ParentPrimPath );

	const UE::FUsdPrim Prim = GetUsdStage().GetPrimAtPath( UsdPrimPath );

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

		UsdPrimTwin->OnDestroyed.AddLambda(
			[ this ]( const UUsdPrimTwin& UsdPrimTwin )
			{
				this->OnUsdPrimTwinDestroyed( UsdPrimTwin );
			} );
	}

	return UsdPrimTwin;
}

UUsdPrimTwin* AUsdStageActor::ExpandPrim( const UE::FUsdPrim& Prim, FUsdSchemaTranslationContext& TranslationContext )
{
	if ( !Prim )
	{
		return nullptr;
	}

	TRACE_CPUPROFILER_EVENT_SCOPE( AUsdStageActor::ExpandPrim );

	UUsdPrimTwin* UsdPrimTwin = GetOrCreatePrimTwin( Prim.GetPrimPath() );

	if ( !UsdPrimTwin )
	{
		return nullptr;
	}

	bool bExpandChilren = true;

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

		bExpandChilren = !SchemaTranslator->CollapsesChildren( FUsdSchemaTranslator::ECollapsingType::Components );
	}

	if ( bExpandChilren )
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
			if ( UUsdPrimTwin* UsdPrimTwin = RootUsdTwin->Find( PrimPath ) )
			{
				UsdPrimTwin->Clear();
			}
		}

		UE::FUsdPrim PrimToExpand = GetUsdStage().GetPrimAtPath( UsdPrimPath );
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

void AUsdStageActor::SetRootLayer( const FString& RootFilePath )
{
	UnrealUSDWrapper::EraseStageFromCache( UsdStage );
	UsdStage = UE::FUsdStage();

	AssetsCache.Reset(); // We've changed USD file, clear the cache
	BlendShapesByPath.Reset();
	MaterialToPrimvarToUVIndex.Reset();

	RootLayer.FilePath = RootFilePath;
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

void AUsdStageActor::SetTime(float InTime)
{
	Time = InTime;

	Refresh();
}

void AUsdStageActor::Clear()
{
	PrimPathsToAssets.Reset();
	ObjectsToWatch.Reset();
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

	if ( FPaths::FileExists( RootLayer.FilePath ) )
	{
		UsdStage = UnrealUSDWrapper::OpenStage( *RootLayer.FilePath, InitialLoadSet );
	}

	if ( UsdStage )
	{
		UsdStage.SetEditTarget( UsdStage.GetRootLayer() );

		UsdListener.Register( UsdStage );

		OnStageChanged.Broadcast();
	}

	UsdUtils::ShowErrorsAndStopMonitoring( FText::Format( LOCTEXT("USDOpenError", "Encountered some errors opening USD file at path '{0}!\nCheck the Output Log for details."), FText::FromString( RootLayer.FilePath ) ) );
}

#if WITH_EDITOR
void AUsdStageActor::OnMapChanged( UWorld* World, EMapChangeType ChangeType )
{
	// This is in charge of loading the stage when we load a level that had a AUsdStageActor saved with a valid root layer filepath.
	// Note that we don't handle here updating the SUSDStage window when our level/world is being destroyed, we do it in the destructor.
	if ( HasAuthorityOverStage() &&
		World && World == GetWorld() && World->GetCurrentLevel() == GetLevel() &&
		( ChangeType == EMapChangeType::LoadMap || ChangeType == EMapChangeType::NewMap ) )
	{
		LoadUsdStage();

		// SUSDStage window needs to update
		OnActorLoaded.Broadcast( this );
	}
}

void AUsdStageActor::OnBeginPIE(bool bIsSimulating)
{
	// Remove transient flag from our spawned actors and components so they can be duplicated for PIE
	const bool bTransient = false;
	UpdateSpawnedObjectsTransientFlag(bTransient);
}

void AUsdStageActor::OnPostPIEStarted(bool bIsSimulating)
{
	// Restore transient flags to our spawned actors and components so they aren't saved otherwise
	const bool bTransient = true;
	UpdateSpawnedObjectsTransientFlag(bTransient);
}

#endif // WITH_EDITOR

void AUsdStageActor::LoadUsdStage()
{
	TRACE_CPUPROFILER_EVENT_SCOPE( AUsdStageActor::LoadUsdStage );

	double StartTime = FPlatformTime::Cycles64();

	FScopedSlowTask SlowTask( 1.f, LOCTEXT( "LoadingUDStage", "Loading USD Stage") );
	SlowTask.MakeDialog();

	Clear();

	FUsdStageActorImpl::DeselectActorsAndComponents( this );

	RootUsdTwin->Clear();
	RootUsdTwin->PrimPath = TEXT("/");

	FScopedUsdMessageLog ScopedMessageLog;

	OpenUsdStage();
	if ( !UsdStage )
	{
		OnStageChanged.Broadcast();
		return;
	}

	ReloadAnimations();

	TSharedRef< FUsdSchemaTranslationContext > TranslationContext = FUsdStageActorImpl::CreateUsdSchemaTranslationContext( this, RootUsdTwin->PrimPath );

	SlowTask.EnterProgressFrame( 0.8f );
	LoadAssets( *TranslationContext, UsdStage.GetPseudoRoot() );

	SlowTask.EnterProgressFrame( 0.2f );
	UpdatePrim( UsdStage.GetPseudoRoot().GetPrimPath(), true, *TranslationContext );

	TranslationContext->CompleteTasks();

	if ( UsdStage.GetRootLayer() )
	{
		SetTime( UsdStage.GetRootLayer().GetStartTimeCode() );
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
			if ( GIsEditor )
			{
				bLevelSequenceEditorWasOpened = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->CloseAllEditorsForAsset(LevelSequence) > 0;
			}
#endif // WITH_EDITOR
		}

		LevelSequence = nullptr;
		LevelSequencesByIdentifier.Reset();

		LevelSequenceHelper.InitLevelSequence(UsdStage);

#if WITH_EDITOR
		if (GIsEditor && GEditor && LevelSequence && bLevelSequenceEditorWasOpened)
		{
			GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(LevelSequence);
		}
#endif // WITH_EDITOR
	}
}

#if WITH_EDITOR
void AUsdStageActor::PostTransacted(const FTransactionObjectEvent& TransactionEvent)
{
	if (TransactionEvent.HasPendingKillChange())
	{
		// Fires when being deleted in editor, redo delete
		if (this->IsPendingKill())
		{
			OnActorDestroyed.Broadcast();
			Reset();
		}
		// This fires when being spawned in an existing level, undo delete, redo spawn
		else
		{
			OnActorLoaded.Broadcast( this );
			OnStageChanged.Broadcast();
		}
	}
	else if (TransactionEvent.GetEventType() == ETransactionObjectEventType::UndoRedo)
	{
		// UsdStageStore can't be a UPROPERTY, so we have to make sure that it
		// is kept in sync with the state of RootLayer, because LoadUsdStage will
		// do the job of clearing our instanced actors/components if the path is empty
		const TArray<FName>& ChangedProperties = TransactionEvent.GetChangedProperties();
		if (ChangedProperties.Contains(GET_MEMBER_NAME_CHECKED(AUsdStageActor, RootLayer)))
		{
			// Changed the path, so we need to reopen to the correct stage
			UnrealUSDWrapper::EraseStageFromCache( UsdStage );
			UsdStage = UE::FUsdStage();
			OnStageChanged.Broadcast();

			ReloadAnimations();
		}
		else if (ChangedProperties.Contains(GET_MEMBER_NAME_CHECKED(AUsdStageActor, Time)))
		{
			Refresh();

			// Sometimes when we undo/redo changes that modify SkinnedMeshComponents, their render state is not correctly updated which can show some
			// very garbled meshes. Here we workaround that by recreating all those render states manually
			const bool bRecurive = true;
			RootUsdTwin->Iterate([](UUsdPrimTwin& PrimTwin)
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

void AUsdStageActor::PostLoad()
{
	Super::PostLoad();

	// This may be reset to nullptr when loading a level or serializing, because the property is Transient
	if (RootUsdTwin == nullptr)
	{
		RootUsdTwin = NewObject<UUsdPrimTwin>(this, TEXT("RootUsdTwin"), DefaultObjFlag);
	}
}

void AUsdStageActor::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

	if (Ar.GetPortFlags() & PPF_DuplicateForPIE)
	{
		// We want to duplicate these properties for PIE only, as they are required to animate and listen to notices
		Ar << LevelSequence;
		Ar << LevelSequencesByIdentifier;
		Ar << RootUsdTwin;
		Ar << PrimsToAnimate;
		Ar << ObjectsToWatch;
		Ar << AssetsCache;
		Ar << PrimPathsToAssets;
		Ar << BlendShapesByPath;
		Ar << MaterialToPrimvarToUVIndex;
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

void AUsdStageActor::OnLevelAddedToWorld( ULevel* Level, UWorld* World )
{
	if ( !HasAuthorityOverStage() )
	{
		return;
	}

	// Load the stage if we're an actor in a level that is being added as a sublevel to the world
	if ( GetLevel() == Level && GetWorld() == World )
	{
		FString PathOnProperty = RootLayer.FilePath;
		FString CurrentStagePath = UsdStage && UsdStage.GetRootLayer() ? UsdStage.GetRootLayer().GetRealPath() : FString();

		FPaths::NormalizeFilename(PathOnProperty);
		FPaths::NormalizeFilename(CurrentStagePath);

		if ( PathOnProperty != CurrentStagePath )
		{
			LoadUsdStage();

			// SUSDStage window needs to update
			OnActorLoaded.Broadcast( this );
		}
	}
}

void AUsdStageActor::OnLevelRemovedFromWorld( ULevel* Level, UWorld* World )
{
	if ( !HasAuthorityOverStage() )
	{
		return;
	}

	// Unload if we're in a level that is being removed from the world.
	// We need to really make sure we're supposed to unload because this function gets called in many scenarios,
	// including right *after* the level has been added to the world (?). For some reason, 'bIsBeingRemoved' is
	// false only for when the level is actually being removed so that's what we check here
	if ( GetLevel() == Level && GetWorld() == World && UsdStage && !Level->bIsBeingRemoved )
	{
		// Clear the SUSDStage window
		OnActorDestroyed.Broadcast();
		Reset();
	}
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
		UsdListener.Block();
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
		UsdListener.Unblock();
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
	RootUsdTwin->Iterate(UpdateTransient, bRecursive);
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

	if ( UUsdPrimTwin* UsdPrimTwin = RootUsdTwin->Find( PrimPath ) )
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
				FScopedBlockNotices BlockNotices( UsdListener );

				UE::FUsdPrim UsdPrim = UsdStage.GetPrimAtPath( UE::FSdfPath( *PrimPath ) );

#if USE_USD_SDK
				UnrealToUsd::ConvertSceneComponent( UsdStage, PrimSceneComponent, UsdPrim );

				if ( UMeshComponent* MeshComponent = Cast< UMeshComponent >( PrimSceneComponent ) )
				{
					UnrealToUsd::ConvertMeshComponent( UsdStage, MeshComponent, UsdPrim );
				}
#endif // #if USE_USD_SDK

				// We want to keep component visibilities in sync with USD, which uses inherited visibilities
				// To accomplish that while blocking notices we must always propagate component visibility changes
				if ( PropertyChangedEvent.GetPropertyName() == TEXT( "bVisible" ) )
				{
					PrimSceneComponent->Modify();
					PrimSceneComponent->SetVisibility( PrimSceneComponent->GetVisibleFlag(), true );
				}

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
		UnrealUSDWrapper::EraseStageFromCache( UsdStage );
		UsdStage = UE::FUsdStage();

		FUsdStageActorImpl::CloseEditorsForAssets( AssetsCache );
		AssetsCache.Reset(); // We've changed USD file, clear the cache
		BlendShapesByPath.Reset();
		MaterialToPrimvarToUVIndex.Reset();
		LoadUsdStage();
	}
	else if ( PropertyName == GET_MEMBER_NAME_CHECKED( AUsdStageActor, Time ) )
	{
		Refresh();
	}
	else if ( PropertyName == GET_MEMBER_NAME_CHECKED( AUsdStageActor, InitialLoadSet ) ||
		PropertyName == GET_MEMBER_NAME_CHECKED( AUsdStageActor, PurposesToLoad ) )
	{
		LoadUsdStage();
	}
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

	PrimPathsToAssets.Remove( PrimPath );

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
	FString StartPrimPath = StartPrim.GetPrimPath().GetString();
	for ( TMap< FString, UObject* >::TIterator PrimPathToAssetIt = PrimPathsToAssets.CreateIterator(); PrimPathToAssetIt; ++PrimPathToAssetIt )
	{
		if ( PrimPathToAssetIt.Key().StartsWith( StartPrimPath ) || PrimPathToAssetIt.Key() == StartPrimPath )
		{
			PrimPathToAssetIt.RemoveCurrent();
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

	TSharedRef< FUsdSchemaTranslationContext > TranslationContext = FUsdStageActorImpl::CreateUsdSchemaTranslationContext( this, RootUsdTwin->PrimPath );

	for ( const FString& PrimToAnimate : PrimsToAnimate )
	{
		UE::FSdfPath PrimPath( *PrimToAnimate );

		IUsdSchemasModule& SchemasModule = FModuleManager::Get().LoadModuleChecked< IUsdSchemasModule >( "USDSchemas" );
		if ( TSharedPtr< FUsdSchemaTranslator > SchemaTranslator = SchemasModule.GetTranslatorRegistry().CreateTranslatorForSchema( TranslationContext, UE::FUsdTyped( UsdStage.GetPrimAtPath( PrimPath ) ) ) )
		{
			if ( UUsdPrimTwin* UsdPrimTwin = RootUsdTwin->Find( PrimToAnimate ) )
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

#undef LOCTEXT_NAMESPACE
