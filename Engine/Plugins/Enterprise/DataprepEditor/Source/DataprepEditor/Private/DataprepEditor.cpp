// Copyright Epic Games, Inc. All Rights Reserved.

#include "DataprepEditor.h"

#include "DataprepOperation.h"

#include "DataprepActionAsset.h"
#include "DataprepAssetInstance.h"
#include "DataprepContentConsumer.h"
#include "DataprepContentProducer.h"
#include "DataprepCoreUtils.h"
#include "DataprepEditorActions.h"
#include "DataprepEditorLogCategory.h"
#include "DataprepEditorModule.h"
#include "DataprepEditorStyle.h"
#include "PreviewSystem/DataprepPreviewAssetColumn.h"
#include "PreviewSystem/DataprepPreviewSceneOutlinerColumn.h"
#include "PreviewSystem/DataprepPreviewSystem.h"
#include "SchemaActions/DataprepOperationMenuActionCollector.h"
#include "Widgets/DataprepAssetView.h"
#include "Widgets/DataprepGraph/SDataprepGraphEditor.h"
#include "Widgets/SAssetsPreviewWidget.h"
#include "Widgets/SDataprepEditorViewport.h"
#include "Widgets/SDataprepPalette.h"

#include "ActorEditorUtils.h"
#include "ActorTreeItem.h"
#include "AssetDeleteModel.h"
#include "AssetRegistryModule.h"
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
#include "Misc/MessageDialog.h"
#include "Misc/Paths.h"
#include "Misc/ScopedSlowTask.h"
#include "Modules/ModuleManager.h"
#include "ObjectTools.h"
#include "SceneOutlinerModule.h"
#include "SceneOutlinerPublicTypes.h"
#include "ScopedTransaction.h"
#include "StatsViewerModule.h"
#include "Templates/UnrealTemplate.h"
#include "Toolkits/IToolkit.h"
#include "UObject/Object.h"
#include "UnrealEdGlobals.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/STextComboBox.h"
#include "Widgets/Layout/SBorder.h"

#define LOCTEXT_NAMESPACE "DataprepEditor"

extern const FName DataprepEditorAppIdentifier;

const FName FDataprepEditor::ScenePreviewTabId(TEXT("DataprepEditor_ScenePreview"));
const FName FDataprepEditor::AssetPreviewTabId(TEXT("DataprepEditor_AssetPreview"));
const FName FDataprepEditor::PaletteTabId(TEXT("DataprepEditor_Palette"));
const FName FDataprepEditor::DetailsTabId(TEXT("DataprepEditor_Details"));
const FName FDataprepEditor::DataprepAssetTabId(TEXT("DataprepEditor_Dataprep"));
const FName FDataprepEditor::SceneViewportTabId(TEXT("DataprepEditor_SceneViewport"));
const FName FDataprepEditor::DataprepStatisticsTabId(TEXT("DataprepEditor_Statistics"));
const FName FDataprepEditor::DataprepGraphEditorTabId(TEXT("DataprepEditor_GraphEditor"));

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

FDataprepEditor::FDataprepEditor()
	: bWorldBuilt(false)
	, bIsFirstRun(false)
	, bPipelineChanged(false)
	, bIsDataprepInstance(false)
	, bIsActionMenuContextSensitive(true)
	, bSaveIntermediateBuildProducts(false)
	, PreviewWorld(nullptr)
	, bIgnoreCloseRequest(false)
	, PreviewSystem( MakeShared<FDataprepPreviewSystem>() )
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

		InTabManager->RegisterTabSpawner(DataprepGraphEditorTabId, FOnSpawnTab::CreateSP(this, &FDataprepEditor::SpawnTabGraphEditor))
			.SetDisplayName(LOCTEXT("GraphEditorTab", "Recipe Graph"))
			.SetGroup(WorkspaceMenuCategoryRef)
			.SetIcon(FSlateIcon(FEditorStyle::GetStyleSetName(), "GraphEditor.EventGraph_16x"));
		
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

void FDataprepEditor::InitDataprepEditor(const EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& InitToolkitHost, UDataprepAssetInterface* InDataprepAssetInterface)
{
	DataprepAssetInterfacePtr = TWeakObjectPtr<UDataprepAssetInterface>(InDataprepAssetInterface);
	check( DataprepAssetInterfacePtr.IsValid() );

	bIsDataprepInstance = InDataprepAssetInterface->IsA<UDataprepAssetInstance>();

	DataprepAssetInterfacePtr->GetOnChanged().AddSP( this, &FDataprepEditor::OnDataprepAssetChanged );

	if ( UDataprepAsset* DataprepAsset = Cast<UDataprepAsset>( InDataprepAssetInterface ) )
	{
		DataprepAsset->GetOnStepObjectsAboutToBeRemoved().AddSP( this, &FDataprepEditor::OnStepObjectsAboutToBeDeleted );
	}

	// Assign unique session identifier
	SessionID = FGuid::NewGuid().ToString();

	// Initialize Actions' context
	DataprepActionAsset::FCanExecuteNextStepFunc CanExecuteNextStepFunc = [this](UDataprepActionAsset* ActionAsset) -> bool
	{
		return this->OnCanExecuteNextStep(ActionAsset);
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

	GEditor->RegisterForUndo(this);

	// Register our commands. This will only register them if not previously registered
	FDataprepEditorCommands::Register();

	BindCommands();

	CreateTabs();
	
	const TSharedRef<FTabManager::FLayout> Layout = bIsDataprepInstance ? CreateDataprepInstanceLayout() : CreateDataprepLayout();

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

	UpdateDataForPreviewSystem();

	CachedAssets.Empty(Assets.Num());
	for(TWeakObjectPtr<UObject>& WeakObject : Assets)
	{
		if(UObject* Object = WeakObject.Get())
		{
			CachedAssets.Emplace( Object );
		}
	}

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
	FDataprepCoreUtils::DeleteTemporaryFolders( GetTransientContentFolder() );
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
	for( FSoftObjectPath& SoftObjectPath : CachedAssets )
	{
		if(SoftObjectPath.GetLongPackageName().StartsWith(TransientContentFolder))
		{
			FSoftObjectPath PackagePath(SoftObjectPath.GetLongPackageName());
			UPackage* Package = Cast<UPackage>(PackagePath.ResolveObject());

			UObject* ObjectToDelete = StaticFindObjectFast(nullptr, Package, *SoftObjectPath.GetAssetName());

			FDataprepCoreUtils::MoveToTransientPackage( ObjectToDelete );
			ObjectsToDelete.Add( ObjectToDelete );
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

		FFeedbackContext* OldGWarn = GWarn;
		GWarn = FeedbackContext.Get();

		ActionsContext->SetProgressReporter( TSharedPtr< IDataprepProgressReporter >( new FDataprepCoreUtils::FDataprepProgressUIReporter( FeedbackContext.ToSharedRef() ) ) );
		ActionsContext->SetWorld( PreviewWorld.Get() ).SetAssets( Assets );

		DataprepAssetInterfacePtr->ExecuteRecipe( ActionsContext );

		GWarn = OldGWarn;

		// Update list of assets with latest ones
		Assets = ActionsContext->Assets.Array();
	}

	if ( ActionsContext->ProgressReporterPtr->IsWorkCancelled() )
	{
		RestoreFromSnapshot();
	}

	UpdateDataForPreviewSystem();

	// Redraw 3D viewport
	SceneViewportView->UpdateScene();

	// Add assets which may have been created by actions
	for( TWeakObjectPtr<UObject>& Asset : Assets )
	{
		if( Asset.IsValid() )
		{
			CachedAssets.Emplace( Asset.Get() );
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

		if( FMessageDialog::Open( EAppMsgType::YesNo, Message, &Title ) != EAppReturnType::Yes )
		{
			return;
		}
	}
	// Pipeline has changed without being executed, validate with user this is intentional
	else if( !bIsFirstRun && bPipelineChanged )
	{
		const FText Title( LOCTEXT( "DataprepEditor_ProceedWithCommit", "Proceed with commit" ) );
		const FText Message( LOCTEXT( "DataprepEditor_ConfirmCommitPipelineChanged", "The action pipeline has changed since last execution.\nDo you want to proceeed with the commit anyway?" ) );

		if( FMessageDialog::Open( EAppMsgType::YesNo, Message, &Title ) != EAppReturnType::Yes )
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

		// Restore Dataprep's import data
		RestoreFromSnapshot();

		// Restore 3D viewport
		SceneViewportView->UpdateScene();

		// Indicates that the pipeline has not been run on the data
		bIsFirstRun = true;

		return;
	}

	ResetBuildWorld();

	UpdateDataForPreviewSystem();
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

	CreateGraphEditor();

	DataprepAssetView = SNew( SDataprepAssetView, DataprepAssetInterfacePtr.Get() );

	CreateScenePreviewTab();

	// Create 3D viewport
	SceneViewportView = SNew( SDataprepEditorViewport, SharedThis(this) )
	.WorldToPreview( PreviewWorld.Get() );

	// Create Details Panel
	CreateDetailsViews();
}

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

TSharedRef<SDockTab> FDataprepEditor::SpawnTabGraphEditor(const FSpawnTabArgs & Args)
{
	check(Args.GetTabId() == DataprepGraphEditorTabId);

	if(!bIsDataprepInstance)
	{
		return SNew(SDockTab)
			//.Icon(FDataprepEditorStyle::Get()->GetBrush("DataprepEditor.Tabs.Pipeline"))
			.Label(LOCTEXT("DataprepEditor_GraphEditorTab_Title", "Recipe Graph"))
			[
				GraphEditor.ToSharedRef()
			];
	}

	return SNew(SDockTab);
}

TSharedRef<FTabManager::FLayout> FDataprepEditor::CreateDataprepLayout()
{
	return FTabManager::NewLayout("Standalone_DataprepEditor_Layout_v0.9")
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
						->Split
						(
							FTabManager::NewStack()
							->SetSizeCoefficient(0.85f)
							->AddTab(DataprepGraphEditorTabId, ETabState::OpenedTab)
							->SetHideTabWell( true )
						)
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

		return ( FMessageDialog::Open(EAppMsgType::YesNo, Message, &Title) == EAppReturnType::Yes );
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

bool FDataprepEditor::OnCanExecuteNextStep(UDataprepActionAsset* ActionAsset)
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

void FDataprepEditor::RefreshColumnsForPreviewSystem()
{
	if ( PreviewSystem->HasObservedObjects() )
	{
		SceneOutliner->RemoveColumn( SceneOutliner::FBuiltInColumnTypes::ActorInfo() );
		SceneOutliner::FColumnInfo ColumnInfo( SceneOutliner::EColumnVisibility::Visible, 100, FCreateSceneOutlinerColumn::CreateLambda( [Preview = PreviewSystem](ISceneOutliner& InSceneOutliner) -> TSharedRef< ISceneOutlinerColumn >
			{
				return MakeShared<FDataprepPreviewOutlinerColumn>( InSceneOutliner, Preview );
			} ) );
		SceneOutliner->AddColumn( FDataprepPreviewOutlinerColumn::ColumnID, ColumnInfo );

		AssetPreviewView->AddColumn( MakeShared<FDataprepPreviewAssetColumn>(PreviewSystem) );
	}
	else
	{
		SceneOutliner->RemoveColumn( FDataprepPreviewOutlinerColumn::ColumnID );
		FSceneOutlinerModule& SceneOutlinerModule = FModuleManager::Get().LoadModuleChecked<FSceneOutlinerModule>("SceneOutliner");
		SceneOutliner::FDefaultColumnInfo* ActorInfoColumPtr = SceneOutlinerModule.DefaultColumnMap.Find( SceneOutliner::FBuiltInColumnTypes::ActorInfo() );
		check( ActorInfoColumPtr );
		SceneOutliner->AddColumn( SceneOutliner::FBuiltInColumnTypes::ActorInfo(), ActorInfoColumPtr->ColumnInfo );

		AssetPreviewView->RemoveColumn( FDataprepPreviewAssetColumn::ColumnID );
	}
}

void FDataprepEditor::UpdateDataForPreviewSystem()
{
	TArray<UObject*> ObjectsForThePreviewSystem;
	FDataprepCoreUtils::GetActorsFromWorld( PreviewWorld.Get(), ObjectsForThePreviewSystem );
	ObjectsForThePreviewSystem.Reserve( Assets.Num() );
	for ( TWeakObjectPtr<UObject>& WeakObjectPtr : Assets )
	{
		if ( UObject* Object = WeakObjectPtr.Get() )
		{
			ObjectsForThePreviewSystem.Add( Object );
		}
	}
	PreviewSystem->UpdateDataToProcess( MakeArrayView( ObjectsForThePreviewSystem ) );
}

bool FDataprepEditor::IsPreviewingStep(const UDataprepParameterizableObject* StepObject) const 
{
	return PreviewSystem->IsObservingStepObject( StepObject );
}

int32 FDataprepEditor::GetCountOfPreviewedSteps() const
{
	return PreviewSystem->GetObservedStepsCount();
}

void FDataprepEditor::OnStepObjectsAboutToBeDeleted(const TArrayView<UDataprepParameterizableObject*>& StepObjects)
{
	for ( UDataprepParameterizableObject* StepObject : StepObjects )
	{
		if ( IsPreviewingStep( StepObject ) )
		{
			ClearPreviewedObjects();
			break;
		}
	}
}

void FDataprepEditor::PostUndo(bool bSuccess)
{
	if ( bSuccess )
	{
		PreviewSystem->EnsureValidityOfTheObservedObjects();
		RefreshColumnsForPreviewSystem();
	}
}

void FDataprepEditor::PostRedo(bool bSuccess)
{
	if ( bSuccess )
	{
		PreviewSystem->EnsureValidityOfTheObservedObjects();
		RefreshColumnsForPreviewSystem();
	}
}

void FDataprepEditor::SetPreviewedObjects(const TArrayView<UDataprepParameterizableObject*>& ObservedObjects)
{
	PreviewSystem->SetObservedObjects( ObservedObjects );
	
	if ( SDataprepGraphEditor* RawGraphEditor = GraphEditor.Get() )
	{
		// Refresh the graph
		RawGraphEditor->NotifyGraphChanged();
	}

	RefreshColumnsForPreviewSystem();
}

void FDataprepEditor::ClearPreviewedObjects()
{
	SetPreviewedObjects( MakeArrayView<UDataprepParameterizableObject*>( nullptr, 0 ) );
}

#undef LOCTEXT_NAMESPACE
