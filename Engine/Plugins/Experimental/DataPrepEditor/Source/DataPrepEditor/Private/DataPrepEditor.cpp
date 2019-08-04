// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "DataPrepEditor.h"

#include "BlueprintNodes/K2Node_DataprepAction.h"
#include "BlueprintNodes/K2Node_DataprepProducer.h"
#include "DataPrepContentConsumer.h"
#include "DataPrepContentProducer.h"
#include "DataPrepEditorActions.h"
#include "DataPrepEditorModule.h"
#include "DataPrepEditorStyle.h"
#include "DataPrepRecipe.h"
#include "DataprepActionAsset.h"
#include "DataprepCoreUtils.h"
#include "DataprepEditorLogCategory.h"
#include "SchemaActions/DataprepOperationMenuActionCollector.h"
#include "Widgets/DataprepAssetView.h"
#include "Widgets/SAssetsPreviewWidget.h"
#include "Widgets/SDataprepPalette.h"

#include "ActorEditorUtils.h"
#include "AssetDeleteModel.h"
#include "AssetRegistryModule.h"
#include "BlueprintNodeSpawner.h"
#include "DesktopPlatformModule.h"
#include "Dialogs/DlgPickPath.h"
#include "Dialogs/Dialogs.h"
#include "Editor.h"
#include "EditorDirectories.h"
#include "EditorStyleSet.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "GenericPlatform/GenericPlatformTime.h"
#include "HAL/FileManager.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Materials/MaterialInstance.h"
#include "Misc/Attribute.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Misc/ScopedSlowTask.h"
#include "Modules/ModuleManager.h"
#include "ObjectTools.h"
#include "SceneOutlinerModule.h"
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

const FName FDataprepEditor::PipelineGraphTabId(TEXT("DataprepEditor_Pipeline_Graph"));
// end of temp code for nodes development

#define LOCTEXT_NAMESPACE "DataprepEditor"

extern const FName DataprepEditorAppIdentifier;

const FName FDataprepEditor::ScenePreviewTabId(TEXT("DataprepEditor_ScenePreview"));
const FName FDataprepEditor::AssetPreviewTabId(TEXT("DataprepEditor_AssetPreview"));
const FName FDataprepEditor::PaletteTabId(TEXT("DataprepEditor_Palette"));
const FName FDataprepEditor::DetailsTabId(TEXT("DataprepEditor_Details"));
const FName FDataprepEditor::DataprepAssetTabId(TEXT("DataprepEditor_Dataprep"));


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

class FDataprepLogger : public IDataprepLogger
{
public:
	virtual ~FDataprepLogger() {}

	// Begin IDataprepLogger interface
	virtual void LogInfo(const FText& InLogText, const UObject& InObject) override
	{
		UE_LOG( LogDataprepEditor, Log, TEXT("%s : %s"), *InObject.GetName(), *InLogText.ToString() );
	}

	virtual void LogWarning(const FText& InLogText, const UObject& InObject) override
	{
		UE_LOG( LogDataprepEditor, Warning, TEXT("%s : %s"), *InObject.GetName(), *InLogText.ToString() );
	}

	virtual void LogError(const FText& InLogText,  const UObject& InObject) override
	{
		UE_LOG( LogDataprepEditor, Error, TEXT("%s : %s"), *InObject.GetName(), *InLogText.ToString() );
	}
	// End IDataprepLogger interface

};

class FDataprepProgressReporter : public IDataprepProgressReporter
{
public:
	FDataprepProgressReporter()
	{
	}

	virtual ~FDataprepProgressReporter()
	{
	}

	virtual void PushTask( const FText& InTitle, float InAmountOfWork ) override
	{
		ProgressTasks.Emplace( new FScopedSlowTask( InAmountOfWork, InTitle, true, *GWarn ) );
		ProgressTasks.Last()->MakeDialog(true);
	}

	virtual void PopTask() override
	{
		if(ProgressTasks.Num() > 0)
		{
			ProgressTasks.Pop();
		}
	}

	// Begin IDataprepProgressReporter interface
	virtual void ReportProgress( float Progress, const FText& InMessage ) override
	{
		if( ProgressTasks.Num() > 0 )
		{
			TSharedPtr<FScopedSlowTask>& ProgressTask = ProgressTasks.Last();
			ProgressTask->EnterProgressFrame( Progress, InMessage );
		}
	}

private:
	TArray< TSharedPtr< FScopedSlowTask > > ProgressTasks;
};

FDataprepEditor::FDataprepEditor()
	: bWorldBuilt(false)
	, bIsFirstRun(false)
	, bPipelineChanged(false)
	, bIsActionMenuContextSensitive(true)
	, bSaveIntermediateBuildProducts(false)
	, PreviewWorld(nullptr)
	, bIgnoreCloseRequest(false)
	, StartNode(nullptr)
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
	if( DataprepAssetPtr.IsValid() )
	{
		DataprepAssetPtr->GetOnChanged().RemoveAll( this );
		DataprepAssetPtr->GetOnPipelineChange().RemoveAll( this );
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

		DeleteDirectory(TempDir );

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

	InTabManager->RegisterTabSpawner(PaletteTabId, FOnSpawnTab::CreateSP(this, &FDataprepEditor::SpawnTabPalette))
		.SetDisplayName(LOCTEXT("PaletteTab", "Palette"))
		.SetGroup(WorkspaceMenuCategoryRef)
		.SetIcon( FSlateIcon(FEditorStyle::GetStyleSetName(), "Kismet.Tabs.Palette"));

	InTabManager->RegisterTabSpawner(DetailsTabId, FOnSpawnTab::CreateSP(this, &FDataprepEditor::SpawnTabDetails))
		.SetDisplayName(LOCTEXT("DetailsTab", "Details"))
		.SetGroup(WorkspaceMenuCategoryRef)
		.SetIcon(FSlateIcon(FEditorStyle::GetStyleSetName(), "LevelEditor.Tabs.Details"));

	InTabManager->RegisterTabSpawner(DataprepAssetTabId, FOnSpawnTab::CreateSP(this, &FDataprepEditor::SpawnTabDataprep))
		.SetDisplayName(LOCTEXT("DataprepAssetTab", "Dataprep"))
		.SetGroup(WorkspaceMenuCategoryRef)
		.SetIcon(FSlateIcon(FEditorStyle::GetStyleSetName(), "LevelEditor.Tabs.Details"));

	// Temp code for the nodes development
	InTabManager->RegisterTabSpawner(PipelineGraphTabId, FOnSpawnTab::CreateSP(this, &FDataprepEditor::SpawnTabPipelineGraph))
		.SetDisplayName(LOCTEXT("PipelineGraphTab", "Pipeline Graph"))
		.SetGroup(WorkspaceMenuCategoryRef)
		.SetIcon(FSlateIcon(FEditorStyle::GetStyleSetName(), "GraphEditor.EventGraph_16x"));
	// end of temp code for nodes development
}

void FDataprepEditor::UnregisterTabSpawners(const TSharedRef<class FTabManager>& InTabManager)
{
	FAssetEditorToolkit::UnregisterTabSpawners(InTabManager);
	InTabManager->UnregisterTabSpawner(ScenePreviewTabId);
	InTabManager->UnregisterTabSpawner(AssetPreviewTabId);
	InTabManager->UnregisterTabSpawner(PaletteTabId);
	InTabManager->UnregisterTabSpawner(DetailsTabId);
	// Temp code for the nodes development
	InTabManager->UnregisterTabSpawner(PipelineGraphTabId);
	// end of temp code for nodes development
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
	static FString RootPackagePath( TEXT("/DataprepEditor/Transient") );
	return RootPackagePath;
}

void FDataprepEditor::InitDataprepEditor(const EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& InitToolkitHost, UDataprepAsset* InDataprepAsset)
{
	DataprepAssetPtr = TWeakObjectPtr<UDataprepAsset>(InDataprepAsset);
	check( DataprepAssetPtr.IsValid() );

	DataprepAssetPtr->GetOnChanged().AddRaw( this, &FDataprepEditor::OnDataprepAssetChanged );
	DataprepAssetPtr->GetOnPipelineChange().AddRaw( this, &FDataprepEditor::OnDataprepPipelineChange );


	// Assign unique session identifier
	SessionID = FGuid::NewGuid().ToString();

	// Create temporary directory to store transient data
	CleanUpTemporaryDirectories();
	TempDir = FPaths::Combine( GetRootTemporaryDir(), FString::FromInt( FPlatformProcess::GetCurrentProcessId() ), SessionID);
	IFileManager::Get().MakeDirectory(*TempDir);

	// Temp code for the nodes development
	DataprepRecipeBPPtr = DataprepAssetPtr->DataprepRecipeBP;
	check( DataprepRecipeBPPtr.IsValid() );

	// Necessary step to regenerate blueprint generated class
	// Note that this compilation will always succeed as Dataprep node does not have real body
	// #ueent_todo: Is there a better solution
	{
		FKismetEditorUtilities::CompileBlueprint( DataprepRecipeBPPtr.Get(), EBlueprintCompileOptions::None, nullptr );
	}

	UEdGraph* PipelineGraph = FBlueprintEditorUtils::FindEventGraph(DataprepRecipeBPPtr.Get());
	check( PipelineGraph );

	for( UEdGraphNode* GraphNode : PipelineGraph->Nodes )
	{
		if( GraphNode->IsA<UK2Node_DataprepProducer>() )
		{
			StartNode = GraphNode;
		}
		else if(StartNode != nullptr)
		{
			break;
		}
	}

	// This should normally happen only with a brand new Dataprep asset
	if( StartNode == nullptr )
	{
		UEdGraph* EventGraph = FBlueprintEditorUtils::FindEventGraph(DataprepRecipeBPPtr.Get());

		IBlueprintNodeBinder::FBindingSet Bindings;
		UK2Node_DataprepProducer* ProducerNode = Cast<UK2Node_DataprepProducer>(UBlueprintNodeSpawner::Create<UK2Node_DataprepProducer>()->Invoke(EventGraph, Bindings, FVector2D(-100,0)));

		ProducerNode->SetDataprepAsset( DataprepAssetPtr.Get() );
		StartNode = ProducerNode;
	}
	// end of temp code for nodes development

	GEditor->RegisterForUndo(this);

	// Register our commands. This will only register them if not previously registered
	FDataprepEditorCommands::Register();

	BindCommands();

	CreateTabs();

	const TSharedRef<FTabManager::FLayout> StandaloneDefaultLayout = FTabManager::NewLayout("Standalone_DataprepEditor_Layout_v0.3")
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
					->SetSizeCoefficient(0.9f)
					->Split
					(
						FTabManager::NewSplitter()->SetOrientation(Orient_Horizontal)
						->SetSizeCoefficient(0.5f)
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
						->SetSizeCoefficient(0.25f)
						->AddTab(DataprepAssetTabId, ETabState::OpenedTab)
						->SetHideTabWell(true)
					)
					->Split
					(
						FTabManager::NewStack()
						->SetSizeCoefficient(0.75f)
						->AddTab(DetailsTabId, ETabState::OpenedTab)
					)
				)
			)
		);

	const bool bCreateDefaultStandaloneMenu = true;
	const bool bCreateDefaultToolbar = true;
	FAssetEditorToolkit::InitAssetEditor( Mode, InitToolkitHost, DataprepEditorAppIdentifier, StandaloneDefaultLayout, bCreateDefaultToolbar, bCreateDefaultStandaloneMenu, InDataprepAsset );

	ExtendMenu();
	ExtendToolBar();
	RegenerateMenusAndToolbars();
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
}

void FDataprepEditor::OnBuildWorld()
{
	UDataprepAsset* DataprepAsset = GetDataprepAsset();
	if (!ensureAlways(DataprepAsset))
	{
		return;
	}

	if (!ensureAlways(PreviewWorld.IsValid()))
	{
		return;
	}

	if (DataprepAsset->GetProducersCount() == 0)
	{
		ResetBuildWorld();
		return;
	}

	uint64 StartTime = FPlatformTime::Cycles64();
	UE_LOG( LogDataprepEditor, Log, TEXT("Importing ...") );

	CleanPreviewWorld();

	UPackage* TransientPackage = NewObject< UPackage >( nullptr, *GetTransientContentFolder(), RF_Transient );
	TransientPackage->FullyLoad();

	// #ueent_todo: Add progress reporter and logger to Dataprep editor
	UDataprepContentProducer::ProducerContext Context;
	Context.SetWorld( PreviewWorld.Get() )
		.SetRootPackage( TransientPackage )
		.SetLogger( TSharedPtr< IDataprepLogger >( new FDataprepLogger ) )
		.SetProgressReporter( TSharedPtr< IDataprepProgressReporter >( new FDataprepProgressReporter() ) );

	DataprepAssetPtr->RunProducers( Context, Assets );

	CachedAssets.Reset();
	CachedAssets.Append( Assets );

	UpdatePreviewPanels();
	bWorldBuilt = true;
	bIsFirstRun = true;

	// Log time spent to import incoming file in minutes and seconds
	double ElapsedSeconds = FPlatformTime::ToSeconds64(FPlatformTime::Cycles64() - StartTime);

	int ElapsedMin = int(ElapsedSeconds / 60.0);
	ElapsedSeconds -= 60.0 * (double)ElapsedMin;
	UE_LOG( LogDataprepEditor, Log, TEXT("Import took [%d min %.3f s]"), ElapsedMin, ElapsedSeconds );

	TakeSnapshot();
}

void FDataprepEditor::OnDataprepAssetChanged( FDataprepAssetChangeType ChangeType, int32 Index )
{
	switch(ChangeType)
	{
		case FDataprepAssetChangeType::ConsumerModified:
			{
				UpdatePreviewPanels();
			}
			break;

		case FDataprepAssetChangeType::BlueprintModified:
			{
				OnDataprepPipelineChange( nullptr );
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

void FDataprepEditor::OnDataprepPipelineChange(UObject* ChangedObject)
{
	bPipelineChanged = true;
}

void FDataprepEditor::ResetBuildWorld()
{
	bWorldBuilt = false;
	CleanPreviewWorld();
	UpdatePreviewPanels();
	DataprepEditorUtil::DeleteTemporaryPackage( GetTransientContentFolder() );
}

void FDataprepEditor::CleanPreviewWorld()
{
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
				ObjectToDelete->Rename( nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional );
				ObjectsToDelete.Add( ObjectToDelete );

				// Remove geometry from static meshes to be deleted to avoid unwanted rebuild done when calling FDataprepCoreUtils::PurgeObjects
				// #ueent_todo: This is a temporary solution. Need to find a better way to do that
				if( UStaticMesh* StaticMesh = Cast<UStaticMesh>( ObjectToDelete ) )
				{
					StaticMesh->ReleaseResources();
					StaticMesh->ClearMeshDescriptions();
					StaticMesh->GetSourceModels().Empty();
					StaticMesh->StaticMaterials.Empty();
				}
			}
		}
	}

	// #ueent_todo: Should we find a better way to silently delete assets?
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
	if( DataprepAssetPtr->GetConsumer() == nullptr )
	{
		return;
	}

	if(!bIsFirstRun)
	{
		RestoreFromSnapshot();
	}

	TSharedPtr< IDataprepProgressReporter > ProgressReporter( new FDataprepProgressReporter() );

	// Trigger execution of data preparation operations on world attached to recipe
	{
		// Some operation can indirectly call FAssetEditorManager::CloseAllAssetEditors (eg. Remove Asset)
		// Editors can individually refuse this request: we ignore it during the pipeline traversal.
		TGuardValue<bool> IgnoreCloseRequestGuard(bIgnoreCloseRequest, true);

		// Start of temp code for nodes execution
		// Simulate sequential execution of Dataprep actions starting at StartNode
		TSet<UK2Node_DataprepAction*> ActionNodesExecuted;
		UEdGraphPin* StartNodePin = StartNode->FindPin(UEdGraphSchema_K2::PN_Then, EGPD_Output);
		if( StartNodePin && StartNodePin->LinkedTo.Num() > 0 )
		{
			int32 ActionNodeCount = 0;
			for( UEdGraphPin* NextNodeInPin = StartNodePin->LinkedTo[0]; NextNodeInPin != nullptr ; )
			{
				UEdGraphNode* NextNode = NextNodeInPin->GetOwningNode();

				if(UK2Node_DataprepAction* ActionNode = Cast<UK2Node_DataprepAction>(NextNode))
				{
					++ActionNodeCount;
				}

				UEdGraphPin* NextNodeOutPin = NextNode->FindPin( UEdGraphSchema_K2::PN_Then, EGPD_Output );
				NextNodeInPin = NextNodeOutPin ? ( NextNodeOutPin->LinkedTo.Num() > 0 ? NextNodeOutPin->LinkedTo[0] : nullptr ) : nullptr;
			}

			ActionNodesExecuted.Reserve( ActionNodeCount );

			FDataprepProgressTask Task( *ProgressReporter, LOCTEXT( "DataprepEditor_ExecutingPipeline", "Executing pipeline ..." ), (float)ActionNodeCount, 1.0f );

			for( UEdGraphPin* NextNodeInPin = StartNodePin->LinkedTo[0]; NextNodeInPin != nullptr ; )
			{
				UEdGraphNode* NextNode = NextNodeInPin->GetOwningNode();
				if( UK2Node_DataprepAction* ActionNode = Cast<UK2Node_DataprepAction>( NextNode ) )
				{
					Task.ReportNextStep( FText::Format( LOCTEXT( "DataprepEditor_ExecutingAction", "Executing \"{0}\" ..."), ActionNode->GetNodeTitle( ENodeTitleType::FullTitle ) ) );

					// Break the loop if the node had already been executed
					if( ActionNodesExecuted.Find( ActionNode ) )
					{
						break;
					}

					// Collect all objects the action should be applied on
					// Done for each action node since an operation in an action could modify the world or add/remove assets
					TArray<UObject*> Objects;
					Objects.Reserve( PreviewWorld->GetCurrentLevel()->Actors.Num() + Assets.Num() );

					for( TWeakObjectPtr<UObject>& Object : Assets )
					{
						if( Object.IsValid() && !Object.Get()->IsPendingKill() )
						{
							Objects.Add( Object.Get()  );
						}
					}

					for( AActor* Actor : PreviewWorld->GetCurrentLevel()->Actors )
					{
						const bool bIsValidActor = Actor &&
							!Actor->IsPendingKill() &&
							Actor->IsEditable() &&
							!Actor->IsTemplate() &&
							!FActorEditorUtils::IsABuilderBrush(Actor) &&
							!Actor->IsA(AWorldSettings::StaticClass());

						if( bIsValidActor )
						{
							Objects.Add( Actor  );
						}
					}

					// Execute action
					ActionNode->GetDataprepAction()->Execute( Objects );
					ActionNodesExecuted.Add( ActionNode );

					// Update array of assets in case something was removed
					int32 Index = 0;
					while ( Index < Assets.Num() )
					{
						UObject* Object = Assets[Index].Get();
						if ( Object && Object->IsValidLowLevel() )
						{
							Index++;
						}
						else
						{
							Assets.RemoveAtSwap(Index);
						}
					}

					// World may have changed, update asset preview and scene outliner
					UpdatePreviewPanels();
				}

				UEdGraphPin* NextNodeOutPin = NextNode->FindPin( UEdGraphSchema_K2::PN_Then, EGPD_Output );
				NextNodeInPin = NextNodeOutPin ? ( NextNodeOutPin->LinkedTo.Num() > 0 ? NextNodeOutPin->LinkedTo[0] : nullptr ) : nullptr;
			}
			// End of temp code for nodes execution
		}
	}

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
	// Pipeline has not been executed, validate with user this is intentional
	if( bIsFirstRun )
	{
		UEdGraphPin* StartNodePin = StartNode->FindPin( UEdGraphSchema_K2::PN_Then, EGPD_Output );
		if(StartNodePin && StartNodePin->LinkedTo.Num() > 0)
		{
			const FText Title( LOCTEXT( "DataprepEditor_ProceedWithCommit", "Proceed with commit" ) );
			const FText Message( LOCTEXT( "DataprepEditor_ConfirmCommitPipelineNotExecuted", "The action pipeline has not been executed.\nDo you want to proceeed with the commit anyway?" ) );

			if( OpenMsgDlgInt( EAppMsgType::YesNo, Message, Title ) == EAppReturnType::No )
			{
				return;
			}
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

	// Finalize assets
	TArray<TWeakObjectPtr<UObject>> ValidAssets( MoveTemp(Assets) );

	UDataprepContentConsumer::ConsumerContext Context;
	Context.SetWorld( PreviewWorld.Get() )
		.SetAssets( ValidAssets )
		.SetTransientContentFolder( GetTransientContentFolder() )
		.SetLogger( TSharedPtr<IDataprepLogger>( new FDataprepLogger ) )
		.SetProgressReporter( TSharedPtr< IDataprepProgressReporter >( new FDataprepProgressReporter() ) );

	FString OutReason;
	if( !DataprepAssetPtr->RunConsumer( Context, OutReason ) )
	{
		// #ueent_todo: Inform consumer failed
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

	DataprepAssetView = SNew( SDataprepAssetView, DataprepAssetPtr.Get(), PipelineEditorCommands );

	CreateScenePreviewTab();

	// Create Details Panel
	CreateDetailsViews();

	// Temp code for the nodes development
	// Create Pipeline Editor
	CreatePipelineEditor();
	// end of temp code for nodes development

}

// Temp code for the nodes development
TSharedRef<SDockTab> FDataprepEditor::SpawnTabPipelineGraph(const FSpawnTabArgs & Args)
{
	check(Args.GetTabId() == PipelineGraphTabId);

	return SNew(SDockTab)
		//.Icon(FDataprepEditorStyle::Get()->GetBrush("DataprepEditor.Tabs.Pipeline"))
		.Label(LOCTEXT("DataprepEditor_PipelineTab_Title", "Pipeline"))
		[
			PipelineView.ToSharedRef()
		];
}
// end of temp code for nodes development

void FDataprepEditor::CreateScenePreviewTab()
{
	FSceneOutlinerModule& SceneOutlinerModule = FModuleManager::Get().LoadModuleChecked<FSceneOutlinerModule>("SceneOutliner");
	SceneOutliner::FInitializationOptions SceneOutlinerOptions = SceneOutliner::FInitializationOptions();
	SceneOutlinerOptions.SpecifiedWorldToDisplay = PreviewWorld.Get();
	SceneOutliner = SceneOutlinerModule.CreateSceneOutliner(SceneOutlinerOptions,
		FOnActorPicked::CreateLambda(
			[this](AActor* PickedActor)
			{
				TSet< UObject* > Selection;
				Selection.Add(PickedActor);

				SetDetailsObjects(MoveTemp(Selection), false);
			}
			)
		);

		SAssignNew(ScenePreviewView, SBorder)
		.Padding(2.f)
		.BorderImage(FEditorStyle::GetBrush("ToolPanel.GroupBorder"))
		[
			SNew(SOverlay)
			+ SOverlay::Slot()
			.HAlign(HAlign_Fill)
			.VAlign(VAlign_Fill)
			[
				SceneOutliner.ToSharedRef()
			]
		];
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

	return SNew(SDockTab)
		.Icon(FSlateIcon(FEditorStyle::GetStyleSetName(), "Kismet.Tabs.Palette").GetIcon())
		.Label(LOCTEXT("PaletteTab", "Palette"))
		[
			SNew(SDataprepPalette)
		];
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

void FDataprepEditor::UpdatePreviewPanels()
{
	// #ueent_todo: There should be a event triggered to inform listeners
	//				   that new assets have been generated.
	AssetPreviewView->ClearAssetList();
	FString SubstitutePath = DataprepAssetPtr->GetOutermost()->GetName();
	if(DataprepAssetPtr->GetConsumer() != nullptr && !DataprepAssetPtr->GetConsumer()->GetTargetContentFolder().IsEmpty())
	{
		SubstitutePath = DataprepAssetPtr->GetConsumer()->GetTargetContentFolder();
	}
	AssetPreviewView->SetAssetsList( Assets, GetTransientContentFolder(), SubstitutePath );
	SceneOutliner->Refresh();
}

bool FDataprepEditor::OnRequestClose()
{
	return !bIgnoreCloseRequest;
}

bool FDataprepEditor::CanBuildWorld()
{
	return DataprepAssetPtr->GetProducersCount() > 0;
}

bool FDataprepEditor::CanExecutePipeline()
{
	return bWorldBuilt;
}

bool FDataprepEditor::CanCommitWorld()
{
	// Execution of pipeline is not required. User can directly commit result of import
	return bWorldBuilt && DataprepAssetPtr->GetConsumer() != nullptr;
}


FString FDataprepEditor::GetTransientContentFolder()
{
	return FPaths::Combine( GetRootPackagePath(), FString::FromInt( FPlatformProcess::GetCurrentProcessId() ), SessionID );
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
			// #ueent_todo: We should not use ObjectTools::DeleteObjects but FAssetDeleteModel 
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
