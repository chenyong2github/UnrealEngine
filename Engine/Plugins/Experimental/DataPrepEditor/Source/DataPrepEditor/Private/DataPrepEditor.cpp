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
#include "DataprepEditorLogCategory.h"
#include "SchemaActions/DataprepOperationMenuActionCollector.h"
#include "Widgets/SAssetsPreviewWidget.h"
#include "Widgets/SDataprepPalette.h"

#include "ActorEditorUtils.h"
#include "AssetDeleteModel.h"
#include "AssetRegistryModule.h"
#include "BlueprintNodeSpawner.h"
#include "DesktopPlatformModule.h"
#include "Dialogs/DlgPickPath.h"
#include "EditorDirectories.h"
#include "Editor.h"
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
#include "Toolkits/IToolkit.h"
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
	FDataprepProgressReporter( const FText& Title )
	{
		ProgressTask = TSharedPtr<FScopedSlowTask>( new FScopedSlowTask( 100.0f, Title, true, *GWarn ) );
		ProgressTask->MakeDialog(true);
	}

	virtual ~FDataprepProgressReporter() {}

	// Begin IDataprepProgressReporter interface
	virtual void ReportProgress(float Progress, const UObject& InObject) override
	{
		ReportProgressWithMessage( Progress, ProgressTask->GetCurrentMessage(), InObject );
	}

	virtual void ReportProgressWithMessage(float Progress, const FText& InMessage, const UObject& InObject)
	{
		if( ProgressTask.IsValid() )
		{
			FText ProgressMsg = FText::FromString( FString::Printf( TEXT("%s : %s ..."), *InObject.GetName(), *InMessage.ToString() ) );
			ProgressTask->EnterProgressFrame( Progress, ProgressMsg );
			ProgressTask->EnterProgressFrame( 0.0f, ProgressMsg );
		}
	}
	// End IDataprepProgressReporter interface

private:
	TSharedPtr< FScopedSlowTask > ProgressTask;
};

FDataprepEditor::FDataprepEditor()
	: bWorldBuilt(false)
	, bIsFirstRun(false)
	, bPipelineExecuted(false)
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
	}

	if ( PreviewWorld )
	{
		GEngine->DestroyWorldContext( PreviewWorld.Get() );
		PreviewWorld->DestroyWorld( true );
		PreviewWorld.Reset();
	}

	FCoreUObjectDelegates::OnObjectPropertyChanged.RemoveAll(this);

	if (IFileManager::Get().DirectoryExists( *TempDir ))
	{
		IFileManager::Get().DeleteDirectory( *TempDir, true, true );
	}
}

FName FDataprepEditor::GetToolkitFName() const
{
	return FName("DataprepEditor");
}

FText FDataprepEditor::GetBaseToolkitName() const
{
	return LOCTEXT("AppLabel", "StaticMesh Editor");
}

FString FDataprepEditor::GetWorldCentricTabPrefix() const
{
	return LOCTEXT("WorldCentricTabPrefix", "StaticMesh ").ToString();
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

void FDataprepEditor::InitDataprepEditor(const EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& InitToolkitHost, UDataprepAsset* InDataprepAsset)
{
	DataprepAssetPtr = TWeakObjectPtr<UDataprepAsset>(InDataprepAsset);
	check( DataprepAssetPtr.IsValid() );

	DataprepAssetPtr->GetOnChanged().AddRaw( this, &FDataprepEditor::OnDataprepAssetChanged );

	// Assign unique session identifier
	SessionID = FGuid::NewGuid().ToString();

	// Create temporary directory which will be used by UDatasmithStaticMeshCADImportData to store transient data
	TempDir = FPaths::Combine(FPaths::ProjectIntermediateDir(), TEXT("DataprepTemp"), SessionID);
	IFileManager::Get().MakeDirectory(*TempDir);

	// Temp code for the nodes development
	DataprepRecipeBPPtr = DataprepAssetPtr->DataprepRecipeBP;
	check( DataprepRecipeBPPtr.IsValid() );

	UEdGraph* PipelineGraph = FBlueprintEditorUtils::FindEventGraph(DataprepRecipeBPPtr.Get());
	check( PipelineGraph );

	TArray< class UK2Node_DataprepAction* > ActionNodes;

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

	FDataprepEditorStyle::Initialize();

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

	const TSharedRef<FTabManager::FLayout> StandaloneDefaultLayout = FTabManager::NewLayout("Standalone_DataprepEditor_Layout_v0.2")
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
						)
						->Split
						(
							FTabManager::NewStack()
							->SetSizeCoefficient(0.5f)
							->AddTab(AssetPreviewTabId, ETabState::OpenedTab)
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
						)
						// Temp code for the nodes development
						->Split
						(
							FTabManager::NewStack()
							->SetSizeCoefficient(0.85f)
							->AddTab(PipelineGraphTabId, ETabState::OpenedTab)
						)
						// end of temp code for nodes development
					)
				)
				->Split
				(
					FTabManager::NewStack()
					->SetSizeCoefficient(0.25f)
					->AddTab(DetailsTabId, ETabState::OpenedTab)
				)
			)
		);

	const bool bCreateDefaultStandaloneMenu = true;
	const bool bCreateDefaultToolbar = true;
	FAssetEditorToolkit::InitAssetEditor(Mode, InitToolkitHost, DataprepEditorAppIdentifier, StandaloneDefaultLayout, bCreateDefaultToolbar, bCreateDefaultStandaloneMenu, DataprepAssetPtr.Get());

	ExtendMenu();
	ExtendToolBar();
	RegenerateMenusAndToolbars();

	// Set the details panel to inspect consumer and producers
	OnShowSettings();
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
		Commands.ShowDataprepSettings,
		FExecuteAction::CreateSP(this, &FDataprepEditor::OnShowSettings),
		FCanExecuteAction(),
		FIsActionChecked::CreateSP(this, &FDataprepEditor::IsShowingSettings));

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

	// Temp code for the nodes development
	UDataprepRecipe* DataprepRecipePtr = CastChecked<UDataprepRecipe>(DataprepRecipeBPPtr->GeneratedClass->GetDefaultObject());

	DataprepRecipePtr->SetTargetWorld(nullptr);
	DataprepRecipePtr->ResetAssets();
	// end of temp code for nodes development

	if (DataprepAsset->GetProducersCount() == 0)
	{
		ResetBuildWorld();
		return;
	}

	uint64 StartTime = FPlatformTime::Cycles64();
	UE_LOG( LogDataprepEditor, Log, TEXT("Importing ...") );

	CleanPreviewWorld();

	if(UEdGraph* PipelineGraph = GetPipelineGraph())
	{
		PipelineView->ClearSelectionSet();
		PipelineView->SetNodeSelection( StartNode, true );
		FSlateApplication::Get().PumpMessages();
		FSlateApplication::Get().Tick();
	}

	UPackage* TransientPackage = NewObject< UPackage >( nullptr, *GetTransientContentFolder(), RF_Transient );
	TransientPackage->FullyLoad();

	// #ueent_todo: Add progress reporter and logger to Dataprep editor
	UDataprepContentProducer::ProducerContext Context;
	Context.SetWorld( PreviewWorld.Get() )
		.SetRootPackage( TransientPackage )
		.SetLogger( TSharedPtr< IDataprepLogger >( new FDataprepLogger ) )
		.SetProgressReporter( TSharedPtr< IDataprepProgressReporter >( new FDataprepProgressReporter( LOCTEXT("Dataprep_BuildWorld", "Importing ...") ) ) );

	DataprepAssetPtr->RunProducers( Context, Assets );

	CachedAssets.Reset();
	CachedAssets.Append( Assets );

	UpdatePreviewPanels();
	bWorldBuilt = true;
	bIsFirstRun = true;
	bPipelineExecuted = false;

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
				// #ueent_todo: Anything to do there ?
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
			}
		}
	}

	// Prevent display of progress dialog from ObjectTools::DeleteObjectsUnchecked
	bool bGIsSilentCached = GIsSilent;
	GIsSilent = true;

	ensure( ObjectTools::DeleteObjectsUnchecked( ObjectsToDelete ) == ObjectsToDelete.Num() );

	// Restore GIsSilent
	GIsSilent = bGIsSilentCached;

	CachedAssets.Reset();
	Assets.Reset();

	if (GEngine)
	{
		// Otherwise we end up with some issues
		GEngine->PerformGarbageCollectionAndCleanupActors();
	}
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

	// Temp code for the nodes development
	UDataprepRecipe* DataprepRecipePtr = CastChecked<UDataprepRecipe>( DataprepRecipeBPPtr->GeneratedClass->GetDefaultObject() );

	// Set the world to pull actors from when executing pipeline
	DataprepRecipePtr->SetTargetWorld(PreviewWorld.Get());

	// Set array of assets to be processed when executing pipeline
	DataprepRecipePtr->SetAssets(Assets);

	// end of temp code for nodes development

	TSharedPtr< IDataprepProgressReporter > ProgressReporter( new FDataprepProgressReporter( LOCTEXT("Dataprep_ExecutePipeline", "Executing pipeline ...") ) );

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
			UEdGraphPin* NextNodeInPin = StartNodePin->LinkedTo[0];
			while( NextNodeInPin != nullptr )
			{
				UEdGraphNode* NextNode = NextNodeInPin->GetOwningNode();
				if( UK2Node_DataprepAction* ActionNode = Cast<UK2Node_DataprepAction>( NextNode ) )
				{
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

					// Highlight action being executed
					PipelineView->ClearSelectionSet();
					PipelineView->SetNodeSelection( ActionNode, true );
					FSlateApplication::Get().PumpMessages();
					FSlateApplication::Get().Tick();

					// Execute action
					ActionNode->GetDataprepAction()->Execute( Objects );
					ActionNodesExecuted.Add( ActionNode );

					// Update array of assets in case something was removed
					int32 Index = 0;
					while ( Index < Assets.Num() )
					{
						UObject* Object = Assets[Index].Get();
						if ( Object && Object->IsValidLowLevelFast() )
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
					FSlateApplication::Get().PumpMessages();
					FSlateApplication::Get().Tick();
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

	// Indicate pipeline has been executed
	bIsFirstRun = false;
	bPipelineExecuted = true;
}

void FDataprepEditor::OnCommitWorld()
{
	if(!bPipelineExecuted)
	{
		// #ueent_todo Prompt user if pipeline has not been run on imported assets
	}

	// Finalize assets
	TArray<TWeakObjectPtr<UObject>> ValidAssets( MoveTemp(Assets) );

	UDataprepContentConsumer::ConsumerContext Context;
	Context.SetWorld( PreviewWorld.Get() )
		.SetAssets( ValidAssets )
		.SetTransientContentFolder( GetTransientContentFolder() )
		.SetLogger( TSharedPtr<IDataprepLogger>( new FDataprepLogger ) )
		.SetProgressReporter( TSharedPtr< IDataprepProgressReporter >( new FDataprepProgressReporter( LOCTEXT("Dataprep_CommitWorld", "Committing ...") ) ) );

	FString OutReason;
	if( !DataprepAssetPtr->RunConsumer( Context, OutReason ) )
	{
		// #ueent_todo: Inform consumer failed
	}

	PipelineView->ClearSelectionSet();
	FSlateApplication::Get().PumpMessages();
	FSlateApplication::Get().Tick();

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
			ToolbarBuilder.BeginSection("Settings");
			{
				ToolbarBuilder.AddToolBarButton(FDataprepEditorCommands::Get().ShowDataprepSettings);
			}
			ToolbarBuilder.EndSection();

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

void FDataprepEditor::UpdatePreviewPanels()
{
	// #ueent_todo: There should be a event triggered to inform listeners
	//				   that new assets have been generated.
	AssetPreviewView->ClearAssetList();
	FString SubstitutePath = DataprepAssetPtr->GetOutermost()->GetName();
	if(!DataprepAssetPtr->GetConsumer()->GetTargetContentFolder().IsEmpty())
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
	return FPaths::Combine( TEXT("/DataPrepEditor"), SessionID );
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
