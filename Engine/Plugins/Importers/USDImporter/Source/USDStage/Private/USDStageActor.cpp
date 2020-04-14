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
#include "MaterialEditingLibrary.h"
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

#if USE_USD_SDK
#include "USDIncludesStart.h"
	#include "pxr/usd/sdf/layer.h"
	#include "pxr/usd/usd/modelAPI.h"
	#include "pxr/usd/usd/stageCache.h"
	#include "pxr/usd/usd/stageCacheContext.h"
	#include "pxr/usd/usdGeom/camera.h"
	#include "pxr/usd/usdGeom/mesh.h"
	#include "pxr/usd/usdGeom/metrics.h"
	#include "pxr/usd/usdGeom/pointInstancer.h"
	#include "pxr/usd/usdGeom/scope.h"
	#include "pxr/usd/usdGeom/xform.h"
	#include "pxr/usd/usdGeom/xformCommonAPI.h"
	#include "pxr/usd/usdShade/material.h"
	#include "pxr/usd/usdSkel/root.h"
#include "USDIncludesEnd.h"
#endif // #if USE_USD_SDK

#define LOCTEXT_NAMESPACE "USDStageActor"

static const EObjectFlags DefaultObjFlag = EObjectFlags::RF_Transactional | EObjectFlags::RF_Transient;

AUsdStageActor::FOnActorLoaded AUsdStageActor::OnActorLoaded;

#if USE_USD_SDK

struct FUsdStageActorImpl
{
	static TSharedRef< FUsdSchemaTranslationContext > CreateUsdSchemaTranslationContext( AUsdStageActor* StageActor, const FString& PrimPath )
	{
		TSharedRef< FUsdSchemaTranslationContext > TranslationContext = MakeShared< FUsdSchemaTranslationContext >( StageActor->PrimPathsToAssets, StageActor->AssetsCache );
		TranslationContext->Level = StageActor->GetLevel();
		TranslationContext->ObjectFlags = DefaultObjFlag;
		TranslationContext->Time = StageActor->GetTime();
		TranslationContext->PurposesToLoad = (EUsdPurpose) StageActor->PurposesToLoad;

		TUsdStore< pxr::SdfPath > UsdPrimPath = UnrealToUsd::ConvertPath( *PrimPath );
		UUsdPrimTwin* ParentUsdPrimTwin = StageActor->RootUsdTwin->Find( UsdToUnreal::ConvertPath( UsdPrimPath.Get().GetParentPath() ) );

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

#endif // #if USE_USD_SDK

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

#if WITH_EDITOR
	// We can't use PostLoad to trigger LoadUsdStage when first loading a saved level because LoadUsdStage may trigger
	// Rename() calls, and that is not allowed.
	FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor");
	LevelEditorModule.OnMapChanged().AddUObject(this, &AUsdStageActor::OnMapChanged);
	FEditorDelegates::BeginPIE.AddUObject(this, &AUsdStageActor::OnBeginPIE);
	FEditorDelegates::PostPIEStarted.AddUObject(this, &AUsdStageActor::OnPostPIEStarted);
#endif // WITH_EDITOR

#if USE_USD_SDK
	OnTimeChanged.AddUObject( this, &AUsdStageActor::AnimatePrims );

	RootUsdTwin->PrimPath = TEXT("/");

	UsdListener.OnPrimsChanged.AddUObject( this, &AUsdStageActor::OnPrimsChanged );

	UsdListener.OnLayersChanged.AddLambda(
		[&](const pxr::SdfLayerChangeListVec& ChangeVec)
		{
			TUniquePtr<TGuardValue<ITransaction*>> SuppressTransaction = nullptr;
			if ( this->GetOutermost()->HasAnyPackageFlags( PKG_PlayInEditor ) )
			{
				SuppressTransaction = MakeUnique<TGuardValue<ITransaction*>>( GUndo, nullptr );
			}

			// Check to see if any layer reloaded. If so, rebuild all of our animations as a single layer changing
			// might propagate timecodes through all level sequences
			for (const std::pair<pxr::SdfLayerHandle, pxr::SdfChangeList>& ChangeVecItem : ChangeVec)
			{
				const pxr::SdfChangeList::EntryList& ChangeList = ChangeVecItem.second.GetEntryList();
				for (const std::pair<pxr::SdfPath, pxr::SdfChangeList::Entry>& Change : ChangeList)
				{
					for (const pxr::SdfChangeList::Entry::SubLayerChange& SubLayerChange : Change.second.subLayerChanges)
					{
						const pxr::SdfChangeList::SubLayerChangeType ChangeType = SubLayerChange.second;
						if (ChangeType == pxr::SdfChangeList::SubLayerChangeType::SubLayerAdded ||
							ChangeType == pxr::SdfChangeList::SubLayerChangeType::SubLayerRemoved)
						{
							UE_LOG(LogUsd, Verbose, TEXT("Reloading animations because layer '%s' was added/removed"), *UsdToUnreal::ConvertString(SubLayerChange.first));
							ReloadAnimations();
							return;
						}
					}

					const pxr::SdfChangeList::Entry::_Flags& Flags = Change.second.flags;
					if (Flags.didReloadContent)
					{
						UE_LOG(LogUsd, Verbose, TEXT("Reloading animations because layer '%s' reloaded"), *UsdToUnreal::ConvertPath(Change.first));
						ReloadAnimations();
						return;
					}
				}
			}
		}
	);
#endif // #if USE_USD_SDK

	if ( HasAutorithyOverStage() )
	{
		FCoreUObjectDelegates::OnObjectPropertyChanged.AddUObject( this, &AUsdStageActor::OnPrimObjectPropertyChanged );
	}
}

void AUsdStageActor::OnPrimsChanged( const TMap< FString, bool >& PrimsChangedList )
{
#if USE_USD_SDK
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

		auto UnwindToNonCollapsedPrim = [ &PrimChangedInfo, this ]( FUsdSchemaTranslator::ECollapsingType CollapsingType ) -> TUsdStore< pxr::SdfPath >
		{
			IUsdSchemasModule& UsdSchemasModule = FModuleManager::Get().LoadModuleChecked< IUsdSchemasModule >( TEXT("USDSchemas") );

			TSharedRef< FUsdSchemaTranslationContext > TranslationContext = FUsdStageActorImpl::CreateUsdSchemaTranslationContext( this, PrimChangedInfo.Key );

			TUsdStore< pxr::SdfPath > UsdPrimPath = UnrealToUsd::ConvertPath( *PrimChangedInfo.Key );
			TUsdStore< pxr::UsdPrim > UsdPrim = this->GetUsdStage()->GetPrimAtPath( UsdPrimPath.Get() );

			if ( TSharedPtr< FUsdSchemaTranslator > SchemaTranslator = UsdSchemasModule.GetTranslatorRegistry().CreateTranslatorForSchema( TranslationContext, pxr::UsdTyped( UsdPrim.Get() ) ) )
			{
				while ( SchemaTranslator->IsCollapsed( CollapsingType ) )
				{
					UsdPrimPath = UsdPrimPath.Get().GetParentPath();
					UsdPrim = this->GetUsdStage()->GetPrimAtPath( UsdPrimPath.Get() );

					TranslationContext = FUsdStageActorImpl::CreateUsdSchemaTranslationContext( this, UsdToUnreal::ConvertPath( UsdPrimPath.Get() ) );
					SchemaTranslator = UsdSchemasModule.GetTranslatorRegistry().CreateTranslatorForSchema( TranslationContext, pxr::UsdTyped( UsdPrim.Get() ) );

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
			TUsdStore< pxr::SdfPath > AssetsPrimPath = UnwindToNonCollapsedPrim( FUsdSchemaTranslator::ECollapsingType::Assets );
			const FString UEAssetsPrimPath = UsdToUnreal::ConvertPath( AssetsPrimPath.Get() );

			if ( !IsPathAlreadyProcessed( UpdatedAssets, UEAssetsPrimPath ) )
			{
				TSharedRef< FUsdSchemaTranslationContext > TranslationContext = FUsdStageActorImpl::CreateUsdSchemaTranslationContext( this, UsdToUnreal::ConvertPath( AssetsPrimPath.Get() ) );
				
				const bool bIsResync = PrimChangedInfo.Value;
				if ( bIsResync )
				{
					this->LoadAssets( *TranslationContext, this->GetUsdStage()->GetPrimAtPath( AssetsPrimPath.Get() ) );
				}
				else
				{
					this->LoadAsset( *TranslationContext, this->GetUsdStage()->GetPrimAtPath( AssetsPrimPath.Get() ) );
				}

				UpdatedAssets.Add( UEAssetsPrimPath );
			}
		}

		// Update components
		{
			TUsdStore< pxr::SdfPath > ComponentsPrimPath = UnwindToNonCollapsedPrim( FUsdSchemaTranslator::ECollapsingType::Components );
			const FString UEComponentsPrimPath = UsdToUnreal::ConvertPath( ComponentsPrimPath.Get() );

			const bool bResync = PrimChangedInfo.Value;
			TSet< FString >& RefreshedComponents = bResync ? ResyncedComponents : UpdatedComponents;

			if ( !IsPathAlreadyProcessed( RefreshedComponents, UEComponentsPrimPath ) )
			{
				TSharedRef< FUsdSchemaTranslationContext > TranslationContext = FUsdStageActorImpl::CreateUsdSchemaTranslationContext( this, UsdToUnreal::ConvertPath( ComponentsPrimPath.Get() ) );
				UpdatePrim( ComponentsPrimPath.Get(), PrimChangedInfo.Value, *TranslationContext );
				TranslationContext->CompleteTasks();

				RefreshedComponents.Add( UEComponentsPrimPath );

				if ( bResync )
				{
					// Consider that the path has been updated in the case of a resync
					UpdatedComponents.Add( UEComponentsPrimPath );
				}
			}
		}

		if ( HasAutorithyOverStage() )
		{
			OnPrimChanged.Broadcast( PrimChangedInfo.Key, PrimChangedInfo.Value );
		}
	}
#endif // #if USE_USD_SDK
}

AUsdStageActor::~AUsdStageActor()
{
#if WITH_EDITOR
	FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor");
	LevelEditorModule.OnMapChanged().RemoveAll(this);
	FEditorDelegates::BeginPIE.RemoveAll(this);
	FEditorDelegates::PostPIEStarted.RemoveAll(this);
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

#if USE_USD_SDK
	UnrealUSDWrapper::GetUsdStageCache().Erase( UsdStageStore.Get() );
	UsdStageStore = TUsdStore< pxr::UsdStageRefPtr >();
#endif // #if USE_USD_SDK

	OnStageChanged.Broadcast();
}

#if USE_USD_SDK

UUsdPrimTwin* AUsdStageActor::GetOrCreatePrimTwin( const pxr::SdfPath& UsdPrimPath )
{
	const FString PrimPath = UsdToUnreal::ConvertPath( UsdPrimPath );
	const FString ParentPrimPath = UsdToUnreal::ConvertPath( UsdPrimPath.GetParentPath() );

	UUsdPrimTwin* UsdPrimTwin = RootUsdTwin->Find( PrimPath );
	UUsdPrimTwin* ParentUsdPrimTwin = RootUsdTwin->Find( ParentPrimPath );

	const pxr::UsdPrim Prim = GetUsdStage()->GetPrimAtPath( UsdPrimPath );

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

UUsdPrimTwin* AUsdStageActor::ExpandPrim( const pxr::UsdPrim& Prim, FUsdSchemaTranslationContext& TranslationContext )
{
	if ( !Prim )
	{
		return nullptr;
	}

	TRACE_CPUPROFILER_EVENT_SCOPE( AUsdStageActor::ExpandPrim );

	UUsdPrimTwin* UsdPrimTwin = GetOrCreatePrimTwin( Prim.GetPrimPath() );

	bool bExpandChilren = true;

	IUsdSchemasModule& UsdSchemasModule = FModuleManager::Get().LoadModuleChecked< IUsdSchemasModule >( TEXT("USDSchemas") );

	if ( TSharedPtr< FUsdSchemaTranslator > SchemaTranslator = UsdSchemasModule.GetTranslatorRegistry().CreateTranslatorForSchema( TranslationContext.AsShared(), pxr::UsdTyped( Prim ) ) )
	{
		if ( !UsdPrimTwin->SceneComponent.IsValid() )
		{
			UsdPrimTwin->SceneComponent = SchemaTranslator->CreateComponents();

			if ( UsdPrimTwin->SceneComponent.IsValid() )
			{
				ObjectsToWatch.Add( UsdPrimTwin->SceneComponent.Get(), UsdPrimTwin->PrimPath );
			}
		}
		else
		{
			SchemaTranslator->UpdateComponents( UsdPrimTwin->SceneComponent.Get() );
		}

		bExpandChilren = !SchemaTranslator->CollapsesChildren( FUsdSchemaTranslator::ECollapsingType::Components );
	}

	if ( bExpandChilren )
	{
		USceneComponent* ContextParentComponent = TranslationContext.ParentComponent;

		if ( UsdPrimTwin && UsdPrimTwin->SceneComponent.IsValid() )
		{
			ContextParentComponent = UsdPrimTwin->SceneComponent.Get();
		}

		TGuardValue< USceneComponent* > ParentComponentGuard( TranslationContext.ParentComponent, ContextParentComponent );

		pxr::UsdPrimSiblingRange PrimChildren = Prim.GetFilteredChildren( pxr::UsdTraverseInstanceProxies() );
		for ( TUsdStore< pxr::UsdPrim > ChildStore : PrimChildren )
		{
			ExpandPrim( ChildStore.Get(), TranslationContext );
		}
	}

	if ( UsdPrimTwin && UsdPrimTwin->SceneComponent.IsValid() && !UsdPrimTwin->SceneComponent->IsRegistered() )
	{
		UsdPrimTwin->SceneComponent->RegisterComponent();
	}

	return UsdPrimTwin;
}

void AUsdStageActor::UpdatePrim( const pxr::SdfPath& InUsdPrimPath, bool bResync, FUsdSchemaTranslationContext& TranslationContext )
{
	TRACE_CPUPROFILER_EVENT_SCOPE( AUsdStageActor::UpdatePrim );

	FScopedSlowTask SlowTask( 1.f, LOCTEXT( "UpdatingUSDPrim", "Updating USD Prim" ) );
	SlowTask.MakeDialog();
	SlowTask.EnterProgressFrame();

	pxr::SdfPath UsdPrimPath = InUsdPrimPath;

	if ( !UsdPrimPath.IsAbsoluteRootOrPrimPath() )
	{
		UsdPrimPath = UsdPrimPath.GetAbsoluteRootOrPrimPath();
	}

	if ( UsdPrimPath.IsAbsoluteRootOrPrimPath() )
	{
		if ( bResync )
		{
			FString PrimPath = UsdToUnreal::ConvertPath( UsdPrimPath );
			if ( UUsdPrimTwin* UsdPrimTwin = RootUsdTwin->Find( PrimPath ) )
			{
				UsdPrimTwin->Clear();
			}
		}

		TUsdStore< pxr::UsdPrim > PrimToExpand = GetUsdStage()->GetPrimAtPath( UsdPrimPath );
		UUsdPrimTwin* UsdPrimTwin = ExpandPrim( PrimToExpand.Get(), TranslationContext );

		GEditor->BroadcastLevelActorListChanged();
		GEditor->RedrawLevelEditingViewports();
	}
}

const pxr::UsdStageRefPtr& AUsdStageActor::GetUsdStage()
{
	OpenUsdStage();

	return UsdStageStore.Get();
}

#endif // #if USE_USD_SDK

void AUsdStageActor::SetTime(float InTime)
{
	Time = InTime;

	Refresh();
}

void AUsdStageActor::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	FProperty* PropertyThatChanged = PropertyChangedEvent.MemberProperty;
	const FName PropertyName = PropertyThatChanged ? PropertyThatChanged->GetFName() : NAME_None;

#if USE_USD_SDK
	if ( PropertyName == GET_MEMBER_NAME_CHECKED( AUsdStageActor, RootLayer ) )
	{
		UnrealUSDWrapper::GetUsdStageCache().Erase( UsdStageStore.Get() );
		UsdStageStore = TUsdStore< pxr::UsdStageRefPtr >();

		AssetsCache.Reset(); // We've changed USD file, clear the cache
		LoadUsdStage();
	}
	else if ( PropertyName == GET_MEMBER_NAME_CHECKED( AUsdStageActor, Time ) )
	{
		Refresh();
	}
	else if ( PropertyName == GET_MEMBER_NAME_CHECKED( AUsdStageActor, StartTimeCode ) || PropertyName == GET_MEMBER_NAME_CHECKED( AUsdStageActor, EndTimeCode ) )
	{
		if ( const pxr::UsdStageRefPtr& UsdStage = GetUsdStage() )
		{
			UsdStage->SetStartTimeCode( StartTimeCode );
			UsdStage->SetEndTimeCode( EndTimeCode );

			LevelSequenceHelper.UpdateLevelSequence( UsdStage );
		}
	}
	else if ( PropertyName == GET_MEMBER_NAME_CHECKED( AUsdStageActor, InitialLoadSet ) ||
		      PropertyName == GET_MEMBER_NAME_CHECKED( AUsdStageActor, PurposesToLoad ) )
	{
		LoadUsdStage();
	}
#endif // #if USE_USD_SDK

	Super::PostEditChangeProperty(PropertyChangedEvent);
}

void AUsdStageActor::Clear()
{
	PrimPathsToAssets.Reset();
	ObjectsToWatch.Reset();
}

void AUsdStageActor::OpenUsdStage()
{
#if USE_USD_SDK
	// Early exit if stage is already opened
	if ( UsdStageStore.Get() || RootLayer.FilePath.IsEmpty() )
	{
		return;
	}

	TRACE_CPUPROFILER_EVENT_SCOPE( AUsdStageActor::OpenUsdStage );

	FString FilePath = IFileManager::Get().ConvertToAbsolutePathForExternalAppForRead( *RootLayer.FilePath );
	FilePath = FPaths::GetPath(FilePath) + TEXT("/");
	FString CleanFilename = FPaths::GetCleanFilename( RootLayer.FilePath );

	if ( FPaths::FileExists( RootLayer.FilePath ) )
	{
		FScopedUsdAllocs UsdAllocs;

		pxr::UsdStageCacheContext UsdStageCacheContext( UnrealUSDWrapper::GetUsdStageCache() );
		UsdStageStore = pxr::UsdStage::Open( UnrealToUsd::ConvertString( *RootLayer.FilePath ).Get(), pxr::UsdStage::InitialLoadSet( InitialLoadSet ) );
	}

	if ( UsdStageStore.Get() )
	{
		UsdStageStore.Get()->SetEditTarget( UsdStageStore.Get()->GetRootLayer() );

		UsdListener.Register( UsdStageStore.Get() );

		OnStageChanged.Broadcast();
	}
#endif // #if USE_USD_SDK
}

#if WITH_EDITOR
void AUsdStageActor::OnMapChanged(UWorld* World, EMapChangeType ChangeType)
{
	// Our current level has changed
	if (HasAutorithyOverStage() && World && World == GetWorld() && World->GetCurrentLevel() == GetTypedOuter<ULevel>())
	{
		if (ChangeType == EMapChangeType::LoadMap || ChangeType == EMapChangeType::NewMap)
		{
			// This is in charge of first loading our stage when we load a level that had a AUsdStageActor saved with
			// a valid root layer filepath
			LoadUsdStage();

			// SUSDStage window needs to update
			OnActorLoaded.Broadcast(this);
		}
		else if (ChangeType == EMapChangeType::TearDownWorld)
		{
			// This is in charge of clearing the SUSDStage window when switching away from our level
			// SUSDStage window needs to update
			OnActorDestroyed.Broadcast();
			Reset();
		}
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
#if USE_USD_SDK
	TRACE_CPUPROFILER_EVENT_SCOPE( AUsdStageActor::LoadUsdStage );

	double StartTime = FPlatformTime::Cycles64();

	FScopedSlowTask SlowTask( 1.f, LOCTEXT( "LoadingUDStage", "Loading USD Stage") );
	SlowTask.MakeDialog();
	SlowTask.EnterProgressFrame();

	Clear();

	RootUsdTwin->Clear();
	RootUsdTwin->PrimPath = TEXT("/");

	const pxr::UsdStageRefPtr& UsdStage = GetUsdStage();

	if ( !UsdStage )
	{
		OnStageChanged.Broadcast();
		return;
	}

	ReloadAnimations();

	TSharedRef< FUsdSchemaTranslationContext > TranslationContext = FUsdStageActorImpl::CreateUsdSchemaTranslationContext( this, RootUsdTwin->PrimPath );

	LoadAssets( *TranslationContext, UsdStage->GetPseudoRoot() );

	UpdatePrim( UsdStage->GetPseudoRoot().GetPrimPath(), true, *TranslationContext );

	TranslationContext->CompleteTasks();

	SetTime( StartTimeCode );

	GEditor->BroadcastLevelActorListChanged();

	// Log time spent to load the stage
	double ElapsedSeconds = FPlatformTime::ToSeconds64(FPlatformTime::Cycles64() - StartTime);

	int ElapsedMin = int(ElapsedSeconds / 60.0);
	ElapsedSeconds -= 60.0 * (double)ElapsedMin;

	UE_LOG( LogUsd, Log, TEXT("%s %s in [%d min %.3f s]"), TEXT("Stage loaded"), *FPaths::GetBaseFilename( RootLayer.FilePath ), ElapsedMin, ElapsedSeconds );
#endif // #if USE_USD_SDK
}

void AUsdStageActor::Refresh() const
{
	OnTimeChanged.Broadcast();
}

void AUsdStageActor::ReloadAnimations()
{
#if USE_USD_SDK
	TRACE_CPUPROFILER_EVENT_SCOPE( AUsdStageActor::ReloadAnimations );

	const pxr::UsdStageRefPtr& UsdStage = GetUsdStage();
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
#endif // #if USE_USD_SDK
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
#if USE_USD_SDK
		const TArray<FName>& ChangedProperties = TransactionEvent.GetChangedProperties();
		if (ChangedProperties.Contains(GET_MEMBER_NAME_CHECKED(AUsdStageActor, RootLayer)))
		{
			// Changed the path, so we need to reopen to the correct stage
			UnrealUSDWrapper::GetUsdStageCache().Erase( UsdStageStore.Get() );
			UsdStageStore = TUsdStore< pxr::UsdStageRefPtr >();
			OnStageChanged.Broadcast();

			ReloadAnimations();
		}
		else if (ChangedProperties.Contains(GET_MEMBER_NAME_CHECKED(AUsdStageActor, Time)))
		{
			Refresh();
		}
#endif // #if USE_USD_SDK
	}
}

void AUsdStageActor::PostDuplicate( bool bDuplicateForPIE )
{
	Super::PostDuplicate( bDuplicateForPIE );

#if USE_USD_SDK
	// Setup for the very first frame when we duplicate into PIE, or else we will just show a T-pose
	if ( bDuplicateForPIE )
	{
		AnimatePrims();
	}
#endif // #if USE_USD_SDK
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
#if USE_USD_SDK
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
			const pxr::UsdStageRefPtr& UsdStage = GetUsdStage();

			if ( UsdStage )
			{
				FScopedBlockNotices BlockNotices( UsdListener );

				TUsdStore< pxr::UsdPrim > UsdPrim = UsdStage->GetPrimAtPath( UnrealToUsd::ConvertPath( *PrimPath ).Get() );

				if ( UMeshComponent* MeshComponent = Cast< UMeshComponent >( PrimSceneComponent ) )
				{
					UnrealToUsd::ConvertMeshComponent( UsdStage, MeshComponent, UsdPrim.Get() );
				}
				else
				{
					UnrealToUsd::ConvertSceneComponent( UsdStage, PrimSceneComponent, UsdPrim.Get() );
				}

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
#endif // #if USE_USD_SDK
}

bool AUsdStageActor::HasAutorithyOverStage() const
{
	return !IsTemplate() && ( !GetWorld() || !GetWorld()->IsGameWorld() );
}

#if USE_USD_SDK
void AUsdStageActor::LoadAsset( FUsdSchemaTranslationContext& TranslationContext, const pxr::UsdPrim& Prim )
{
	TRACE_CPUPROFILER_EVENT_SCOPE( AUsdStageActor::LoadAsset );

	// Mark the assets as non transactional so that they don't get serialized in the transaction buffer
	TGuardValue< EObjectFlags > ContextFlagsGuard( TranslationContext.ObjectFlags, TranslationContext.ObjectFlags & ~RF_Transactional );

	const FString PrimPath = UsdToUnreal::ConvertPath( Prim.GetPrimPath() );
	PrimPathsToAssets.Remove( PrimPath );

	IUsdSchemasModule& UsdSchemasModule = FModuleManager::Get().LoadModuleChecked< IUsdSchemasModule >( TEXT("USDSchemas") );
	if ( TSharedPtr< FUsdSchemaTranslator > SchemaTranslator = UsdSchemasModule.GetTranslatorRegistry().CreateTranslatorForSchema( TranslationContext.AsShared(), pxr::UsdTyped( Prim ) ) )
	{
		TRACE_CPUPROFILER_EVENT_SCOPE( AUsdStageActor::CreateAssetsForPrim );
		SchemaTranslator->CreateAssets();
	}

	TranslationContext.CompleteTasks(); // Finish the asset tasks before moving on
}

void AUsdStageActor::LoadAssets( FUsdSchemaTranslationContext& TranslationContext, const pxr::UsdPrim& StartPrim )
{
	TRACE_CPUPROFILER_EVENT_SCOPE( AUsdStageActor::LoadAssets );

	// Mark the assets as non transactional so that they don't get serialized in the transaction buffer
	TGuardValue< EObjectFlags > ContextFlagsGuard( TranslationContext.ObjectFlags, TranslationContext.ObjectFlags & ~RF_Transactional );

	// Clear existing prim/asset association
	FString StartPrimPath = UsdToUnreal::ConvertPath( StartPrim.GetPrimPath() );
	for ( TMap< FString, UObject* >::TIterator PrimPathToAssetIt = PrimPathsToAssets.CreateIterator(); PrimPathToAssetIt; ++PrimPathToAssetIt )
	{
		if ( PrimPathToAssetIt.Key().StartsWith( StartPrimPath ) || PrimPathToAssetIt.Key() == StartPrimPath )
		{
			PrimPathToAssetIt.RemoveCurrent();
		}
	}

	IUsdSchemasModule& UsdSchemasModule = FModuleManager::Get().LoadModuleChecked< IUsdSchemasModule >( TEXT("USDSchemas") );

	auto CreateAssetsForPrims = [ &UsdSchemasModule, &TranslationContext ]( const TArray< TUsdStore< pxr::UsdPrim > >& AllPrimAssets )
	{
		TRACE_CPUPROFILER_EVENT_SCOPE( AUsdStageActor::CreateAssetsForPrims );

		for ( const TUsdStore< pxr::UsdPrim >& UsdPrim : AllPrimAssets )
		{
			if ( TSharedPtr< FUsdSchemaTranslator > SchemaTranslator = UsdSchemasModule.GetTranslatorRegistry().CreateTranslatorForSchema( TranslationContext.AsShared(), pxr::UsdTyped( UsdPrim.Get() ) ) )
			{
				TRACE_CPUPROFILER_EVENT_SCOPE( AUsdStageActor::CreateAssetsForPrim );
				SchemaTranslator->CreateAssets();
			}
		}

		TranslationContext.CompleteTasks(); // Finish the assets tasks before moving on
	};

	auto PruneChildren = [ &UsdSchemasModule, &TranslationContext ]( const pxr::UsdPrim& UsdPrim ) -> bool
	{
		if ( TSharedPtr< FUsdSchemaTranslator > SchemaTranslator = UsdSchemasModule.GetTranslatorRegistry().CreateTranslatorForSchema( TranslationContext.AsShared(), pxr::UsdTyped( UsdPrim ) ) )
		{
			return SchemaTranslator->CollapsesChildren( FUsdSchemaTranslator::ECollapsingType::Assets );
		}

		return false;
	};

	// Load materials first since meshes are referencing them
	TArray< TUsdStore< pxr::UsdPrim > > AllPrimAssets = UsdUtils::GetAllPrimsOfType( StartPrim, pxr::TfType::Find< pxr::UsdShadeMaterial >() );

	CreateAssetsForPrims( AllPrimAssets );

	// Load meshes
	AllPrimAssets = UsdUtils::GetAllPrimsOfType( StartPrim, pxr::TfType::Find< pxr::UsdGeomXformable >(), PruneChildren );
	CreateAssetsForPrims( AllPrimAssets );
}

void AUsdStageActor::AnimatePrims()
{
	// Don't try to animate if we don't have a stage opened
	const pxr::UsdStageRefPtr& UsdStage = GetUsdStage();
	if (!UsdStage)
	{
		return;
	}

	TSharedRef< FUsdSchemaTranslationContext > TranslationContext = FUsdStageActorImpl::CreateUsdSchemaTranslationContext( this, RootUsdTwin->PrimPath );

	for ( const FString& PrimToAnimate : PrimsToAnimate )
	{
		TUsdStore< pxr::SdfPath > PrimPath = UnrealToUsd::ConvertPath( *PrimToAnimate );

		IUsdSchemasModule& SchemasModule = FModuleManager::Get().LoadModuleChecked< IUsdSchemasModule >( "USDSchemas" );
		if ( TSharedPtr< FUsdSchemaTranslator > SchemaTranslator = SchemasModule.GetTranslatorRegistry().CreateTranslatorForSchema( TranslationContext, pxr::UsdTyped( this->GetUsdStage()->GetPrimAtPath( PrimPath.Get() ) ) ) )
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

#endif // #if USE_USD_SDK

#undef LOCTEXT_NAMESPACE
