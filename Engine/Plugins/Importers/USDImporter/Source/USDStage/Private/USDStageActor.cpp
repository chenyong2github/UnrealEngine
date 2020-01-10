// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "USDStageActor.h"

#include "USDConversionUtils.h"
#include "USDGeomMeshTranslator.h"
#include "USDGeomXformableTranslator.h"
#include "USDListener.h"
#include "USDPrimConversion.h"
#include "USDSchemasModule.h"
#include "USDSchemaTranslator.h"
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
#include "MaterialEditingLibrary.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceConstant.h"
#include "MeshDescription.h"
#include "MeshDescriptionOperations.h"
#include "Misc/Paths.h"
#include "Misc/ScopedSlowTask.h"
#include "PhysicsEngine/BodySetup.h"
#include "Rendering/SkeletalMeshLODImporterData.h"
#include "StaticMeshAttributes.h"

#include "Channels/MovieSceneChannelProxy.h"
#include "LevelSequence.h"
#include "LevelSequenceActor.h"
#include "Misc/FrameNumber.h"
#include "Modules/ModuleManager.h"
#include "MovieScene.h"
#include "Sections/MovieSceneFloatSection.h"
#include "Tracks/MovieSceneFloatTrack.h"

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

DEFINE_LOG_CATEGORY( LogUsdStage );

static const EObjectFlags DefaultObjFlag = EObjectFlags::RF_Transactional | EObjectFlags::RF_Transient;

AUsdStageActor::FOnActorLoaded AUsdStageActor::OnActorLoaded{};

#if USE_USD_SDK

struct UsdStageActorImpl
{
	static TSharedRef< FUsdSchemaTranslationContext > CreateUsdSchemaTranslationContext( AUsdStageActor* StageActor, const FString& PrimPath )
	{
		TSharedRef< FUsdSchemaTranslationContext > TranslationContext = MakeShared< FUsdSchemaTranslationContext >( StageActor->PrimPathsToAssets, StageActor->AssetsCache );
		TranslationContext->Level = StageActor->GetLevel();
		TranslationContext->ObjectFlags = DefaultObjFlag;
		TranslationContext->Time = StageActor->GetTime();

		TUsdStore< pxr::SdfPath > UsdPrimPath = UnrealToUsd::ConvertPath( *PrimPath );
		FUsdPrimTwin* ParentUsdPrimTwin = StageActor->RootUsdTwin.Find( UsdToUnreal::ConvertPath( UsdPrimPath.Get().GetParentPath() ) );

		if ( !ParentUsdPrimTwin )
		{
			ParentUsdPrimTwin = &StageActor->RootUsdTwin;
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
	, Time( 0.0f )
	, StartTimeCode( 0.f )
	, EndTimeCode( 100.f )
	, TimeCodesPerSecond( 24.f )
{
	SceneComponent = CreateDefaultSubobject< USceneComponent >( TEXT("SceneComponent0") );
	SceneComponent->Mobility = EComponentMobility::Static;

	RootComponent = SceneComponent;

#if USE_USD_SDK
	RootUsdTwin.PrimPath = TEXT("/");

	UsdListener.OnPrimChanged.AddLambda(
		[ this ]( const FString& PrimPath, bool bResync )
			{
				TUsdStore< pxr::SdfPath > UsdPrimPath = UnrealToUsd::ConvertPath( *PrimPath );

				TSharedRef< FUsdSchemaTranslationContext > TranslationContext = UsdStageActorImpl::CreateUsdSchemaTranslationContext( this, PrimPath );

				this->LoadAssets( *TranslationContext, this->GetUsdStage()->GetPrimAtPath( UsdPrimPath.Get() ) );

				this->UpdatePrim( UsdPrimPath.Get(), bResync, *TranslationContext );
				TranslationContext->CompleteTasks();

				if ( this->HasAutorithyOverStage() )
				{
					this->OnPrimChanged.Broadcast( PrimPath, bResync );
				}
			}
		);
#endif // #if USE_USD_SDK

	InitLevelSequence(TimeCodesPerSecond);
	SetupLevelSequence();

	if ( HasAutorithyOverStage() )
	{
		FCoreUObjectDelegates::OnObjectPropertyChanged.AddUObject( this, &AUsdStageActor::OnPrimObjectPropertyChanged );
	}
}

AUsdStageActor::~AUsdStageActor()
{
#if USE_USD_SDK
	if ( HasAutorithyOverStage() )
	{
		UnrealUSDWrapper::GetUsdStageCache().Erase( UsdStageStore.Get() );
	}
#endif // #if USE_USD_SDK
}

#if USE_USD_SDK

FUsdPrimTwin* AUsdStageActor::GetOrCreatePrimTwin( const pxr::SdfPath& UsdPrimPath )
{
	const FString PrimPath = UsdToUnreal::ConvertPath( UsdPrimPath );
	const FString ParentPrimPath = UsdToUnreal::ConvertPath( UsdPrimPath.GetParentPath() );

	FUsdPrimTwin* UsdPrimTwin = RootUsdTwin.Find( PrimPath );
	FUsdPrimTwin* ParentUsdPrimTwin = RootUsdTwin.Find( ParentPrimPath );

	const pxr::UsdPrim Prim = GetUsdStage()->GetPrimAtPath( UsdPrimPath );

	if ( !Prim )
	{
		return nullptr;
	}

	if ( !ParentUsdPrimTwin )
	{
		ParentUsdPrimTwin = &RootUsdTwin;
	}

	if ( !UsdPrimTwin )
	{
		UsdPrimTwin = &ParentUsdPrimTwin->AddChild( *PrimPath );

		UsdPrimTwin->OnDestroyed.AddLambda(
			[ this ]( const FUsdPrimTwin& UsdPrimTwin )
			{
				this->OnUsdPrimTwinDestroyed( UsdPrimTwin );
			} );

		if ( !UsdPrimTwin->AnimationHandle.IsValid() && UsdUtils::IsAnimated( Prim ) )
		{
			PrimsToAnimate.Add( PrimPath );
		}
	}

	return UsdPrimTwin;
}

FUsdPrimTwin* AUsdStageActor::ExpandPrim( const pxr::UsdPrim& Prim, FUsdSchemaTranslationContext& TranslationContext )
{
	if ( !Prim )
	{
		return nullptr;
	}

	TRACE_CPUPROFILER_EVENT_SCOPE( AUsdStageActor::ExpandPrim );

	FUsdPrimTwin* UsdPrimTwin = GetOrCreatePrimTwin( Prim.GetPrimPath() );

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

		bExpandChilren = !SchemaTranslator->CollapsedHierarchy();
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
			if ( FUsdPrimTwin* UsdPrimTwin = RootUsdTwin.Find( PrimPath ) )
			{
				UsdPrimTwin->Clear();
			}
		}

		TUsdStore< pxr::UsdPrim > PrimToExpand = GetUsdStage()->GetPrimAtPath( UsdPrimPath );
		FUsdPrimTwin* UsdPrimTwin = ExpandPrim( PrimToExpand.Get(), TranslationContext );

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
		LoadUsdStage();
	}
	else if ( PropertyName == GET_MEMBER_NAME_CHECKED( AUsdStageActor, Time ) )
	{
		Refresh();
	}
	else if ( PropertyName == GET_MEMBER_NAME_CHECKED( AUsdStageActor, StartTimeCode ) || PropertyName == GET_MEMBER_NAME_CHECKED( AUsdStageActor, EndTimeCode ) )
	{
		if ( UsdStageStore.Get() )
		{
			UsdStageStore.Get()->SetStartTimeCode( StartTimeCode );
			UsdStageStore.Get()->SetEndTimeCode( EndTimeCode );

			SetupLevelSequence();
		}
	}
	else
#endif // #if USE_USD_SDK
	{
		Super::PostEditChangeProperty( PropertyChangedEvent );
	}
}

void AUsdStageActor::Clear()
{
	AssetsCache.Reset();
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

void AUsdStageActor::InitLevelSequence(float FramesPerSecond)
{
	if ( LevelSequence || !HasAutorithyOverStage() )
	{
		return;
	}

	// Create LevelSequence
	LevelSequence = NewObject<ULevelSequence>(GetTransientPackage(), NAME_None, DefaultObjFlag | EObjectFlags::RF_Public);
	LevelSequence->Initialize();

	UMovieScene* MovieScene = LevelSequence->MovieScene;

	FFrameRate FrameRate(FramesPerSecond, 1);
	MovieScene->SetDisplayRate(FrameRate);

	// Populate the Sequence
	FGuid ObjectBinding = MovieScene->AddPossessable(GetActorLabel(), this->GetClass());
	LevelSequence->BindPossessableObject(ObjectBinding, *this, GetWorld());

	// Add the Time track
	UMovieSceneFloatTrack* TimeTrack = MovieScene->AddTrack<UMovieSceneFloatTrack>(ObjectBinding);
	TimeTrack->SetPropertyNameAndPath(FName(TEXT("Time")), "Time");

	FFrameRate DestFrameRate = LevelSequence->MovieScene->GetTickResolution();
	FFrameNumber StartFrame = DestFrameRate.AsFrameNumber(StartTimeCode / TimeCodesPerSecond);

	bool bSectionAdded = false;
	UMovieSceneFloatSection* TimeSection = Cast<UMovieSceneFloatSection>(TimeTrack->FindOrAddSection(StartFrame, bSectionAdded));

	TimeSection->EvalOptions.CompletionMode = EMovieSceneCompletionMode::KeepState;
	MovieScene->SetEvaluationType(EMovieSceneEvaluationType::FrameLocked);
}

void AUsdStageActor::SetupLevelSequence()
{
	if (!LevelSequence)
	{
		return;
	}

	UMovieScene* MovieScene = LevelSequence->MovieScene;
	if (!MovieScene)
	{
		return;
	}

	FFrameRate DestFrameRate = MovieScene->GetTickResolution();

	TArray<UMovieSceneSection*> Sections = MovieScene->GetAllSections();
	if (Sections.Num() > 0)
	{
		UMovieSceneSection* TimeSection = Sections[0];
		FMovieSceneFloatChannel* TimeChannel = TimeSection->GetChannelProxy().GetChannel<FMovieSceneFloatChannel>(0);

		FFrameNumber StartFrame = DestFrameRate.AsFrameNumber(StartTimeCode / TimeCodesPerSecond);
		FFrameNumber EndFrame = DestFrameRate.AsFrameNumber(EndTimeCode / TimeCodesPerSecond);

		TArray<FFrameNumber> FrameNumbers;
		FrameNumbers.Add(StartFrame);
		FrameNumbers.Add(EndFrame);

		TArray<FMovieSceneFloatValue> FrameValues;
		FrameValues.Add_GetRef(FMovieSceneFloatValue(StartTimeCode)).InterpMode = ERichCurveInterpMode::RCIM_Linear;
		FrameValues.Add_GetRef(FMovieSceneFloatValue(EndTimeCode)).InterpMode = ERichCurveInterpMode::RCIM_Linear;

		TimeChannel->Set(FrameNumbers, FrameValues);

		TimeSection->SetRange(TRange<FFrameNumber>::All());

		float StartTime = StartTimeCode / TimeCodesPerSecond;
		float EndTime = EndTimeCode / TimeCodesPerSecond;

		TRange<FFrameNumber> TimeRange = TRange<FFrameNumber>::Inclusive(StartFrame, EndFrame);

		LevelSequence->MovieScene->SetPlaybackRange(TimeRange);
		LevelSequence->MovieScene->SetViewRange((StartTimeCode - 5.f) / TimeCodesPerSecond, (EndTimeCode + 5.f) / TimeCodesPerSecond);
		LevelSequence->MovieScene->SetWorkingRange((StartTimeCode - 5.f) / TimeCodesPerSecond, (EndTimeCode + 5.f) / TimeCodesPerSecond);
	}
}

void AUsdStageActor::LoadUsdStage()
{
#if USE_USD_SDK
	double StartTime = FPlatformTime::Cycles64();

	FScopedSlowTask SlowTask( 1.f, LOCTEXT( "LoadingUDStage", "Loading USD Stage") );
	SlowTask.MakeDialog();
	SlowTask.EnterProgressFrame();

	Clear();

	RootUsdTwin.Clear();
	RootUsdTwin.PrimPath = TEXT("/");

	const pxr::UsdStageRefPtr& UsdStage = GetUsdStage();

	if ( !UsdStage )
	{
		OnStageChanged.Broadcast();
		return;
	}

	StartTimeCode = UsdStage->GetStartTimeCode();
	EndTimeCode = UsdStage->GetEndTimeCode();
	TimeCodesPerSecond = UsdStage->GetTimeCodesPerSecond();

	SetupLevelSequence();

	TSharedRef< FUsdSchemaTranslationContext > TranslationContext = UsdStageActorImpl::CreateUsdSchemaTranslationContext( this, RootUsdTwin.PrimPath );

	LoadAssets( *TranslationContext, UsdStage->GetPseudoRoot() );

	UpdatePrim( UsdStage->GetPseudoRoot().GetPrimPath(), true, *TranslationContext );

	TranslationContext->CompleteTasks();

	SetTime( StartTimeCode );

	GEditor->BroadcastLevelActorListChanged();
	GEditor->RedrawLevelEditingViewports();

	OnTimeChanged.AddUObject( this, &AUsdStageActor::AnimatePrims );

	// Log time spent to load the stage
	double ElapsedSeconds = FPlatformTime::ToSeconds64(FPlatformTime::Cycles64() - StartTime);

	int ElapsedMin = int(ElapsedSeconds / 60.0);
	ElapsedSeconds -= 60.0 * (double)ElapsedMin;

	UE_LOG( LogUsdStage, Log, TEXT("%s %s in [%d min %.3f s]"), TEXT("Stage loaded"), *FPaths::GetBaseFilename( RootLayer.FilePath ), ElapsedMin, ElapsedSeconds );
#endif // #if USE_USD_SDK
}

void AUsdStageActor::Refresh() const
{
	if ( HasAutorithyOverStage() )
	{
		OnTimeChanged.Broadcast();
	}
}

void AUsdStageActor::PostRegisterAllComponents()
{
	Super::PostRegisterAllComponents();

#if USE_USD_SDK
	if ( HasAutorithyOverStage() )
	{
		LoadUsdStage();
	}
	else
	{
		const pxr::UsdStageRefPtr& UsdStage = GetUsdStage();

		if ( UsdStage )
		{
			TSharedRef< FUsdSchemaTranslationContext > TranslationContext = UsdStageActorImpl::CreateUsdSchemaTranslationContext( this, RootUsdTwin.PrimPath );

			LoadAssets( *TranslationContext, UsdStage->GetPseudoRoot() );
			UpdatePrim( UsdStage->GetPseudoRoot().GetPrimPath(), true, *TranslationContext );
		}
	}
#endif // #if USE_USD_SDK
}

void AUsdStageActor::PostLoad()
{
	Super::PostLoad();

	if ( HasAutorithyOverStage() )
	{
		OnActorLoaded.Broadcast( this );
	}
}

void AUsdStageActor::OnUsdPrimTwinDestroyed( const FUsdPrimTwin& UsdPrimTwin )
{
	TArray<FDelegateHandle> DelegateHandles;
	PrimDelegates.MultiFind( UsdPrimTwin.PrimPath, DelegateHandles );

	for ( FDelegateHandle Handle : DelegateHandles )
	{
		OnTimeChanged.Remove( Handle );
	}

	PrimDelegates.Remove( UsdPrimTwin.PrimPath );
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

	if ( FUsdPrimTwin* UsdPrimTwin = RootUsdTwin.Find( PrimPath ) )
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
void AUsdStageActor::LoadAssets( FUsdSchemaTranslationContext& TranslationContext, const pxr::UsdPrim& StartPrim )
{
	auto CreateAssetsForPrims = [ &TranslationContext ]( const TArray< TUsdStore< pxr::UsdPrim > >& AllPrimAssets )
	{
		IUsdSchemasModule& UsdSchemasModule = FModuleManager::Get().LoadModuleChecked< IUsdSchemasModule >( TEXT("USDSchemas") );

		for ( const TUsdStore< pxr::UsdPrim >& UsdPrim : AllPrimAssets )
		{
			if ( TSharedPtr< FUsdSchemaTranslator > SchemaTranslator = UsdSchemasModule.GetTranslatorRegistry().CreateTranslatorForSchema( TranslationContext.AsShared(), pxr::UsdTyped( UsdPrim.Get() ) ) )
			{
				SchemaTranslator->CreateAssets();
			}
		}

		TranslationContext.CompleteTasks(); // Finish the assets tasks before moving on
	};

	// Load materials first since meshes are referencing them
	TArray< TUsdStore< pxr::UsdPrim > > AllPrimAssets = UsdUtils::GetAllPrimsOfType( StartPrim, pxr::TfType::Find< pxr::UsdShadeMaterial >() );
	
	CreateAssetsForPrims( AllPrimAssets );

	// Load meshes
	AllPrimAssets = UsdUtils::GetAllPrimsOfType( StartPrim, pxr::TfType::Find< pxr::UsdGeomMesh >(), { pxr::TfType::Find< pxr::UsdSkelRoot >() } );
	AllPrimAssets.Append( UsdUtils::GetAllPrimsOfType( StartPrim, pxr::TfType::Find< pxr::UsdSkelRoot >() ) );
	
	CreateAssetsForPrims( AllPrimAssets );
}

void AUsdStageActor::AnimatePrims()
{
	TSharedRef< FUsdSchemaTranslationContext > TranslationContext = UsdStageActorImpl::CreateUsdSchemaTranslationContext( this, RootUsdTwin.PrimPath );

	for ( const FString& PrimToAnimate : PrimsToAnimate )
	{
		TUsdStore< pxr::SdfPath > PrimPath = UnrealToUsd::ConvertPath( *PrimToAnimate );

		IUsdSchemasModule& SchemasModule = FModuleManager::Get().LoadModuleChecked< IUsdSchemasModule >( "USDSchemas" );
		if ( TSharedPtr< FUsdSchemaTranslator > SchemaTranslator = SchemasModule.GetTranslatorRegistry().CreateTranslatorForSchema( TranslationContext, pxr::UsdTyped( this->GetUsdStage()->GetPrimAtPath( PrimPath.Get() ) ) ) )
		{
			if ( FUsdPrimTwin* UsdPrimTwin = RootUsdTwin.Find( PrimToAnimate ) )
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