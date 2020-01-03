// Copyright Epic Games, Inc. All Rights Reserved.

#include "DataPrepEditor.h"

#include "DataPrepOperation.h"

#include "DataprepAssetInstance.h"
#include "DataPrepContentConsumer.h"
#include "DataPrepContentProducer.h"
#include "DataPrepEditorActions.h"
#include "DataPrepEditorModule.h"
#include "DataprepEditorStyle.h"
#include "DataPrepRecipe.h"
#include "DataprepActionAsset.h"
#include "DataprepCoreUtils.h"
#include "DataprepEditorLogCategory.h"
#include "SchemaActions/DataprepOperationMenuActionCollector.h"
#include "Widgets/DataprepAssetView.h"
#include "Widgets/SAssetsPreviewWidget.h"
#include "Widgets/SDataprepEditorViewport.h"
#include "Widgets/SDataprepPalette.h"

#include "ActorEditorUtils.h"
#include "ActorTreeItem.h"
#include "AssetDeleteModel.h"
#include "AssetRegistryModule.h"
#include "StatsViewerModule.h"
#include "BlueprintNodeSpawner.h"
#include "ComponentTreeItem.h"
#include "DesktopPlatformModule.h"
#include "Dialogs/Dialogs.h"
#include "Dialogs/DlgPickPath.h"
#include "Editor.h"
#include "EditorDirectories.h"
#include "EditorStyleSet.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "GenericPlatform/GenericPlatformTime.h"
#include "HAL/FileManager.h"
#include "ICustomSceneOutliner.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Materials/MaterialInstance.h"
#include "Misc/Attribute.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Misc/ScopedSlowTask.h"
#include "Modules/ModuleManager.h"
#include "ObjectTools.h"
#include "ScopedTransaction.h"
#include "Templates/UnrealTemplate.h"
#include "Toolkits/IToolkit.h"
#include "UObject/Object.h"
#include "UnrealEdGlobals.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/STextComboBox.h"
#include "Widgets/Layout/SBorder.h"

// Temp code for the nodes development
#include "Engine/Blueprint.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "K2Node_CustomEvent.h"
#include "DataPrepRecipe.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "PropertyEditorModule.h"
#include "IDetailsView.h"
#include "DataPrepAsset.h"
#include "IStructureDetailsView.h"

const FName FDataprepEditor::PipelineGraphTabId(TEXT("DataprepEditor_Pipeline_Graph"));
// end of temp code for nodes development

#define LOCTEXT_NAMESPACE "DataprepEditor"

extern const FName DataprepEditorAppIdentifier;

const FName FDataprepEditor::ScenePreviewTabId(TEXT("DataprepEditor_ScenePreview"));
const FName FDataprepEditor::AssetPreviewTabId(TEXT("DataprepEditor_AssetPreview"));
const FName FDataprepEditor::PaletteTabId(TEXT("DataprepEditor_Palette"));
const FName FDataprepEditor::DetailsTabId(TEXT("DataprepEditor_Details"));
const FName FDataprepEditor::DataprepAssetTabId(TEXT("DataprepEditor_Dataprep"));
const FName FDataprepEditor::SceneViewportTabId(TEXT("DataprepEditor_SceneViewport"));
const FName FDataprepEditor::DataprepStatisticsTabId(TEXT("DataprepEditor_Statistics"));

static bool bLogTiming = true;

class FTimeLogger
{
public:
	FTimeLogger(const FString& InText)
		: StartTime( FPlatformTime::Cycles64() )
		, Text( InText )
	{
		if( bLogTiming )
		{
			UE_LOG( LogDataprepEditor, Log, TEXT("%s ..."), *Text );
		}
	}

	~FTimeLogger()
	{
		if( bLogTiming )
		{
			// Log time spent to import incoming file in minutes and seconds
			double ElapsedSeconds = FPlatformTime::ToSeconds64(FPlatformTime::Cycles64() - StartTime);

			int ElapsedMin = int(ElapsedSeconds / 60.0);
			ElapsedSeconds -= 60.0 * (double)ElapsedMin;
			UE_LOG( LogDataprepEditor, Log, TEXT("%s took [%d min %.3f s]"), *Text, ElapsedMin, ElapsedSeconds );
		}
	}

private:
	uint64 StartTime;
	FString Text;
};

namespace DataprepEditorUtil
{
	void DeleteActor(AActor* Actor, UWorld* World)
	{
		if (Actor == nullptr || World != Actor->GetWorld())
		{
			return;
		}

		TArray<AActor*> Children;
		Actor->GetAttachedActors(Children);

		for (AActor* ChildActor : Children)
		{
			DeleteActor(ChildActor, World);
		}

		World->DestroyActor(Actor, false, true);
	}

	void DeleteTemporaryPackage( const FString& PathToDelete );
}

FDataprepEditor::FDataprepEditor()
	: bWorldBuilt(false)
	, bIsFirstRun(false)
	, bPipelineChanged(false)
	, bIsDataprepInstance(false)
	, bIsActionMenuContextSensitive(true)
	, bSaveIntermediateBuildProducts(false)
	, PreviewWorld(nullptr)
	, bIgnoreCloseRequest(false)
{
	FName UniqueWorldName = MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), FName( *(LOCTEXT("PreviewWorld", "Preview").ToString()) ));
	PreviewWorld = TStrongObjectPtr<UWorld>(NewObject< UWorld >(GetTransientPackage(), UniqueWorldName));
	PreviewWorld->WorldType = EWorldType::EditorPreview;

	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(PreviewWorld->WorldType);
	WorldContext.SetCurrentWorld(PreviewWorld.Get());

	PreviewWorld->InitializeNewWorld(UWorld::InitializationValues()
		.AllowAudioPlayback(false)
		.CreatePhysicsScene(false)
		.RequiresHitProxies(false)
		.CreateNavigation(false)
		.CreateAISystem(false)
		.ShouldSimulatePhysics(false)
		.SetTransactional(false));

	for ( ULevel* Level : PreviewWorld->GetLevels() )
	{
		for ( AActor* Actor : Level->Actors )
		{
			DefaultActorsInPreviewWorld.Add( Actor );
		}
	}
}

FDataprepEditor::~FDataprepEditor()
{
	if( DataprepAssetInterfacePtr.IsValid() )
	{
		DataprepAssetInterfacePtr->GetOnChanged().RemoveAll( this );
	}

	if ( PreviewWorld )
	{
		GEngine->DestroyWorldContext( PreviewWorld.Get() );
		PreviewWorld->DestroyWorld( true );
		PreviewWorld.Reset();
	}

	FCoreUObjectDelegates::OnObjectPropertyChanged.RemoveAll(this);

	auto DeleteDirectory = [&](const FString& DirectoryToDelete)
	{
		const FString AbsolutePath = FPaths::ConvertRelativePathToFull( DirectoryToDelete );
		IFileManager::Get().DeleteDirectory( *AbsolutePath, false, true );
	};

	// Clean up temporary directories and data created for this session
	{

		DeleteDirectory( TempDir );

		FString PackagePathToDeleteOnDisk;
		if (FPackageName::TryConvertLongPackageNameToFilename( GetTransientContentFolder(), PackagePathToDeleteOnDisk))
		{
			DeleteDirectory( PackagePathToDeleteOnDisk );
		}

	}

	// Clean up temporary directories associated to process if no session of Dataprep editor is running
	{
		auto IsDirectoryEmpty = [&](const TCHAR* Directory) -> bool
		{
			bool bDirectoryIsEmpty = true;
			IFileManager::Get().IterateDirectory(Directory, [&](const TCHAR* FilenameOrDirectory, bool bIsDirectory) -> bool
			{
				bDirectoryIsEmpty = false;
				return false;
			});

			return bDirectoryIsEmpty;
		};

		FString RootTempDir = FPaths::Combine( GetRootTemporaryDir(), FString::FromInt( FPlatformProcess::GetCurrentProcessId() ) );
		if(IsDirectoryEmpty( *RootTempDir ))
		{
			DeleteDirectory( RootTempDir );
		}

		const FString PackagePathToDelete = FPaths::Combine( GetRootPackagePath(), FString::FromInt( FPlatformProcess::GetCurrentProcessId() ) );
		FString PackagePathToDeleteOnDisk;
		if (FPackageName::TryConvertLongPackageNameToFilename(PackagePathToDelete, PackagePathToDeleteOnDisk))
		{
			if(IsDirectoryEmpty( *PackagePathToDeleteOnDisk ))
			{
				DeleteDirectory( PackagePathToDeleteOnDisk );
			}
		}
	}
}

FName FDataprepEditor::GetToolkitFName() const
{
	return FName("DataprepEditor");
}

FText FDataprepEditor::GetBaseToolkitName() const
{
	return LOCTEXT("AppLabel", "Dataprep Editor");
}

FString FDataprepEditor::GetWorldCentricTabPrefix() const
{
	return LOCTEXT("WorldCentricTabPrefix", "Dataprep").ToString();
}

FLinearColor FDataprepEditor::GetWorldCentricTabColorScale() const
{
	return FLinearColor(0.3f, 0.2f, 0.5f, 0.5f);
}

void FDataprepEditor::RegisterTabSpawners(const TSharedRef<class FTabManager>& InTabManager)
{
	WorkspaceMenuCategory = InTabManager->AddLocalWorkspaceMenuCategory(LOCTEXT("WorkspaceMenu_DataprepEditor", "Data Preparation Editor"));

	FAssetEditorToolkit::RegisterTabSpawners(InTabManager);

	auto WorkspaceMenuCategoryRef = WorkspaceMenuCategory.ToSharedRef();

	InTabManager->RegisterTabSpawner(ScenePreviewTabId, FOnSpawnTab::CreateSP(this, &FDataprepEditor::SpawnTabScenePreview))
		.SetDisplayName(LOCTEXT("ScenePreviewTab", "Scene Preview"))
		.SetGroup(WorkspaceMenuCategoryRef)
		.SetIcon(FSlateIcon(FDataprepEditorStyle::GetStyleSetName(), "DataprepEditor.Tabs.ScenePreview"));

	InTabManager->RegisterTabSpawner(AssetPreviewTabId, FOnSpawnTab::CreateSP(this, &FDataprepEditor::SpawnTabAssetPreview))
		.SetDisplayName(LOCTEXT("AssetPreviewTab", "Asset Preview"))
		.SetGroup(WorkspaceMenuCategoryRef)
		.SetIcon(FSlateIcon(FDataprepEditorStyle::GetStyleSetName(), "DataprepEditor.Tabs.AssetPreview"));

	InTabManager->RegisterTabSpawner(SceneViewportTabId, FOnSpawnTab::CreateSP(this, &FDataprepEditor::SpawnTabSceneViewport))
		.SetDisplayName(LOCTEXT("SceneViewportTab", "Scene Viewport"))
		.SetGroup(WorkspaceMenuCategoryRef)
		.SetIcon(FSlateIcon(FDataprepEditorStyle::GetStyleSetName(), "DataprepEditor.Tabs.SceneViewport"));

	InTabManager->RegisterTabSpawner(DetailsTabId, FOnSpawnTab::CreateSP(this, &FDataprepEditor::SpawnTabDetails))
		.SetDisplayName(LOCTEXT("DetailsTab", "Details"))
		.SetGroup(WorkspaceMenuCategoryRef)
		.SetIcon(FSlateIcon(FEditorStyle::GetStyleSetName(), "LevelEditor.Tabs.Details"));

	InTabManager->RegisterTabSpawner(DataprepAssetTabId, FOnSpawnTab::CreateSP(this, &FDataprepEditor::SpawnTabDataprep))
		.SetDisplayName(LOCTEXT("DataprepAssetTab", "Dataprep"))
		.SetGroup(WorkspaceMenuCategoryRef)
		.SetIcon(FSlateIcon(FEditorStyle::GetStyleSetName(), "LevelEditor.Tabs.Details"));

	InTabManager->RegisterTabSpawner(DataprepStatisticsTabId, FOnSpawnTab::CreateSP(this, &FDataprepEditor::SpawnTabStatistics))
		.SetDisplayName(LOCTEXT("StatisticsTab", "Statistics"))
		.SetGroup(WorkspaceMenuCategoryRef)
		.SetIcon(FSlateIcon(FEditorStyle::GetStyleSetName(), "LevelEditor.Tabs.StatsViewer"));

	// Do not register tabs which are not pertinent to Dataprep instance
	if(!bIsDataprepInstance)
	{
		InTabManager->RegisterTabSpawner(PaletteTabId, FOnSpawnTab::CreateSP(this, &FDataprepEditor::SpawnTabPalette))
			.SetDisplayName(LOCTEXT("PaletteTab", "Palette"))
			.SetGroup(WorkspaceMenuCategoryRef)
			.SetIcon( FSlateIcon(FEditorStyle::GetStyleSetName(), "Kismet.Tabs.Palette"));

		// Temp code for the nodes development
		InTabManager->RegisterTabSpawner(PipelineGraphTabId, FOnSpawnTab::CreateSP(this, &FDataprepEditor::SpawnTabPipelineGraph))
			.SetDisplayName(LOCTEXT("PipelineGraphTab", "Pipeline Graph"))
			.SetGroup(WorkspaceMenuCategoryRef)
			.SetIcon(FSlateIcon(FEditorStyle::GetStyleSetName(), "GraphEditor.EventGraph_16x"));
		// end of temp code for nodes development
	}
}

void FDataprepEditor::UnregisterTabSpawners(const TSharedRef<class FTabManager>& InTabManager)
{
	FAssetEditorToolkit::UnregisterTabSpawners(InTabManager);
	InTabManager->UnregisterAllTabSpawners();
}

void FDataprepEditor::CleanUpTemporaryDirectories()
{
	const int32 CurrentProcessID = FPlatformProcess::GetCurrentProcessId();

	TSet<FString> TempDirectories;
	IFileManager::Get().IterateDirectory( *GetRootTemporaryDir(), [&](const TCHAR* FilenameOrDirectory, bool bIsDirectory) -> bool
	{
		if (bIsDirectory)
		{
			FString DirectoryName = FPaths::GetBaseFilename( FilenameOrDirectory );
			if(FCString::IsNumeric( *DirectoryName ))
			{

				int32 ProcessID = FCString::Atoi( *DirectoryName );
				if(ProcessID != CurrentProcessID)
				{
					FProcHandle ProcHandle = FPlatformProcess::OpenProcess( ProcessID );

					// Delete directories if process is not valid
					bool bDeleteDirectories = !ProcHandle.IsValid();

					// Process is valid, check if application associated with process id is UE4 editor
					if(!bDeleteDirectories)
					{
						const FString ApplicationName = FPlatformProcess::GetApplicationName( ProcessID );
						bDeleteDirectories = !ApplicationName.StartsWith(TEXT("UE4Editor"));
					}

					if(bDeleteDirectories)
					{
						FString PackagePathToDelete = FPaths::Combine( GetRootPackagePath(), DirectoryName );
						FString PackagePathToDeleteOnDisk;
						if (FPackageName::TryConvertLongPackageNameToFilename(PackagePathToDelete, PackagePathToDeleteOnDisk))
						{
							TempDirectories.Add( MoveTemp(PackagePathToDeleteOnDisk) );
						}

						TempDirectories.Emplace( FilenameOrDirectory );
					}
				}
			}
		}
		return true;
	});

	for(FString& TempDirectory : TempDirectories)
	{
		FString AbsolutePath = FPaths::ConvertRelativePathToFull( TempDirectory );
		IFileManager::Get().DeleteDirectory( *AbsolutePath, false, true );
	}
}

const FString& FDataprepEditor::GetRootTemporaryDir()
{
	static FString RootTemporaryDir = FPaths::Combine(FPaths::ProjectIntermediateDir(), TEXT("DataprepTemp") );
	return RootTemporaryDir;
}

const FString& FDataprepEditor::GetRootPackagePath()
{
	static FString RootPackagePath( TEXT("/Engine/DataprepEditor/Transient") );
	return RootPackagePath;
}

void FDataprepEditor::InitDataprepEditor(const EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& InitToolkitHost, UDataprepAssetInterface* InDataprepAssetInterface, UObject* Blueprint)
{
	DataprepAssetInterfacePtr = TWeakObjectPtr<UDataprepAssetInterface>(InDataprepAssetInterface);
	check( DataprepAssetInterfacePtr.IsValid() );

	bIsDataprepInstance = Cast<UDataprepAssetInstance>(InDataprepAssetInterface) != nullptr;

	DataprepAssetInterfacePtr->GetOnChanged().AddSP( this, &FDataprepEditor::OnDataprepAssetChanged );

	// Assign unique session identifier
	SessionID = FGuid::NewGuid().ToString();

	// Initialize Actions' context
	DataprepActionAsset::FCanExecuteNextStepFunc CanExecuteNextStepFunc = [this](UDataprepActionAsset* ActionAsset, UDataprepOperation* Operation, UDataprepFilter* Filter) -> bool
	{
		return this->OnCanExecuteNextStep(ActionAsset, Operation, Filter);
	};

	DataprepActionAsset::FActionsContextChangedFunc ActionsContextChangedFunc = [this](const UDataprepActionAsset* ActionAsset, bool bWorldChanged, bool bAssetsChanged, const TArray< TWeakObjectPtr<UObject> >& NewAssets)
	{
		this->OnActionsContextChanged(ActionAsset, bWorldChanged, bAssetsChanged, NewAssets);
	};

	ActionsContext = MakeShareable( new FDataprepActionContext() );

	ActionsContext->SetTransientContentFolder( GetTransientContentFolder() / DataprepAssetInterfacePtr->GetName() / TEXT("Pipeline") )
		.SetLogger( TSharedPtr<IDataprepLogger>( new FDataprepCoreUtils::FDataprepLogger ) )
		.SetCanExecuteNextStep( CanExecuteNextStepFunc )
		.SetActionsContextChanged( ActionsContextChangedFunc );

	// Create temporary directory to store transient data
	CleanUpTemporaryDirectories();
	TempDir = FPaths::Combine( GetRootTemporaryDir(), FString::FromInt( FPlatformProcess::GetCurrentProcessId() ), SessionID);
	IFileManager::Get().MakeDirectory(*TempDir);

	// Temp code for the nodes development
	if(Blueprint != nullptr)
	{
		DataprepRecipeBPPtr = Cast<UBlueprint>(Blueprint);
		check( DataprepRecipeBPPtr.IsValid() );

		// Necessary step to regenerate blueprint generated class
		// Note that this compilation will always succeed as Dataprep node does not have real body
		{
			FKismetEditorUtilities::CompileBlueprint( DataprepRecipeBPPtr.Get(), EBlueprintCompileOptions::None, nullptr );
		}
	}
	// End temp code for the nodes development

	GEditor->RegisterForUndo(this);

	// Register our commands. This will only register them if not previously registered
	FDataprepEditorCommands::Register();

	BindCommands();

	CreateTabs();

	
	const TSharedRef<FTabManager::FLayout> Layout = DataprepRecipeBPPtr.IsValid() ? CreateDataprepLayout() : CreateDataprepInstanceLayout();

	const bool bCreateDefaultStandaloneMenu = true;
	const bool bCreateDefaultToolbar = true;
	FAssetEditorToolkit::InitAssetEditor( Mode, InitToolkitHost, DataprepEditorAppIdentifier, Layout, bCreateDefaultToolbar, bCreateDefaultStandaloneMenu, InDataprepAssetInterface );

	ExtendMenu();
	ExtendToolBar();
	RegenerateMenusAndToolbars();

#ifdef DATAPREP_EDITOR_VERBOSE
	LogDataprepEditor.SetVerbosity( ELogVerbosity::Verbose );
#endif
}

void FDataprepEditor::BindCommands()
{
	const FDataprepEditorCommands& Commands = FDataprepEditorCommands::Get();

	const TSharedRef<FUICommandList>& UICommandList = GetToolkitCommands();

	//UICommandList->MapAction(FGenericCommands::Get().Delete,
	//	FExecuteAction::CreateSP(this, &FDataprepEditor::DeleteSelected),
	//	FCanExecuteAction::CreateSP(this, &FDataprepEditor::CanDeleteSelected));

	//UICommandList->MapAction(FGenericCommands::Get().Undo,
	//	FExecuteAction::CreateSP(this, &FDataprepEditor::UndoAction));

	//UICommandList->MapAction(FGenericCommands::Get().Redo,
	//	FExecuteAction::CreateSP(this, &FDataprepEditor::RedoAction));

	// Temp code for the nodes development
	UICommandList->MapAction(
		Commands.CompileGraph,
		FExecuteAction::CreateSP(this, &FDataprepEditor::OnCompile));
	// end of temp code for nodes development

	UICommandList->MapAction(
		Commands.SaveScene,
		FExecuteAction::CreateSP(this, &FDataprepEditor::OnSaveScene));

	UICommandList->MapAction(
		Commands.BuildWorld,
		FExecuteAction::CreateSP( this, &FDataprepEditor::OnBuildWorld ),
		FCanExecuteAction::CreateSP( this, &FDataprepEditor::CanBuildWorld ) );

	UICommandList->MapAction(
		Commands.ExecutePipeline,
		FExecuteAction::CreateSP(this, &FDataprepEditor::OnExecutePipeline),
		FCanExecuteAction::CreateSP(this, &FDataprepEditor::CanExecutePipeline));

	UICommandList->MapAction(
		Commands.CommitWorld,
		FExecuteAction::CreateSP(this, &FDataprepEditor::OnCommitWorld),
		FCanExecuteAction::CreateSP(this, &FDataprepEditor::CanCommitWorld));
}

void FDataprepEditor::OnSaveScene()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FDataprepEditor::OnSaveScene);

}

void FDataprepEditor::OnBuildWorld()
{
	UDataprepAssetInterface* DataprepAssetInterface = DataprepAssetInterfacePtr.Get();
	if (!ensureAlways(DataprepAssetInterface))
	{
		return;
	}

	if (!ensureAlways(PreviewWorld.IsValid()))
	{
		return;
	}

	TRACE_CPUPROFILER_EVENT_SCOPE(FDataprepEditor::OnBuildWorld);

	if (DataprepAssetInterface->GetProducers()->GetProducersCount() == 0)
	{
		ResetBuildWorld();
		return;
	}

	CleanPreviewWorld();

	UPackage* TransientPackage = NewObject< UPackage >( nullptr, *GetTransientContentFolder(), RF_Transient );
	TransientPackage->FullyLoad();

	TSharedPtr< FDataprepCoreUtils::FDataprepFeedbackContext > FeedbackContext( new FDataprepCoreUtils::FDataprepFeedbackContext );

	TSharedPtr< IDataprepProgressReporter > ProgressReporter( new FDataprepCoreUtils::FDataprepProgressUIReporter( FeedbackContext.ToSharedRef() ) );
	{
		FTimeLogger TimeLogger( TEXT("Import") );

		FDataprepProducerContext Context;
		Context.SetWorld( PreviewWorld.Get() )
			.SetRootPackage( TransientPackage )
			.SetLogger( TSharedPtr< IDataprepLogger >( new FDataprepCoreUtils::FDataprepLogger ) )
			.SetProgressReporter( ProgressReporter );

		Assets = DataprepAssetInterface->GetProducers()->Produce( Context );
	}

	if ( ProgressReporter->IsWorkCancelled() )
	{
		// Flush the work that's already been done
		ResetBuildWorld();
		return;
	}

	CachedAssets.Reset();
	CachedAssets.Append( Assets );

	TakeSnapshot();

	UpdatePreviewPanels();
	SceneViewportView->FocusViewportOnScene();

	bWorldBuilt = true;
	bIsFirstRun = true;
}

void FDataprepEditor::OnDataprepAssetChanged( FDataprepAssetChangeType ChangeType )
{
	switch(ChangeType)
	{
		case FDataprepAssetChangeType::RecipeModified:
		{
			bPipelineChanged = true;
		}
		break;

		case FDataprepAssetChangeType::ProducerAdded:
		case FDataprepAssetChangeType::ProducerRemoved:
		case FDataprepAssetChangeType::ProducerModified:
		{
			// Just reset world for the time being
			ResetBuildWorld();
		}
		break;

		default:
			break;

	}
}

void FDataprepEditor::ResetBuildWorld()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FDataprepEditor::ResetBuildWorld);

	bWorldBuilt = false;
	CleanPreviewWorld();
	UpdatePreviewPanels();
	DataprepEditorUtil::DeleteTemporaryPackage( GetTransientContentFolder() );
}

void FDataprepEditor::CleanPreviewWorld()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FDataprepEditor::CleanPreviewWorld);

	FTimeLogger TimeLogger( TEXT("CleanPreviewWorld") );

	// Destroy all actors in preview world
	for (ULevel* Level : PreviewWorld->GetLevels())
	{
		TArray<AActor*> LevelActors( Level->Actors );

		for( AActor* Actor : LevelActors )
		{
			if (Actor && !Actor->IsPendingKill() && !DefaultActorsInPreviewWorld.Contains(Actor))
			{
				PreviewWorld->EditorDestroyActor(Actor, true);

				// Since deletion can be delayed, rename to avoid future name collision
				// Call UObject::Rename directly on actor to avoid AActor::Rename which unnecessarily sunregister and re-register components
				Actor->UObject::Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_ForceNoResetLoaders);
			}
		}
	}

	SceneViewportView->ClearScene();

	// Delete assets which are still in the transient content folder
	const FString TransientContentFolder( GetTransientContentFolder() );
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

	CachedAssets.Reset();
	Assets.Reset();

	PreviewWorld->CleanupActors();
}

void FDataprepEditor::OnExecutePipeline()
{
	if( DataprepAssetInterfacePtr->GetConsumer() == nullptr )
	{
		return;
	}

	TRACE_CPUPROFILER_EVENT_SCOPE(FDataprepEditor::OnExecutePipeline);

	if(!bIsFirstRun)
	{
		RestoreFromSnapshot();
	}

	// Remove any link between assets referenced in the preview world and the viewport
	SceneViewportView->ClearScene();

	// Trigger execution of data preparation operations on world attached to recipe
	{
		FTimeLogger TimeLogger( TEXT("ExecutePipeline") );

		// Some operation can indirectly call FAssetEditorManager::CloseAllAssetEditors (eg. Remove Asset)
		// Editors can individually refuse this request: we ignore it during the pipeline traversal.
		TGuardValue<bool> IgnoreCloseRequestGuard(bIgnoreCloseRequest, true);

		TSharedPtr< FDataprepCoreUtils::FDataprepFeedbackContext > FeedbackContext(new FDataprepCoreUtils::FDataprepFeedbackContext);
		ActionsContext->SetProgressReporter( TSharedPtr< IDataprepProgressReporter >( new FDataprepCoreUtils::FDataprepProgressUIReporter( FeedbackContext.ToSharedRef() ) ) );
		ActionsContext->SetWorld( PreviewWorld.Get() ).SetAssets( Assets );

		DataprepAssetInterfacePtr->ExecuteRecipe( ActionsContext );

		// Update list of assets with latest ones
		Assets = ActionsContext->Assets.Array();
	}

	if ( ActionsContext->ProgressReporterPtr->IsWorkCancelled() )
	{
		RestoreFromSnapshot();
	}

	// Redraw 3D viewport
	SceneViewportView->UpdateScene();

	// Add assets which may have been created by actions
	for( TWeakObjectPtr<UObject>& Asset : Assets )
	{
		if( Asset.IsValid() )
		{
			CachedAssets.Add( Asset );
		}
	}

	// Indicate pipeline has been executed at least once
	bIsFirstRun = false;
	// Reset tracking of pipeline changes between execution
	bPipelineChanged = false;
}

void FDataprepEditor::OnCommitWorld()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FDataprepEditor::OnCommitWorld);

	FTimeLogger TimeLogger( TEXT("Commit") );

	// Pipeline has not been executed, validate with user this is intentional
	if( bIsFirstRun && DataprepAssetInterfacePtr->HasActions())
	{
		const FText Title( LOCTEXT( "DataprepEditor_ProceedWithCommit", "Proceed with commit" ) );
		const FText Message( LOCTEXT( "DataprepEditor_ConfirmCommitPipelineNotExecuted", "The action pipeline has not been executed.\nDo you want to proceeed with the commit anyway?" ) );

		if( OpenMsgDlgInt( EAppMsgType::YesNo, Message, Title ) == EAppReturnType::No )
		{
			return;
		}
	}
	// Pipeline has changed without being executed, validate with user this is intentional
	else if( !bIsFirstRun && bPipelineChanged )
	{
		const FText Title( LOCTEXT( "DataprepEditor_ProceedWithCommit", "Proceed with commit" ) );
		const FText Message( LOCTEXT( "DataprepEditor_ConfirmCommitPipelineChanged", "The action pipeline has changed since last execution.\nDo you want to proceeed with the commit anyway?" ) );

		if( OpenMsgDlgInt( EAppMsgType::YesNo, Message, Title ) == EAppReturnType::No )
		{
			return;
		}
	}

	// Remove references to assets in 3D viewport before commit
	SceneViewportView->ClearScene();

	// Finalize assets
	TArray<TWeakObjectPtr<UObject>> ValidAssets( MoveTemp(Assets) );

	FDataprepConsumerContext Context;
	Context.SetWorld( PreviewWorld.Get() )
		.SetAssets( ValidAssets )
		.SetTransientContentFolder( GetTransientContentFolder() )
		.SetLogger( TSharedPtr<IDataprepLogger>( new FDataprepCoreUtils::FDataprepLogger ) )
		.SetProgressReporter( TSharedPtr< IDataprepProgressReporter >( new FDataprepCoreUtils::FDataprepProgressUIReporter() ) );

	if( !DataprepAssetInterfacePtr->RunConsumer( Context ) )
	{
		UE_LOG( LogDataprepEditor, Error, TEXT("Consumer failed...") );
	}

	ResetBuildWorld();
}

void FDataprepEditor::ExtendMenu()
{
	IDataprepEditorModule* DataprepEditorModule = &FModuleManager::LoadModuleChecked<IDataprepEditorModule>("DataprepEditor");
	AddMenuExtender(DataprepEditorModule->GetMenuExtensibilityManager()->GetAllExtenders(GetToolkitCommands(), GetEditingObjects()));
}

void FDataprepEditor::ExtendToolBar()
{
	struct Local
	{
		static void FillToolbar(FToolBarBuilder& ToolbarBuilder, FDataprepEditor* ThisEditor)
		{
			ToolbarBuilder.BeginSection("Run");
			{
				ToolbarBuilder.AddToolBarButton(FDataprepEditorCommands::Get().BuildWorld);
				ToolbarBuilder.AddToolBarButton(FDataprepEditorCommands::Get().ExecutePipeline);
				ToolbarBuilder.AddToolBarButton(FDataprepEditorCommands::Get().CommitWorld);
			}
			ToolbarBuilder.EndSection();
		}
	};

	TSharedPtr<FExtender> ToolbarExtender = MakeShareable(new FExtender);

	FDataprepEditor* ThisEditor = this;

	ToolbarExtender->AddToolBarExtension(
		"Asset",
		EExtensionHook::After,
		ToolkitCommands,
		FToolBarExtensionDelegate::CreateStatic(&Local::FillToolbar, ThisEditor)
	);

	AddToolbarExtender(ToolbarExtender);

	IDataprepEditorModule* DataprepEditorModule = &FModuleManager::LoadModuleChecked<IDataprepEditorModule>("DataprepEditor");
	AddToolbarExtender(DataprepEditorModule->GetToolBarExtensibilityManager()->GetAllExtenders(GetToolkitCommands(), GetEditingObjects()));
}

void FDataprepEditor::CreateTabs()
{
	AssetPreviewView = SNew(AssetPreviewWidget::SAssetsPreviewWidget);
	AssetPreviewView->OnSelectionChanged().AddLambda(
		[this](TSet< UObject* > Selection)
		{
			SetDetailsObjects(MoveTemp(Selection), false);
		}
	);

	DataprepAssetView = SNew( SDataprepAssetView, DataprepAssetInterfacePtr.Get(), PipelineEditorCommands );

	CreateScenePreviewTab();

	// Create 3D viewport
	SceneViewportView = SNew( SDataprepEditorViewport, SharedThis(this) )
	.WorldToPreview( PreviewWorld.Get() );

	// Create Details Panel
	CreateDetailsViews();

	// Temp code for the nodes development
	if(DataprepRecipeBPPtr.IsValid())
	{
		// Create Pipeline Editor
		CreatePipelineEditor();
	}
	// end of temp code for nodes development

}

// Temp code for the nodes development
TSharedRef<SDockTab> FDataprepEditor::SpawnTabPipelineGraph(const FSpawnTabArgs & Args)
{
	check(Args.GetTabId() == PipelineGraphTabId);

	if(!bIsDataprepInstance)
	{
		return SNew(SDockTab)
			//.Icon(FDataprepEditorStyle::Get()->GetBrush("DataprepEditor.Tabs.Pipeline"))
			.Label(LOCTEXT("DataprepEditor_PipelineTab_Title", "Pipeline"))
			[
				DataprepRecipeBPPtr.IsValid() ? PipelineView.ToSharedRef() : SNullWidget::NullWidget
			];
	}

	return SNew(SDockTab);
}
// end of temp code for nodes development

TSharedRef<SDockTab> FDataprepEditor::SpawnTabScenePreview(const FSpawnTabArgs & Args)
{
	check(Args.GetTabId() == ScenePreviewTabId);

	return SNew(SDockTab)
		//.Icon(FDataprepEditorStyle::Get()->GetBrush("DataprepEditor.Tabs.ScenePreview"))
		.Label(LOCTEXT("DataprepEditor_ScenePreviewTab_Title", "Scene Preview"))
		[
			ScenePreviewView.ToSharedRef()
		];
}

TSharedRef<SDockTab> FDataprepEditor::SpawnTabAssetPreview(const FSpawnTabArgs & Args)
{
	check(Args.GetTabId() == AssetPreviewTabId);

	return SNew(SDockTab)
		//.Icon(FDataprepEditorStyle::Get()->GetBrush("DataprepEditor.Tabs.AssetPreview"))
		.Label(LOCTEXT("DataprepEditor_AssetPreviewTab_Title", "Asset Preview"))
		[
			SNew(SBorder)
			.Padding(2.f)
			.BorderImage(FEditorStyle::GetBrush("ToolPanel.GroupBorder"))
			[
				AssetPreviewView.ToSharedRef()
			]
		];
}

TSharedRef<SDockTab> FDataprepEditor::SpawnTabPalette(const FSpawnTabArgs & Args)
{
	check(Args.GetTabId() == PaletteTabId);

	if(!bIsDataprepInstance)
	{
		return SNew(SDockTab)
			.Icon(FSlateIcon(FEditorStyle::GetStyleSetName(), "Kismet.Tabs.Palette").GetIcon())
			.Label(LOCTEXT("PaletteTab", "Palette"))
			[
				SNew(SDataprepPalette)
			];
	}

	return SNew(SDockTab);
}

TSharedRef<SDockTab> FDataprepEditor::SpawnTabDataprep(const FSpawnTabArgs & Args)
{
	check(Args.GetTabId() == DataprepAssetTabId);

	return SNew(SDockTab)
	.Label(LOCTEXT("DataprepEditor_DataprepTab_Title", "Dataprep"))
	[
		SNew(SBorder)
		.Padding(2.f)
		.BorderImage(FEditorStyle::GetBrush("ToolPanel.GroupBorder"))
		[
			DataprepAssetView.ToSharedRef()
		]
	];
}

TSharedRef<SDockTab> FDataprepEditor::SpawnTabStatistics(const FSpawnTabArgs & Args)
{
	check(Args.GetTabId() == DataprepStatisticsTabId);

	FStatsViewerModule& StatsViewerModule = FModuleManager::Get().LoadModuleChecked<FStatsViewerModule>("StatsViewer");
	
	const uint32 EnablePagesMask = (1 << EStatsPage::PrimitiveStats) | (1 << EStatsPage::StaticMeshLightingInfo) | (1 << EStatsPage::TextureStats);

	return SNew(SDockTab)
		.Label(LOCTEXT("DataprepEditor_StatisticsTab_Title", "Statistics"))
		.Icon(FEditorStyle::GetBrush("LevelEditor.Tabs.StatsViewer"))
		[
			StatsViewerModule.CreateStatsViewer( *PreviewWorld.Get(), EnablePagesMask, TEXT("Dataprep") )
		];
}

TSharedRef<SDockTab> FDataprepEditor::SpawnTabSceneViewport( const FSpawnTabArgs& Args )
{
	check( Args.GetTabId() == SceneViewportTabId );

	TSharedRef<SDockTab> SpawnedTab =
		SNew(SDockTab)
		.Label( LOCTEXT("DataprepEditor_SceneViewportTab_Title", "Viewport") )
		[
			SceneViewportView.ToSharedRef()
		];

	return SpawnedTab;
}

TSharedRef<FTabManager::FLayout> FDataprepEditor::CreateDataprepLayout()
{
	return FTabManager::NewLayout("Standalone_DataprepEditor_Layout_v0.7")
		->AddArea
		(
			FTabManager::NewPrimaryArea()->SetOrientation(Orient_Vertical)
			->Split
			(
				FTabManager::NewStack()
				->SetSizeCoefficient(0.1f)
				->SetHideTabWell(true)
				->AddTab(GetToolbarTabId(), ETabState::OpenedTab)
			)
			->Split
			(
				FTabManager::NewSplitter()->SetOrientation(Orient_Horizontal)
				->Split
				(
					FTabManager::NewSplitter()->SetOrientation(Orient_Vertical)
					->Split
					(
						FTabManager::NewSplitter()->SetOrientation(Orient_Horizontal)
						->SetSizeCoefficient(0.75f)
						->Split
						(
							FTabManager::NewStack()
							->SetSizeCoefficient(0.2f)
							->AddTab(AssetPreviewTabId, ETabState::OpenedTab)
							->SetHideTabWell( true )
						)
						->Split
						(
							FTabManager::NewStack()
							->SetSizeCoefficient(0.55f)
							->AddTab(SceneViewportTabId, ETabState::OpenedTab)
							->SetHideTabWell(true)
						)
						->Split
						(
							FTabManager::NewStack()
							->SetSizeCoefficient(0.25f)
							->AddTab(ScenePreviewTabId, ETabState::OpenedTab)
							->SetHideTabWell( true )
						)
					)
					->Split
					(
						FTabManager::NewSplitter()->SetOrientation(Orient_Horizontal)
						->Split
						(
							FTabManager::NewStack()
							->SetSizeCoefficient(0.15f)
							->AddTab(PaletteTabId, ETabState::OpenedTab)
							->SetHideTabWell( true )
						)
						// Temp code for the nodes development
						->Split
						(
							FTabManager::NewStack()
							->SetSizeCoefficient(0.85f)
							->AddTab(PipelineGraphTabId, ETabState::OpenedTab)
							->SetHideTabWell( true )
						)
						// end of temp code for nodes development
					)
				)
				->Split
				(
					FTabManager::NewSplitter()->SetOrientation(Orient_Vertical)
					->SetSizeCoefficient(0.25f)
					->Split
					(
						FTabManager::NewStack()
						->SetSizeCoefficient(0.3f)
						->AddTab(DataprepAssetTabId, ETabState::OpenedTab)
						->SetHideTabWell(true)
					)
					->Split
					(
						FTabManager::NewStack()
						->SetSizeCoefficient(0.7f)
						->AddTab(DetailsTabId, ETabState::OpenedTab)
						->SetHideTabWell(true)
					)
				)
			)
		);
}

TSharedRef<FTabManager::FLayout> FDataprepEditor::CreateDataprepInstanceLayout()
{
	return FTabManager::NewLayout("Standalone_DataprepEditor_InstanceLayout_v0.2")
		->AddArea
		(
			FTabManager::NewPrimaryArea()->SetOrientation(Orient_Vertical)
			->Split
			(
				FTabManager::NewStack()
				->SetSizeCoefficient(0.1f)
				->SetHideTabWell(true)
				->AddTab(GetToolbarTabId(), ETabState::OpenedTab)
				// Don't want the secondary toolbar tab to be opened if there's nothing in it
				//->AddTab(SecondaryToolbarTabId, ETabState::ClosedTab)
			)
			->Split
			(
				FTabManager::NewSplitter()->SetOrientation(Orient_Horizontal)
				->Split
				(
					FTabManager::NewSplitter()->SetOrientation(Orient_Vertical)
					->SetSizeCoefficient(0.2f)
					->Split
					(
						FTabManager::NewStack()
						->SetSizeCoefficient(0.5f)
						->AddTab(ScenePreviewTabId, ETabState::OpenedTab)
						->SetHideTabWell( true )
					)
					->Split
					(
						FTabManager::NewStack()
						->SetSizeCoefficient(0.5f)
						->AddTab(AssetPreviewTabId, ETabState::OpenedTab)
						->SetHideTabWell( true )
					)
				)
				->Split
				(
					FTabManager::NewSplitter()->SetOrientation(Orient_Vertical)
					->SetSizeCoefficient(0.6f)
					->Split
					(
						FTabManager::NewStack()
						->SetSizeCoefficient(1.f)
						->AddTab(SceneViewportTabId, ETabState::OpenedTab)
						->SetHideTabWell(true)
					)
				)
				->Split
				(
					FTabManager::NewSplitter()->SetOrientation(Orient_Vertical)
					->SetSizeCoefficient(0.2f)
					->Split
					(
						FTabManager::NewStack()
						->SetSizeCoefficient(0.5f)
						->AddTab(DataprepAssetTabId, ETabState::OpenedTab)
						->SetHideTabWell(true)
					)
					->Split
					(
						FTabManager::NewStack()
						->SetSizeCoefficient(0.5f)
						->AddTab(DetailsTabId, ETabState::OpenedTab)
						->SetHideTabWell(true)
					)
				)
			)
		);
}

void FDataprepEditor::UpdatePreviewPanels(bool bInclude3DViewport)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FDataprepEditor::UpdatePreviewPanels);

	{
		FTimeLogger TimeLogger( TEXT("UpdatePreviewPanels") );

		AssetPreviewView->ClearAssetList();
		FString SubstitutePath = DataprepAssetInterfacePtr->GetOutermost()->GetName();
		if(DataprepAssetInterfacePtr->GetConsumer() != nullptr && !DataprepAssetInterfacePtr->GetConsumer()->GetTargetContentFolder().IsEmpty())
		{
			SubstitutePath = DataprepAssetInterfacePtr->GetConsumer()->GetTargetContentFolder();
		}
		AssetPreviewView->SetAssetsList( Assets, GetTransientContentFolder(), SubstitutePath );
	}

	if(bInclude3DViewport)
	{
		SceneViewportView->UpdateScene();
	}
}

bool FDataprepEditor::OnRequestClose()
{
	const int ActorCount = PreviewWorld->GetActorCount();
	if( bWorldBuilt && !bIgnoreCloseRequest && ActorCount > DefaultActorsInPreviewWorld.Num() )
	{
		// World was imported and is not empty -- show warning message
		const FText Title( LOCTEXT( "DataprepEditor_ProceedWithClose", "Proceed with close") );
		const FText Message( LOCTEXT( "DataprepEditor_ConfirmClose", "Imported data was not committed! Closing the editor will discard imported data.\nDo you want to close anyway?" ) );

		return ( OpenMsgDlgInt( EAppMsgType::YesNo, Message, Title ) == EAppReturnType::Yes );
	}
	return !bIgnoreCloseRequest;
}

bool FDataprepEditor::CanBuildWorld()
{
	return DataprepAssetInterfacePtr->GetProducers()->GetProducersCount() > 0;
}

bool FDataprepEditor::CanExecutePipeline()
{
	return bWorldBuilt;
}

bool FDataprepEditor::CanCommitWorld()
{
	// Execution of pipeline is not required. User can directly commit result of import
	return bWorldBuilt && DataprepAssetInterfacePtr->GetConsumer() != nullptr;
}

FString FDataprepEditor::GetTransientContentFolder()
{
	return FPaths::Combine( GetRootPackagePath(), FString::FromInt( FPlatformProcess::GetCurrentProcessId() ), SessionID );
}

bool FDataprepEditor::OnCanExecuteNextStep(UDataprepActionAsset* ActionAsset, UDataprepOperation* Operation, UDataprepFilter* Filter)
{
	// #ueent_todo: Make this action configurable by user
	UpdatePreviewPanels(false);
	return true;
}

void FDataprepEditor::OnActionsContextChanged( const UDataprepActionAsset* ActionAsset, bool bWorldChanged, bool bAssetsChanged, const TArray< TWeakObjectPtr<UObject> >& NewAssets )
{
	if(bAssetsChanged)
	{
		Assets = NewAssets;
	}
}

void DataprepEditorUtil::DeleteTemporaryPackage( const FString& PathToDelete )
{
	// See ContentBrowserUtils::LoadAssetsIfNeeded
	// See ContentBrowserUtils::DeleteFolders

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::Get().LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));

	// Form a filter from the path to delete
	FARFilter Filter;
	Filter.bRecursivePaths = true;
	new (Filter.PackagePaths) FName( *PathToDelete );

	// Query for a list of assets in the path to delete
	TArray<FAssetData> AssetDataList;
	AssetRegistryModule.Get().GetAssets(Filter, AssetDataList);

	// Delete all registered objects which are in  memory and under this path
	{
		TArray<UObject*> AssetsToDelete;
		AssetsToDelete.Reserve(AssetDataList.Num());
		for(const FAssetData& AssetData : AssetDataList)
		{
			FSoftObjectPath ObjectPath( AssetData.ObjectPath.ToString() );

			if(UObject* Object = ObjectPath.ResolveObject())
			{
				AssetsToDelete.Add(Object);
			}
		}

		if(AssetsToDelete.Num() > 0)
		{
			ObjectTools::DeleteObjects(AssetsToDelete, false);
		}
	}

	// Delete all assets not in memory but on disk
	{
		struct FEmptyFolderVisitor : public IPlatformFile::FDirectoryVisitor
		{
			bool bIsEmpty;

			FEmptyFolderVisitor()
				: bIsEmpty(true)
			{
			}

			virtual bool Visit(const TCHAR* FilenameOrDirectory, bool bIsDirectory) override
			{
				if (!bIsDirectory)
				{
					bIsEmpty = false;
					return false; // abort searching
				}

				return true; // continue searching
			}
		};

		FString PathToDeleteOnDisk;
		if (FPackageName::TryConvertLongPackageNameToFilename(PathToDelete, PathToDeleteOnDisk))
		{
			if(IFileManager::Get().DirectoryExists( *PathToDeleteOnDisk ))
			{
				// Look for files on disk in case the folder contains things not tracked by the asset registry
				FEmptyFolderVisitor EmptyFolderVisitor;
				IFileManager::Get().IterateDirectoryRecursively(*PathToDeleteOnDisk, EmptyFolderVisitor);

				if (EmptyFolderVisitor.bIsEmpty && IFileManager::Get().DeleteDirectory(*PathToDeleteOnDisk, false, true))
				{
					AssetRegistryModule.Get().RemovePath(PathToDelete);
				}
			}
			// Request deletion anyway
			else
			{
				AssetRegistryModule.Get().RemovePath(PathToDelete);
			}
		}
	}

	// Check that no asset remains
	AssetDataList.Reset();
	AssetRegistryModule.Get().GetAssets(Filter, AssetDataList);
	//ensure(AssetDataList.Num() == 0);
}

#undef LOCTEXT_NAMESPACE
