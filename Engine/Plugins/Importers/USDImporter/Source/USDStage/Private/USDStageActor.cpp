// Copyright Epic Games, Inc. All Rights Reserved.

#include "USDStageActor.h"

#include "USDConversionUtils.h"
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

#include "UsdWrappers/SdfLayer.h"

#include "Async/ParallelFor.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Editor.h"
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
#include "PhysicsEngine/BodySetup.h"
#include "Rendering/SkeletalMeshLODImporterData.h"
#include "StaticMeshAttributes.h"
#include "StaticMeshOperations.h"
#include "Subsystems/AssetEditorSubsystem.h"
#if WITH_EDITOR
#include "LevelEditor.h"
#endif // WITH_EDITOR

#include "Modules/ModuleManager.h"


#define LOCTEXT_NAMESPACE "USDStageActor"

static const EObjectFlags DefaultObjFlag = EObjectFlags::RF_Transactional | EObjectFlags::RF_Transient;

AUsdStageActor::FOnActorLoaded AUsdStageActor::OnActorLoaded;

struct FUsdStageActorImpl
{
	static TSharedRef< FUsdSchemaTranslationContext > CreateUsdSchemaTranslationContext( AUsdStageActor* StageActor, const FString& PrimPath )
	{
		TSharedRef< FUsdSchemaTranslationContext > TranslationContext = MakeShared< FUsdSchemaTranslationContext >( StageActor->PrimPathsToAssets, StageActor->AssetsCache );
		TranslationContext->Level = StageActor->GetLevel();
		TranslationContext->ObjectFlags = DefaultObjFlag;
		TranslationContext->Time = StageActor->GetTime();
		TranslationContext->PurposesToLoad = (EUsdPurpose) StageActor->PurposesToLoad;

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
};

AUsdStageActor::AUsdStageActor()
	: InitialLoadSet( EUsdInitialLoadSet::LoadAll )
	, PurposesToLoad((int32) EUsdPurpose::Proxy)
	, Time( 0.0f )
	, StartTimeCode( 0.f )
	, EndTimeCode( 100.f )
	, TimeCodesPerSecond( 24.f )
	, LevelSequenceHelper(this)
{
	SceneComponent = CreateDefaultSubobject< USceneComponent >( TEXT("SceneComponent0") );
	SceneComponent->Mobility = EComponentMobility::Static;

	RootComponent = SceneComponent;

	RootUsdTwin = NewObject<UUsdPrimTwin>(this, TEXT("RootUsdTwin"), DefaultObjFlag);
	RootUsdTwin->PrimPath = TEXT( "/" );

	if ( HasAutorithyOverStage() )
	{
#if WITH_EDITOR
		// We can't use PostLoad to trigger LoadUsdStage when first loading a saved level because LoadUsdStage may trigger
		// Rename() calls, and that is not allowed.
		FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor");
		LevelEditorModule.OnMapChanged().AddUObject(this, &AUsdStageActor::OnMapChanged);
		FEditorDelegates::BeginPIE.AddUObject(this, &AUsdStageActor::OnBeginPIE);
		FEditorDelegates::PostPIEStarted.AddUObject(this, &AUsdStageActor::OnPostPIEStarted);

		FWorldDelegates::LevelAddedToWorld.AddUObject( this, &AUsdStageActor::OnLevelAddedToWorld );
		FWorldDelegates::LevelRemovedFromWorld.AddUObject( this, &AUsdStageActor::OnLevelRemovedFromWorld );

		FUsdDelegates::OnPostUsdImport.AddUObject( this, &AUsdStageActor::OnPostUsdImport );
		FUsdDelegates::OnPreUsdImport.AddUObject( this, &AUsdStageActor::OnPreUsdImport );
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

		FCoreUObjectDelegates::OnObjectPropertyChanged.AddUObject( this, &AUsdStageActor::OnPrimObjectPropertyChanged );
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

	TSet< FString > UpdatedAssets;
	TSet< FString > UpdatedComponents;
	TSet< FString > ResyncedComponents;

	for ( const TPair< FString, bool >& PrimChangedInfo : SortedPrimsChangedList )
	{
		RefreshStageTask.EnterProgressFrame();

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

			if ( !IsPathAlreadyProcessed( UpdatedAssets, AssetsPrimPath.GetString() ) )
			{
				TSharedRef< FUsdSchemaTranslationContext > TranslationContext = FUsdStageActorImpl::CreateUsdSchemaTranslationContext( this, AssetsPrimPath.GetString() );

				const bool bIsResync = PrimChangedInfo.Value;
				if ( bIsResync )
				{
					LoadAssets( *TranslationContext, GetUsdStage().GetPrimAtPath( AssetsPrimPath ) );
				}
				else
				{
					LoadAsset( *TranslationContext, GetUsdStage().GetPrimAtPath( AssetsPrimPath ) );
				}

				UpdatedAssets.Add( AssetsPrimPath.GetString() );
			}
		}

		// Update components
		{
			UE::FSdfPath ComponentsPrimPath = UnwindToNonCollapsedPrim( FUsdSchemaTranslator::ECollapsingType::Components );

			const bool bResync = PrimChangedInfo.Value;
			TSet< FString >& RefreshedComponents = bResync ? ResyncedComponents : UpdatedComponents;

			if ( !IsPathAlreadyProcessed( RefreshedComponents, ComponentsPrimPath.GetString() ) )
			{
				TSharedRef< FUsdSchemaTranslationContext > TranslationContext = FUsdStageActorImpl::CreateUsdSchemaTranslationContext( this, ComponentsPrimPath.GetString() );
				UpdatePrim( ComponentsPrimPath, PrimChangedInfo.Value, *TranslationContext );
				TranslationContext->CompleteTasks();

				RefreshedComponents.Add( ComponentsPrimPath.GetString() );

				if ( bResync )
				{
					// Consider that the path has been updated in the case of a resync
					UpdatedComponents.Add( ComponentsPrimPath.GetString() );
				}
			}
		}

		if ( HasAutorithyOverStage() )
		{
			OnPrimChanged.Broadcast( PrimChangedInfo.Key, PrimChangedInfo.Value );
		}
	}
}

AUsdStageActor::~AUsdStageActor()
{
#if WITH_EDITOR
	if ( HasAutorithyOverStage() )
	{
		FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor");
		LevelEditorModule.OnMapChanged().RemoveAll(this);
		FEditorDelegates::BeginPIE.RemoveAll(this);
		FEditorDelegates::PostPIEStarted.RemoveAll(this);
		FWorldDelegates::LevelAddedToWorld.RemoveAll(this);
		FWorldDelegates::LevelRemovedFromWorld.RemoveAll(this);
		FUsdDelegates::OnPostUsdImport.RemoveAll(this);
		FUsdDelegates::OnPreUsdImport.RemoveAll(this);

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

	Clear();
	AssetsCache.Reset();

	GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->CloseAllEditorsForAsset(LevelSequence);
	LevelSequence = nullptr;
	StartTimeCode = 0.0f;
	EndTimeCode = 100.0f;
	TimeCodesPerSecond = 24.0f;
	Time = 0;

	RootUsdTwin->Clear();
	RootUsdTwin->PrimPath = TEXT("/");
	GEditor->BroadcastLevelActorListChanged();

	RootLayer.FilePath.Empty();

	UnrealUSDWrapper::EraseStageFromCache( UsdStage );
	UsdStage = UE::FUsdStage();

	OnStageChanged.Broadcast();
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

	// Update the prim animated status
	if ( UsdUtils::IsAnimated( Prim ) )
	{
		PrimsToAnimate.Add( PrimPath );
	}
	else
	{
		PrimsToAnimate.Remove( PrimPath );
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
		UsdPrimTwin->SceneComponent->PostEditChange();

		if ( !UsdPrimTwin->SceneComponent->IsRegistered() )
		{
			UsdPrimTwin->SceneComponent->RegisterComponent();
		}

		ObjectsToWatch.Add( UsdPrimTwin->SceneComponent.Get(), UsdPrimTwin->PrimPath );
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

		GEditor->BroadcastLevelActorListChanged();
		GEditor->RedrawLevelEditingViewports();
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

void AUsdStageActor::SetTime(float InTime)
{
	Time = InTime;

	Refresh();
}

void AUsdStageActor::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	FProperty* PropertyThatChanged = PropertyChangedEvent.MemberProperty;
	const FName PropertyName = PropertyThatChanged ? PropertyThatChanged->GetFName() : NAME_None;

	if ( PropertyName == GET_MEMBER_NAME_CHECKED( AUsdStageActor, RootLayer ) )
	{
		UnrealUSDWrapper::EraseStageFromCache( UsdStage );
		UsdStage = UE::FUsdStage();

		AssetsCache.Reset(); // We've changed USD file, clear the cache
		LoadUsdStage();
	}
	else if ( PropertyName == GET_MEMBER_NAME_CHECKED( AUsdStageActor, Time ) )
	{
		Refresh();
	}
	else if ( PropertyName == GET_MEMBER_NAME_CHECKED( AUsdStageActor, StartTimeCode ) || PropertyName == GET_MEMBER_NAME_CHECKED( AUsdStageActor, EndTimeCode ) )
	{
		if ( UsdStage )
		{
			UsdStage.SetStartTimeCode( StartTimeCode );
			UsdStage.SetEndTimeCode( EndTimeCode );

			LevelSequenceHelper.UpdateLevelSequence( UsdStage );
		}
	}
	else if ( PropertyName == GET_MEMBER_NAME_CHECKED( AUsdStageActor, InitialLoadSet ) ||
		      PropertyName == GET_MEMBER_NAME_CHECKED( AUsdStageActor, PurposesToLoad ) )
	{
		LoadUsdStage();
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
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

	FString FilePath = IFileManager::Get().ConvertToAbsolutePathForExternalAppForRead( *RootLayer.FilePath );
	FilePath = FPaths::GetPath(FilePath) + TEXT("/");
	FString CleanFilename = FPaths::GetCleanFilename( RootLayer.FilePath );

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
}

#if WITH_EDITOR
void AUsdStageActor::OnMapChanged( UWorld* World, EMapChangeType ChangeType )
{
	// This is in charge of loading the stage when we load a level that had a AUsdStageActor saved with a valid root layer filepath.
	// Note that we don't handle here updating the SUSDStage window when our level/world is being destroyed, we do it in the destructor.
	if ( HasAutorithyOverStage() &&
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

	RootUsdTwin->Clear();
	RootUsdTwin->PrimPath = TEXT("/");

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

	SetTime( StartTimeCode );

	GEditor->BroadcastLevelActorListChanged();

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

	if ( HasAutorithyOverStage() )
	{
		if (LevelSequence)
		{
			// The sequencer won't update on its own, so let's at least force it closed
			GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->CloseAllEditorsForAsset(LevelSequence);
		}

		LevelSequence = nullptr;
		SubLayerLevelSequencesByIdentifier.Reset();

		LevelSequenceHelper.InitLevelSequence(UsdStage);
	}
}

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
		}
	}
}

void AUsdStageActor::PostDuplicate( bool bDuplicateForPIE )
{
	Super::PostDuplicate( bDuplicateForPIE );

	// Setup for the very first frame when we duplicate into PIE, or else we will just show a T-pose
	if ( bDuplicateForPIE )
	{
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
		Ar << SubLayerLevelSequencesByIdentifier;
		Ar << RootUsdTwin;
		Ar << PrimsToAnimate;
		Ar << ObjectsToWatch;
		Ar << AssetsCache;
		Ar << PrimPathsToAssets;
	}
}

void AUsdStageActor::OnLevelAddedToWorld( ULevel* Level, UWorld* World )
{
	if ( !HasAutorithyOverStage() )
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
	if ( !HasAutorithyOverStage() )
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
	if ( !UsdStage || !HasAutorithyOverStage() )
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
	if ( !UsdStage || !HasAutorithyOverStage() )
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
}

void AUsdStageActor::OnPrimObjectPropertyChanged( UObject* ObjectBeingModified, FPropertyChangedEvent& PropertyChangedEvent )
{
	if ( ObjectBeingModified == this )
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
				if ( UMeshComponent* MeshComponent = Cast< UMeshComponent >( PrimSceneComponent ) )
				{
					UnrealToUsd::ConvertMeshComponent( UsdStage, MeshComponent, UsdPrim );
				}
				else
				{
					UnrealToUsd::ConvertSceneComponent( UsdStage, PrimSceneComponent, UsdPrim );
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
				if ( this->HasAutorithyOverStage() )
				{
					this->OnPrimChanged.Broadcast( PrimPath, false );
				}
			}
		}
	}
}

bool AUsdStageActor::HasAutorithyOverStage() const
{
	return !IsTemplate() && ( !GetWorld() || !GetWorld()->IsGameWorld() );
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

	auto CreateAssetsForPrims = [ &UsdSchemasModule, &TranslationContext ]( const TArray< UE::FUsdPrim >& AllPrimAssets )
	{
		TRACE_CPUPROFILER_EVENT_SCOPE( AUsdStageActor::CreateAssetsForPrims );

		for ( const UE::FUsdPrim& UsdPrim : AllPrimAssets )
		{
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
	TArray< UE::FUsdPrim > AllPrimAssets = UsdUtils::GetAllPrimsOfType( StartPrim, TEXT("UsdShadeMaterial") );

	CreateAssetsForPrims( AllPrimAssets );

	// Load meshes
	AllPrimAssets = UsdUtils::GetAllPrimsOfType( StartPrim, TEXT("UsdGeomXformable"), PruneChildren );
	CreateAssetsForPrims( AllPrimAssets );
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

	GEditor->BroadcastLevelActorListChanged();
	GEditor->RedrawLevelEditingViewports();
}

#undef LOCTEXT_NAMESPACE
