// Copyright Epic Games, Inc. All Rights Reserved.

#include "DataprepCoreLibrary.h"

#include "DataprepActionAsset.h"
#include "DataprepAssetInterface.h"
#include "DataprepAssetProducers.h"
#include "DataprepAssetUserData.h"
#include "DataPrepContentConsumer.h"
#include "DataPrepContentProducer.h"
#include "DataprepCoreLogCategory.h"
#include "DataprepCorePrivateUtils.h"
#include "DataprepCoreUtils.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectIterator.h"

#define LOCTEXT_NAMESPACE "DataprepCoreLibrary"

namespace DataprepCoreLibraryUtils
{
#ifdef LOG_TIME
	class FTimeLogger
	{
	public:
		FTimeLogger(const FString& InText)
			: StartTime( FPlatformTime::Cycles64() )
			, Text( InText )
		{
			UE_LOG( LogDataprepCore, Log, TEXT("%s ..."), *Text );
		}

		~FTimeLogger()
		{
			// Log time spent to import incoming file in minutes and seconds
			double ElapsedSeconds = FPlatformTime::ToSeconds64(FPlatformTime::Cycles64() - StartTime);

			int ElapsedMin = int(ElapsedSeconds / 60.0);
			ElapsedSeconds -= 60.0 * (double)ElapsedMin;
			UE_LOG( LogDataprepCore, Log, TEXT("%s took [%d min %.3f s]"), *Text, ElapsedMin, ElapsedSeconds );
		}

	private:
		uint64 StartTime;
		FString Text;
	};
#endif // LOG_TIME
}

void UDataprepCoreLibrary::Execute( UDataprepAssetInterface* DataprepAssetInterface, TArray<AActor*>& ActorsCreated, TArray<UObject*>& AssetsCreated  )
{
	ActorsCreated.Empty();
	AssetsCreated.Empty();

	if( DataprepAssetInterface )
	{
#ifdef LOG_TIME
		DataprepCoreLibraryUtils::FTimeLogger TimeLogger(TEXT("UDataprepCoreLibrary::Execute"));
#endif

		TSharedPtr<IDataprepLogger> Logger( new FDataprepCoreUtils::FDataprepLogger );
		TSharedPtr<IDataprepProgressReporter> Reporter( new FDataprepCoreUtils::FDataprepProgressTextReporter );

		if( Execute_Internal(DataprepAssetInterface, Logger, Reporter) )
		{
			// Objects created by the Dataprep asset have UDataprepAssetUserData as their asset user data
			auto IsFromExecution = [&DataprepAssetInterface](UObject* Object) -> bool {

				if( Object && Object->GetClass()->ImplementsInterface( UInterface_AssetUserData::StaticClass() ) )
				{
					if( IInterface_AssetUserData* AssetUserDataInterface = Cast< IInterface_AssetUserData >(Object) )
					{
						if(UDataprepAssetUserData* UserData = AssetUserDataInterface->GetAssetUserData<UDataprepAssetUserData>())
						{
							if(UserData->DataprepAssetPtr == DataprepAssetInterface)
							{
								return true;
							}
						}
					}
				}

				return false;
			};

			for(FObjectIterator ObjIter; ObjIter; ++ObjIter)
			{
				UObject* Object = *ObjIter;

				if( AActor* Actor = Cast<AActor>(Object) )
				{
					// Add actors which have added to the LevelEditor's world
					if( Actor->GetWorld() == GWorld && IsFromExecution( Actor->GetRootComponent() ) )
					{
						ActorsCreated.Add( Actor );
					}
				}
				// Add assets which are not transient
				else if( Object && Object->GetOutermost()->GetPathName().StartsWith(TEXT("/Game")) && Object->IsAsset() )
				{
					if( IsFromExecution( Object ) )
					{
						AssetsCreated.Add( Object );
					}
				}
			}
		}
	}
}

bool UDataprepCoreLibrary::ExecuteWithReporting( UDataprepAssetInterface* DataprepAssetInterface )
{
#ifdef LOG_TIME
	DataprepCoreLibraryUtils::FTimeLogger TimeLogger(TEXT("UDataprepCoreLibrary::ExecuteWithReporting"));
#endif

	TSharedPtr<IDataprepLogger> Logger( new FDataprepCoreUtils::FDataprepLogger );
	TSharedPtr<IDataprepProgressReporter> Reporter( new FDataprepCoreUtils::FDataprepProgressUIReporter() );
	return Execute_Internal( DataprepAssetInterface, Logger, Reporter );
}

bool UDataprepCoreLibrary::Execute_Internal(UDataprepAssetInterface* DataprepAssetInterface, TSharedPtr<IDataprepLogger>& Logger, TSharedPtr<IDataprepProgressReporter>& Reporter )
{
	if(DataprepAssetInterface != nullptr)
	{
		// The temporary folders are used for the whole session of the Unreal Editor
		static FString RelativeTempFolder = FString::FromInt( FPlatformProcess::GetCurrentProcessId() ) / FGuid::NewGuid().ToString();
		static FString TransientContentFolder = DataprepCorePrivateUtils::GetRootPackagePath() / RelativeTempFolder;

		// Create transient world to host data from producer
		FName UniqueWorldName = MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), FName( *(LOCTEXT("TransientWorld", "Preview").ToString()) ));
		TStrongObjectPtr<UWorld> TransientWorld = TStrongObjectPtr<UWorld>( NewObject< UWorld >( GetTransientPackage(), UniqueWorldName ) );
		TransientWorld->WorldType = EWorldType::EditorPreview;

		FWorldContext& WorldContext = GEngine->CreateNewWorldContext(TransientWorld->WorldType);
		WorldContext.SetCurrentWorld(TransientWorld.Get());

		TransientWorld->InitializeNewWorld(UWorld::InitializationValues()
			.AllowAudioPlayback(false)
			.CreatePhysicsScene(false)
			.RequiresHitProxies(false)
			.CreateNavigation(false)
			.CreateAISystem(false)
			.ShouldSimulatePhysics(false)
			.SetTransactional(false));

		TArray<TWeakObjectPtr<UObject>> Assets;

		FText DataprepAssetTextName = FText::FromString( DataprepAssetInterface->GetName() );
		FText TaskDescription = FText::Format( LOCTEXT( "ExecutingDataprepAsset", "Executing Dataprep Asset \"{0}\" ..."), DataprepAssetTextName );
		FDataprepWorkReporter ProgressTask( Reporter, TaskDescription, 3.0f, 1.0f );

		bool bSuccessfulExecute = true;

		// Run the producers
		{
			// Create package to pass to the producers
			UPackage* TransientPackage = NewObject< UPackage >( nullptr, *TransientContentFolder, RF_Transient );
			TransientPackage->FullyLoad();

			TSharedPtr< FDataprepCoreUtils::FDataprepFeedbackContext > FeedbackContext( new FDataprepCoreUtils::FDataprepFeedbackContext );

			FDataprepProducerContext Context;
			Context.SetWorld( TransientWorld.Get() )
				.SetRootPackage( TransientPackage )
				.SetLogger( Logger)
				.SetProgressReporter( Reporter );
			
			FText Message = FText::Format( LOCTEXT( "Running_Producers", "Running \"{0}\'s Producers ..."), DataprepAssetTextName );
			ProgressTask.ReportNextStep( Message );
			Assets = DataprepAssetInterface->GetProducers()->Produce( Context );
		}

		// Trigger execution of data preparation operations on world attached to recipe
		TSet< TWeakObjectPtr< UObject > > CachedAssets;
		{
			DataprepActionAsset::FCanExecuteNextStepFunc CanExecuteNextStepFunc = [](UDataprepActionAsset* ActionAsset, UDataprepOperation* Operation, UDataprepFilter* Filter) -> bool
			{
				return true;
			};

			DataprepActionAsset::FActionsContextChangedFunc ActionsContextChangedFunc = [&](const UDataprepActionAsset* ActionAsset, bool bWorldChanged, bool bAssetsChanged, const TArray< TWeakObjectPtr<UObject> >& NewAssets)
			{
			};

			TSharedPtr<FDataprepActionContext> ActionsContext = MakeShareable( new FDataprepActionContext() );

			ActionsContext->SetTransientContentFolder( TransientContentFolder / TEXT("Pipeline") )
				.SetLogger( Logger )
				.SetProgressReporter( Reporter )
				.SetCanExecuteNextStep( CanExecuteNextStepFunc )
				.SetActionsContextChanged( ActionsContextChangedFunc )
				.SetWorld( TransientWorld.Get() )
				.SetAssets( Assets );

			FText Message = FText::Format( LOCTEXT( "Executing_Recipe", "Executing \"{0}\'s Recipe ..."), DataprepAssetTextName );
			ProgressTask.ReportNextStep( Message );
			DataprepAssetInterface->ExecuteRecipe( ActionsContext );

			// Update list of assets with latest ones
			Assets = ActionsContext->Assets.Array();

			for( TWeakObjectPtr<UObject>& Asset : Assets )
			{
				if( Asset.IsValid() )
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

			FText Message = FText::Format( LOCTEXT( "Running_Consumer", "Running \"{0}\'s Consumer ..."), DataprepAssetTextName );
			ProgressTask.ReportNextStep( Message );

			bSuccessfulExecute = DataprepAssetInterface->RunConsumer( Context );
		}

		// Clean all temporary data created by the Dataprep asset
		{
			// Delete all actors of the transient world
			TArray<AActor*> TransientActors;
			DataprepCorePrivateUtils::GetActorsFromWorld( TransientWorld.Get(), TransientActors );
			for( AActor* Actor : TransientActors )
			{
				if (Actor && !Actor->IsPendingKill() )
				{
					TransientWorld->EditorDestroyActor(Actor, true);

					// Since deletion can be delayed, rename to avoid future name collision
					// Call UObject::Rename directly on actor to avoid AActor::Rename which unnecessarily sunregister and re-register components
					Actor->UObject::Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_ForceNoResetLoaders);
				}
			}

			// Delete assets which are still in the transient content folder
			TArray<UObject*> ObjectsToDelete;
			for( TWeakObjectPtr<UObject>& Asset : CachedAssets )
			{
				if( UObject* ObjectToDelete = Asset.Get() )
				{
					FString PackagePath = ObjectToDelete->GetOutermost()->GetName();
					if( PackagePath.StartsWith( TransientContentFolder ) )
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

#undef LOCTEXT_NAMESPACE
