// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "DataprepCoreUtils.h"

#ifdef NEW_DATASMITHSCENE_WORKFLOW
#include "DataprepAssetUserData.h"
#endif

#include "DataprepAsset.h"
#include "DataprepAssetInterface.h"
#include "DataprepContentConsumer.h"
#include "DataprepContentProducer.h"
#include "DataprepCoreLogCategory.h"
#include "DataprepCorePrivateUtils.h"
#include "IDataprepProgressReporter.h"

#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "HAL/FileManager.h"
#include "LevelSequence.h"
#include "MaterialGraph/MaterialGraph.h"
#include "MaterialShared.h"
#include "Materials/Material.h"
#include "Materials/MaterialFunction.h"
#include "Materials/MaterialInstance.h"
#include "Misc/ScopedSlowTask.h"
#include "RenderingThread.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectHash.h"

#if WITH_EDITOR
#include "ComponentRecreateRenderStateContext.h"
#include "Editor.h"
#include "ObjectTools.h"
#include "Subsystems/AssetEditorSubsystem.h"
#endif

#define LOCTEXT_NAMESPACE "DataprepCoreUtils"

UDataprepAsset* FDataprepCoreUtils::GetDataprepAssetOfObject(UObject* Object)
{
	while ( Object )
	{
		if ( UDataprepAsset::StaticClass() == Object->GetClass() )
		{
			return static_cast<UDataprepAsset*>( Object );
		}
		Object = Object->GetOuter();
	}

	return nullptr;
}

void FDataprepCoreUtils::PurgeObjects(TArray<UObject*> InObjects)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FDataprepCoreUtils::PurgeObjects);

	// Build an array of unique objects
	TSet<UObject*> ObjectsSet;
	ObjectsSet.Reserve( InObjects.Num() );
	ObjectsSet.Append( InObjects );
	TArray<UObject*> Objects( ObjectsSet.Array() );

	TArray<UObject*> ObjectsToPurge;
	ObjectsToPurge.Reserve( Objects.Num() );
#if WITH_EDITOR
	TArray<UObject*> PublicObjectsToPurge;
	PublicObjectsToPurge.Reserve( Objects.Num() );
#endif // WITH_EDITOR

	auto MakeObjectPurgeable = [&ObjectsToPurge](UObject* InObject)
	{
#if WITH_EDITOR
		if ( InObject->IsAsset() )
		{
			GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->CloseAllEditorsForAsset( InObject );
		}
#endif // WITH_EDITOR
		if ( InObject->IsRooted() )
		{
			InObject->RemoveFromRoot();
		}

		InObject->ClearFlags( RF_Public | RF_Standalone );
		InObject->MarkPendingKill();
		ObjectsToPurge.Add( InObject );
	};

	auto MakeSourceObjectPurgeable = [MakeObjectPurgeable](UObject* InSourceObject)
	{
		MakeObjectPurgeable( InSourceObject );
		ForEachObjectWithOuter( InSourceObject, [MakeObjectPurgeable](UObject* InObject)
		{
			MakeObjectPurgeable( InObject );
		});
	};

	// Clean-up any in-memory packages that should be purged and check if we are purging the current map
	ELogVerbosity::Type PrevLogStaticMeshVerbosity = LogStaticMesh.GetVerbosity();
	LogStaticMesh.SetVerbosity( ELogVerbosity::Error );
	for ( UObject* Object : Objects )
	{
		if (Object)
		{
#if WITH_EDITOR
			/**
			 * Add object for reference removal if it's public
			 * This is used to emulate the workflow that is used by the editor when deleting a asset.
			 * Due to the transient package we can't simply use IsAsset()
			 */
			if ( Object->HasAnyFlags( RF_Public ) )
			{
				PublicObjectsToPurge.Add( Object );
			}
#endif // WITH_EDITOR

			MakeSourceObjectPurgeable( Object );
		}
	}
	// Restore LogStaticMesh verbosity
	LogStaticMesh.SetVerbosity( PrevLogStaticMeshVerbosity );

	/** 
	 * If we have any public object that were made purgeable, null out their references so we can safely garbage collect
	 * Additionally, ObjectTools::ForceReplaceReferences is calling PreEditChange and PostEditChange on all impacted objects.
	 * Consequently, making sure async tasks processing those objects are notified and act accordingly.
	 * This is the way to make sure that all dependencies are taken in account and properly handled
	 */
#if WITH_EDITOR
	if ( PublicObjectsToPurge.Num() > 0 )
	{
		/**
		 * Due to way that some render proxy are created we must remove the current rendering scene.
		 * This is to ensure that the render proxies won't have a dangling pointer to an asset while removing then on the next tick
		 */
		FGlobalComponentRecreateRenderStateContext RefreshRendering;
		ObjectTools::ForceReplaceReferences(nullptr, PublicObjectsToPurge);

		// Ensure that all the rendering commands were processed before doing the garbage collection (see above comment)
		FlushRenderingCommands();
	}
#endif // WITH_EDITOR

	// if we have object to purge but the map isn't one of them collect garbage (if we purged the map it has already been done)
	if ( ObjectsToPurge.Num() > 0 )
	{
		CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
	}
}

bool FDataprepCoreUtils::IsAsset(UObject* Object)
{
	const bool bHasValidObjectFlags = Object && !Object->HasAnyFlags(RF_ClassDefaultObject) && Object->HasAnyFlags(RF_Public) && !Object->IsPendingKill();

	if(!bHasValidObjectFlags)
	{
		// Otherwise returns true if Object is of the supported classes
		return Object && (Object->IsA<UStaticMesh>() || Object->IsA<UMaterialInterface>() || Object->IsA<UTexture>() || Object->IsA<ULevelSequence>() || Object->IsAsset());
	}

	return true;
}

bool FDataprepCoreUtils::ExecuteDataprep(UDataprepAssetInterface* DataprepAssetInterface, const TSharedPtr<IDataprepLogger>& Logger, const TSharedPtr<IDataprepProgressReporter>& Reporter)
{
	if ( DataprepAssetInterface != nullptr )
	{
		// The temporary folders are used for the whole session of the Unreal Editor
		static FString RelativeTempFolder = FString::FromInt( FPlatformProcess::GetCurrentProcessId() ) / FGuid::NewGuid().ToString();
		static FString TransientContentFolder = DataprepCorePrivateUtils::GetRootPackagePath() / RelativeTempFolder;

		// Create transient world to host data from producer
		FName UniqueWorldName = MakeUniqueObjectName( GetTransientPackage(), UWorld::StaticClass(), FName( *( LOCTEXT("TransientWorld", "Preview").ToString() ) ) );
		TStrongObjectPtr<UWorld> TransientWorld = TStrongObjectPtr<UWorld>( NewObject<UWorld>( GetTransientPackage(), UniqueWorldName ) );
		TransientWorld->WorldType = EWorldType::EditorPreview;

		FWorldContext& WorldContext = GEngine->CreateNewWorldContext( TransientWorld->WorldType );
		WorldContext.SetCurrentWorld( TransientWorld.Get() );

		TransientWorld->InitializeNewWorld( UWorld::InitializationValues()
			.AllowAudioPlayback( false )
			.CreatePhysicsScene( false )
			.RequiresHitProxies( false )
			.CreateNavigation( false )
			.CreateAISystem( false )
			.ShouldSimulatePhysics( false )
			.SetTransactional( false )
			);

		TArray<TWeakObjectPtr<UObject>> Assets;

		FText DataprepAssetTextName = FText::FromString( DataprepAssetInterface->GetName() );
		FText TaskDescription = FText::Format( LOCTEXT("ExecutingDataprepAsset", "Executing Dataprep Asset \"{0}\" ..."), DataprepAssetTextName );
		FDataprepWorkReporter ProgressTask( Reporter, TaskDescription, 3.0f, 1.0f );

		bool bSuccessfulExecute = true;

		// Run the producers
		{
			// Create package to pass to the producers
			UPackage* TransientPackage = NewObject<UPackage>( nullptr, *TransientContentFolder, RF_Transient );
			TransientPackage->FullyLoad();

			TSharedPtr<FDataprepCoreUtils::FDataprepFeedbackContext> FeedbackContext = MakeShared<FDataprepCoreUtils::FDataprepFeedbackContext>();

			FDataprepProducerContext Context;
			Context.SetWorld( TransientWorld.Get() )
				.SetRootPackage( TransientPackage )
				.SetLogger( Logger )
				.SetProgressReporter( Reporter );

			FText Message = FText::Format( LOCTEXT("Running_Producers", "Running \"{0}\'s Producers ..."), DataprepAssetTextName );
			ProgressTask.ReportNextStep( Message );
			Assets = DataprepAssetInterface->GetProducers()->Produce( Context );
		}

		// Trigger execution of data preparation operations on world attached to recipe
		TSet<TWeakObjectPtr<UObject>> CachedAssets;
		{
			DataprepActionAsset::FCanExecuteNextStepFunc CanExecuteNextStepFunc = [](UDataprepActionAsset* ActionAsset, UDataprepOperation* Operation, UDataprepFilter* Filter) -> bool
			{
				return true;
			};


			TSharedPtr<FDataprepActionContext> ActionsContext = MakeShared<FDataprepActionContext>();

			ActionsContext->SetTransientContentFolder( TransientContentFolder / TEXT("Pipeline") )
				.SetLogger( Logger )
				.SetProgressReporter( Reporter )
				.SetCanExecuteNextStep( CanExecuteNextStepFunc )
				.SetWorld( TransientWorld.Get() )
				.SetAssets( Assets );

			FText Message = FText::Format( LOCTEXT("Executing_Recipe", "Executing \"{0}\'s Recipe ..."), DataprepAssetTextName );
			ProgressTask.ReportNextStep( Message );
			DataprepAssetInterface->ExecuteRecipe( ActionsContext );

			// Update list of assets with latest ones
			Assets = ActionsContext->Assets.Array();

			for ( TWeakObjectPtr<UObject>& Asset : Assets )
			{
				if ( Asset.IsValid() )
				{
					CachedAssets.Add( Asset );
				}
			}

		}

		// Run consumer to output result of recipe
		{
			FDataprepConsumerContext Context;
			Context.SetWorld( TransientWorld.Get() )
				.SetAssets( Assets )
				.SetTransientContentFolder( TransientContentFolder )
				.SetLogger( Logger )
				.SetProgressReporter( Reporter );

			FText Message = FText::Format( LOCTEXT("Running_Consumer", "Running \"{0}\'s Consumer ..."), DataprepAssetTextName );
			ProgressTask.ReportNextStep( Message );

			bSuccessfulExecute = DataprepAssetInterface->RunConsumer( Context );
		}

		// Clean all temporary data created by the Dataprep asset
		{
			// Delete all actors of the transient world
			TArray<AActor*> TransientActors;
			DataprepCorePrivateUtils::GetActorsFromWorld( TransientWorld.Get(), TransientActors );
			for ( AActor* Actor : TransientActors )
			{
				if ( Actor && !Actor->IsPendingKill() )
				{
					TransientWorld->EditorDestroyActor( Actor, true );

					// Since deletion can be delayed, rename to avoid future name collision
					// Call UObject::Rename directly on actor to avoid AActor::Rename which unnecessarily sunregister and re-register components
					Actor->UObject::Rename( nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_ForceNoResetLoaders );
				}
			}

			// Delete assets which are still in the transient content folder
			TArray<UObject*> ObjectsToDelete;
			for ( TWeakObjectPtr<UObject>& Asset : CachedAssets )
			{
				if ( UObject* ObjectToDelete = Asset.Get() )
				{
					FString PackagePath = ObjectToDelete->GetOutermost()->GetName();
					if ( PackagePath.StartsWith( TransientContentFolder ) )
					{
						FDataprepCoreUtils::MoveToTransientPackage( ObjectToDelete );
						ObjectsToDelete.Add( ObjectToDelete );
					}
				}
			}

			// Disable warnings from LogStaticMesh because FDataprepCoreUtils::PurgeObjects is pretty verbose on harmless warnings
			ELogVerbosity::Type PrevLogStaticMeshVerbosity = LogStaticMesh.GetVerbosity();
			LogStaticMesh.SetVerbosity( ELogVerbosity::Error );

			FDataprepCoreUtils::PurgeObjects( MoveTemp( ObjectsToDelete ) );

			// Restore LogStaticMesh verbosity
			LogStaticMesh.SetVerbosity( PrevLogStaticMeshVerbosity );

			// Erase all temporary files created by the Dataprep asset
			static FString AbsolutePath = FPaths::ConvertRelativePathToFull( DataprepCorePrivateUtils::GetRootTemporaryDir() / RelativeTempFolder );
			IFileManager::Get().DeleteDirectory( *AbsolutePath, false, true );
		}

		return bSuccessfulExecute;
	}

	return false;
}

FDataprepWorkReporter::FDataprepWorkReporter(const TSharedPtr<IDataprepProgressReporter>& InReporter, const FText& InDescription, float InAmountOfWork, float InIncrementOfWork, bool bInterruptible )
	: Reporter( InReporter )
	, DefaultIncrementOfWork( InIncrementOfWork )
{
	if( Reporter.IsValid())
	{
		Reporter->BeginWork( InDescription, InAmountOfWork, bInterruptible );
	}
}

FDataprepWorkReporter::~FDataprepWorkReporter()
{
	if( Reporter.IsValid() )
	{
		Reporter->EndWork();
	}
}

void FDataprepWorkReporter::ReportNextStep(const FText & InMessage, float InIncrementOfWork )
{
	if( Reporter.IsValid() )
	{
		Reporter->ReportProgress( InIncrementOfWork, InMessage );
	}
}

bool FDataprepWorkReporter::IsWorkCancelled() const
{
	if ( Reporter.IsValid() )
	{
		return Reporter->IsWorkCancelled();
	}
	return false;
}

#ifdef NEW_DATASMITHSCENE_WORKFLOW
void FDataprepCoreUtils::AddDataprepAssetUserData(UObject* Target, UDataprepAssetInterface* DataprepAssetInterface)
{
	if (!Target || !Target->GetClass()->ImplementsInterface(UInterface_AssetUserData::StaticClass()))
	{
		return;
	}

	if (Target->GetClass()->IsChildOf(AActor::StaticClass()))
	{
		// The root Component holds AssetUserData on behalf of the actor
		Target = Cast<AActor>(Target)->GetRootComponent();
	}

	if(IInterface_AssetUserData* AssetUserDataInterface = Cast< IInterface_AssetUserData >( Target ))
	{
		UDataprepAssetUserData* UserData = AssetUserDataInterface->GetAssetUserData< UDataprepAssetUserData >();

		if(!UserData)
		{
			EObjectFlags Flags = RF_Public /*| RF_Transactional*/; // RF_Transactional Disabled as is can cause a crash in the transaction system for blueprints

			UserData = NewObject< UDataprepAssetUserData >(Target, NAME_None, Flags);

			AssetUserDataInterface->AddAssetUserData( UserData );
		}

		UserData->DataprepAssetPtr = DataprepAssetInterface;
	}
}
#endif

void FDataprepCoreUtils::FDataprepLogger::LogInfo(const FText& InLogText, const UObject& InObject)
{
	UE_LOG( LogDataprepCore, Log, TEXT("%s : %s"), *InObject.GetName(), *InLogText.ToString() );
}

void FDataprepCoreUtils::FDataprepLogger::LogWarning(const FText& InLogText, const UObject& InObject)
{
	UE_LOG( LogDataprepCore, Warning, TEXT("%s : %s"), *InObject.GetName(), *InLogText.ToString() );
}

void FDataprepCoreUtils::FDataprepLogger::LogError(const FText& InLogText,  const UObject& InObject)
{
	UE_LOG( LogDataprepCore, Error, TEXT("%s : %s"), *InObject.GetName(), *InLogText.ToString() );
}

void FDataprepCoreUtils::FDataprepProgressUIReporter::BeginWork( const FText& InTitle, float InAmountOfWork, bool bInterruptible )
{
	ProgressTasks.Emplace( new FScopedSlowTask( InAmountOfWork, InTitle, true, FeedbackContext.IsValid() ? *FeedbackContext.Get() : *GWarn ) );
	ProgressTasks.Last()->MakeDialog( bInterruptible );
}

void FDataprepCoreUtils::FDataprepProgressUIReporter::EndWork()
{
	if(ProgressTasks.Num() > 0)
	{
		ProgressTasks.Pop();
	}
}

void FDataprepCoreUtils::FDataprepProgressUIReporter::ReportProgress( float Progress, const FText& InMessage )
{
	if( ProgressTasks.Num() > 0 )
	{
		TSharedPtr<FScopedSlowTask>& ProgressTask = ProgressTasks.Last();
		ProgressTask->EnterProgressFrame( Progress, InMessage );
	}
}

bool FDataprepCoreUtils::FDataprepProgressUIReporter::IsWorkCancelled()
{
	if( !bIsCancelled && ProgressTasks.Num() > 0 )
	{
		const TSharedPtr<FScopedSlowTask>& ProgressTask = ProgressTasks.Last();
		bIsCancelled |= ProgressTask->ShouldCancel();
	}
	return bIsCancelled;
}

FFeedbackContext* FDataprepCoreUtils::FDataprepProgressUIReporter::GetFeedbackContext() const
{
	return FeedbackContext.IsValid() ? FeedbackContext.Get() : GWarn;
}

void FDataprepCoreUtils::FDataprepProgressTextReporter::BeginWork( const FText& InTitle, float InAmountOfWork, bool /*bInterruptible*/ )
{
	UE_LOG( LogDataprepCore, Log, TEXT("Start: %s ..."), *InTitle.ToString() );
	++TaskDepth;
}

void FDataprepCoreUtils::FDataprepProgressTextReporter::EndWork()
{
	if(TaskDepth > 0)
	{
		--TaskDepth;
	}
}

void FDataprepCoreUtils::FDataprepProgressTextReporter::ReportProgress( float Progress, const FText& InMessage )
{
	if( TaskDepth > 0 )
	{
		UE_LOG( LogDataprepCore, Log, TEXT("Doing %s ..."), *InMessage.ToString() );
	}
}

bool FDataprepCoreUtils::FDataprepProgressTextReporter::IsWorkCancelled()
{
	return false;
}

FFeedbackContext* FDataprepCoreUtils::FDataprepProgressTextReporter::GetFeedbackContext() const
{
	return FeedbackContext.Get();
}

void FDataprepCoreUtils::BuildAssets(const TArray<TWeakObjectPtr<UObject>>& Assets, const TSharedPtr<IDataprepProgressReporter>& ProgressReporterPtr)
{
	TSet<UStaticMesh*> StaticMeshes;
	TSet<UMaterialInterface*> MaterialInterfaces;

	for( const TWeakObjectPtr<UObject>& AssetPtr : Assets )
	{
		UObject* AssetObject = AssetPtr.Get();
		if( AssetObject )
		{
			if(UMaterialInterface* MaterialInterface = Cast<UMaterialInterface>(AssetObject))
			{
				MaterialInterfaces.Add( MaterialInterface );
				if(MaterialInterface->GetMaterial())
				{
					MaterialInterfaces.Add( MaterialInterface->GetMaterial() );
				}
			}
			else if(UStaticMesh* StaticMesh = Cast<UStaticMesh>(AssetObject))
			{
				StaticMeshes.Add( StaticMesh );
			}
		}
	}

	int32 AssetToBuildCount = MaterialInterfaces.Num() + StaticMeshes.Num();
	FDataprepWorkReporter Task( ProgressReporterPtr, LOCTEXT( "BuildAssets_Building", "Building assets ..." ), (float)AssetToBuildCount, 1.0f, false );

	// Force compilation of materials which have no render proxy
	if(MaterialInterfaces.Num() > 0)
	{
		auto MustCompile = [](const UMaterialInterface* MaterialInterface)
		{
			if( MaterialInterface )
			{
				const FMaterialRenderProxy* RenderProxy = MaterialInterface->GetRenderProxy();
				return RenderProxy == nullptr || !RenderProxy->IsInitialized();
			}

			return false;
		};

		int32 AssetBuiltCount = 0;
		AssetToBuildCount = MaterialInterfaces.Num();

		for(UMaterialInterface* MaterialInterface : MaterialInterfaces)
		{
			++AssetBuiltCount;
			Task.ReportNextStep(FText::Format(LOCTEXT( "BuildAssets_Building_Materials", "Building materials ({0} / {1})" ), AssetBuiltCount, AssetToBuildCount), 1.0f);

			if( MustCompile( MaterialInterface ) )
			{
				FPropertyChangedEvent EmptyPropertyUpdateStruct( nullptr );
				MaterialInterface->PostEditChangeProperty( EmptyPropertyUpdateStruct );
			}
		}
	}

	// Build static meshes
	int32 AssetBuiltCount = 0;
	AssetToBuildCount = StaticMeshes.Num();
	DataprepCorePrivateUtils::BuildStaticMeshes(StaticMeshes, [&](UStaticMesh* StaticMesh) -> bool {
		++AssetBuiltCount;
		Task.ReportNextStep(FText::Format(LOCTEXT( "BuildAssets_Building_Meshes", "Building static meshes ({0} / {1})" ), AssetBuiltCount, AssetToBuildCount), 1.0f);
		return true;
	});
}

#undef LOCTEXT_NAMESPACE