// Copyright Epic Games, Inc. All Rights Reserved.

#include "Toolkit/RenderPageCollectionEditor.h"

#include "RenderPage/RenderPageCollection.h"
#include "RenderPage/RenderPageManager.h"
#include "RenderPage/RenderPageMoviePipelineJob.h"
#include "RenderPageCollectionEditorToolbar.h"
#include "IRenderPagesEditorModule.h"
#include "IRenderPagesModule.h"

#include "BlueprintCompilationManager.h"
#include "BlueprintModes/RenderPagesApplicationModeBase.h"
#include "BlueprintModes/RenderPagesApplicationModeListing.h"
#include "BlueprintModes/RenderPagesApplicationModeLogic.h"
#include "BlueprintModes/RenderPagesApplicationModes.h"
#include "Blueprints/RenderPagesBlueprint.h"
#include "Commands/RenderPagesEditorCommands.h"
#include "EdGraphNode_Comment.h"
#include "EditorModeManager.h"
#include "Framework/Commands/GenericCommands.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Misc/MessageDialog.h"
#include "PropertyCustomizationHelpers.h"
#include "SBlueprintEditorToolbar.h"
#include "SGraphPanel.h"
#include "SKismetInspector.h"
#include "SMyBlueprint.h"
#include "Stats/StatsHierarchical.h"
#include "Styling/AppStyle.h"

#define LOCTEXT_NAMESPACE "FRenderPageCollectionEditor"


namespace UE::RenderPages::Private
{
	const FName RenderPagesEditorAppName(TEXT("RenderPagesEditorApp"));
}


void UE::RenderPages::Private::FRenderPageCollectionEditor::InitRenderPagesEditor(const EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& InitToolkitHost, URenderPagesBlueprint* InRenderPagesBlueprint)
{
	check(InRenderPagesBlueprint);

	FBlueprintCompilationManager::FlushCompilationQueue(nullptr);

	TSharedPtr<IRenderPageCollectionEditor> ThisPtr(SharedThis(this));

	if (!Toolbar.IsValid())
	{
		Toolbar = MakeShareable(new FBlueprintEditorToolbar(SharedThis(this)));
	}

	RenderPagesToolbar = MakeShared<FRenderPagesBlueprintEditorToolbar>(ThisPtr);

	// Build up a list of objects being edited in this asset editor
	TArray<UObject*> ObjectsBeingEdited;
	ObjectsBeingEdited.Add(InRenderPagesBlueprint);

	// Initialize the asset editor and spawn tabs
	constexpr bool bCreateDefaultStandaloneMenu = true;
	constexpr bool bCreateDefaultToolbar = true;
	InitAssetEditor(Mode, InitToolkitHost, RenderPagesEditorAppName, FTabManager::FLayout::NullLayout, bCreateDefaultStandaloneMenu, bCreateDefaultToolbar, ObjectsBeingEdited);

	CreateDefaultCommands();

	TArray<UBlueprint*> RenderPagesBlueprints;
	RenderPagesBlueprints.Add(InRenderPagesBlueprint);

	CommonInitialization(RenderPagesBlueprints, false);

	BindCommands();

	ExtendMenu();
	ExtendToolbar();
	RegenerateMenusAndToolbars();

	UpdateInstance(GetRenderPagesBlueprint(), true);

	constexpr bool bShouldOpenInDefaultsMode = true;
	RegisterApplicationModes(RenderPagesBlueprints, bShouldOpenInDefaultsMode, InRenderPagesBlueprint->bIsNewlyCreated);

	// Post-layout initialization
	PostLayoutBlueprintEditorInitialization();
}

UE::RenderPages::Private::FRenderPageCollectionEditor::FRenderPageCollectionEditor()
	: PreviewBlueprint(nullptr)
	, bRunRenderNewBatch(false)
	, BatchRenderJob(nullptr)
	, PreviewRenderJob(nullptr)
{}

UE::RenderPages::Private::FRenderPageCollectionEditor::~FRenderPageCollectionEditor()
{
	URenderPagesBlueprint* RenderPagesBlueprint = FRenderPageCollectionEditor::GetRenderPagesBlueprint();
	if (IsValid(RenderPagesBlueprint))
	{
		RenderPagesEditorClosedDelegate.Broadcast(this, RenderPagesBlueprint);
	}
	RenderPagesBlueprint = nullptr;

	DestroyInstance();
}

void UE::RenderPages::Private::FRenderPageCollectionEditor::CreateDefaultCommands()
{
	if (GetBlueprintObj())
	{
		FBlueprintEditor::CreateDefaultCommands();
	}
	else
	{
		ToolkitCommands->MapAction(FGenericCommands::Get().Undo, FExecuteAction::CreateSP(this, &FRenderPageCollectionEditor::UndoAction));
		ToolkitCommands->MapAction(FGenericCommands::Get().Redo, FExecuteAction::CreateSP(this, &FRenderPageCollectionEditor::RedoAction));
	}
}

UBlueprint* UE::RenderPages::Private::FRenderPageCollectionEditor::GetBlueprintObj() const
{
	for (UObject* Obj : GetEditingObjects())
	{
		if (!IsValid(Obj))
		{
			continue;
		}
		if (UBlueprint* Result = Cast<UBlueprint>(Obj))
		{
			return Result;
		}
	}
	return nullptr;
}

FGraphAppearanceInfo UE::RenderPages::Private::FRenderPageCollectionEditor::GetGraphAppearance(UEdGraph* InGraph) const
{
	FGraphAppearanceInfo AppearanceInfo = FBlueprintEditor::GetGraphAppearance(InGraph);
	if (GetBlueprintObj()->IsA(URenderPagesBlueprint::StaticClass()))
	{
		AppearanceInfo.CornerText = LOCTEXT("AppearanceCornerText_RenderPages", "RENDER PAGES");
	}
	return AppearanceInfo;
}

void UE::RenderPages::Private::FRenderPageCollectionEditor::OnActiveTabChanged(TSharedPtr<SDockTab> PreviouslyActive, TSharedPtr<SDockTab> NewlyActivated)
{
	if (!NewlyActivated.IsValid())
	{
		TArray<UObject*> ObjArray;
		Inspector->ShowDetailsForObjects(ObjArray);
	}
	else
	{
		FBlueprintEditor::OnActiveTabChanged(PreviouslyActive, NewlyActivated);
	}
}

void UE::RenderPages::Private::FRenderPageCollectionEditor::SetupGraphEditorEvents(UEdGraph* InGraph, SGraphEditor::FGraphEditorEvents& InEvents)
{
	FBlueprintEditor::SetupGraphEditorEvents(InGraph, InEvents);

	InEvents.OnCreateActionMenu = SGraphEditor::FOnCreateActionMenu::CreateSP(this, &FRenderPageCollectionEditor::HandleCreateGraphActionMenu);
}

void UE::RenderPages::Private::FRenderPageCollectionEditor::RegisterApplicationModes(const TArray<UBlueprint*>& InBlueprints, bool bShouldOpenInDefaultsMode, bool bNewlyCreated)
{
	if (InBlueprints.Num() == 1)
	{
		TSharedPtr<FRenderPageCollectionEditor> ThisPtr(SharedThis(this));

		// Create the modes and activate one (which will populate with a real layout)
		TArray<TSharedRef<FApplicationMode>> TempModeList;
		TempModeList.Add(MakeShareable(new FRenderPagesApplicationModeListing(ThisPtr)));
		TempModeList.Add(MakeShareable(new FRenderPagesApplicationModeLogic(ThisPtr)));

		for (TSharedRef<FApplicationMode>& AppMode : TempModeList)
		{
			AddApplicationMode(AppMode->GetModeName(), AppMode);
		}

		SetCurrentMode(FRenderPagesApplicationModes::ListingMode);

		// Activate our edit mode
		GetEditorModeManager().SetDefaultMode(FRenderPagesApplicationModes::ListingMode);
		GetEditorModeManager().ActivateMode(FRenderPagesApplicationModes::ListingMode);
	}
}

void UE::RenderPages::Private::FRenderPageCollectionEditor::Compile()
{
	DestroyInstance();
	FBlueprintEditor::Compile();
}

URenderPagesBlueprint* UE::RenderPages::Private::FRenderPageCollectionEditor::GetRenderPagesBlueprint() const
{
	return Cast<URenderPagesBlueprint>(GetBlueprintObj());
}

URenderPageCollection* UE::RenderPages::Private::FRenderPageCollectionEditor::GetInstance() const
{
	return RenderPageCollectionWeakPtr.Get();
}

bool UE::RenderPages::Private::FRenderPageCollectionEditor::IsBatchRendering() const
{
	return IsValid(BatchRenderJob);
}

bool UE::RenderPages::Private::FRenderPageCollectionEditor::IsPreviewRendering() const
{
	return IsValid(PreviewRenderJob);
}

void UE::RenderPages::Private::FRenderPageCollectionEditor::MarkAsModified()
{
	if (URenderPageCollection* Instance = GetInstance(); IsValid(Instance))
	{
		Instance->Modify();
	}
	if (UBlueprint* BlueprintObj = GetBlueprintObj(); IsValid(BlueprintObj))
	{
		BlueprintObj->Modify();
	}
}

TArray<URenderPage*> UE::RenderPages::Private::FRenderPageCollectionEditor::GetSelectedRenderPages() const
{
	TArray<URenderPage*> SelectedPages;
	if (URenderPageCollection* Collection = GetInstance(); IsValid(Collection))
	{
		for (URenderPage* Page : Collection->GetRenderPages())
		{
			if (SelectedRenderPagesIds.Contains(Page->GetId()))
			{
				SelectedPages.Add(Page);
			}
		}
	}
	return SelectedPages;
}

void UE::RenderPages::Private::FRenderPageCollectionEditor::SetSelectedRenderPages(const TArray<URenderPage*>& RenderPages)
{
	TSet<FGuid> PreviouslySelectedRenderPagesIds;
	PreviouslySelectedRenderPagesIds.Append(SelectedRenderPagesIds);

	SelectedRenderPagesIds.Empty();
	for (URenderPage* Page : RenderPages)
	{
		if (!IsValid(Page))
		{
			continue;
		}
		SelectedRenderPagesIds.Add(Page->GetId());
	}

	if (SelectedRenderPagesIds.Num() != PreviouslySelectedRenderPagesIds.Num())
	{
		OnRenderPagesSelectionChanged().Broadcast();
		return;
	}
	for (const FGuid& PageId : SelectedRenderPagesIds)
	{
		if (!PreviouslySelectedRenderPagesIds.Contains(PageId))
		{
			OnRenderPagesSelectionChanged().Broadcast();
			return;
		}
	}
}

FName UE::RenderPages::Private::FRenderPageCollectionEditor::GetToolkitFName() const
{
	return FName("RenderPageCollectionEditor");
}

FText UE::RenderPages::Private::FRenderPageCollectionEditor::GetBaseToolkitName() const
{
	return LOCTEXT("AppLabel", "Render Page Collection Editor");
}

FString UE::RenderPages::Private::FRenderPageCollectionEditor::GetDocumentationLink() const
{
	return FString();
}

FText UE::RenderPages::Private::FRenderPageCollectionEditor::GetToolkitToolTipText() const
{
	return GetToolTipTextForObject(GetBlueprintObj());
}

FLinearColor UE::RenderPages::Private::FRenderPageCollectionEditor::GetWorldCentricTabColorScale() const
{
	return FLinearColor(0.5f, 0.25f, 0.35f, 0.5f);
}

FString UE::RenderPages::Private::FRenderPageCollectionEditor::GetWorldCentricTabPrefix() const
{
	return LOCTEXT("WorldCentricTabPrefix", "Render Page Collection ").ToString();
}

void UE::RenderPages::Private::FRenderPageCollectionEditor::InitToolMenuContext(FToolMenuContext& MenuContext)
{
	FBlueprintEditor::InitToolMenuContext(MenuContext);
}

void UE::RenderPages::Private::FRenderPageCollectionEditor::Tick(float DeltaTime)
{
	FBlueprintEditor::Tick(DeltaTime);

	// Note: The weak ptr can become stale if the actor is reinstanced due to a Blueprint change, etc. In that case we 
	//       look to see if we can find the new instance in the preview world and then update the weak ptr.
	if (RenderPageCollectionWeakPtr.IsStale(true))
	{
		RefreshInstance();
	}

	if (bRunRenderNewBatch)
	{
		bRunRenderNewBatch = false;
		BatchRenderListAction();
	}
}

TStatId UE::RenderPages::Private::FRenderPageCollectionEditor::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(FRenderPageCollectionEditor, STATGROUP_Tickables);
}

void UE::RenderPages::Private::FRenderPageCollectionEditor::RefreshInstance()
{
	UpdateInstance(GetRenderPagesBlueprint(), true);
}

void UE::RenderPages::Private::FRenderPageCollectionEditor::OnBlueprintChangedImpl(UBlueprint* InBlueprint, bool bIsJustBeingCompiled)
{
	DestroyInstance();

	FBlueprintEditor::OnBlueprintChangedImpl(InBlueprint, bIsJustBeingCompiled);

	if (InBlueprint)
	{
		RefreshInstance();
	}
}

void UE::RenderPages::Private::FRenderPageCollectionEditor::AddReferencedObjects(FReferenceCollector& Collector)
{
	FBlueprintEditor::AddReferencedObjects(Collector);

	if (URenderPageCollection* Instance = GetInstance(); IsValid(Instance))
	{
		Collector.AddReferencedObject(Instance);
	}
}

void UE::RenderPages::Private::FRenderPageCollectionEditor::NotifyPreChange(FProperty* PropertyAboutToChange)
{
	FBlueprintEditor::NotifyPreChange(PropertyAboutToChange);

	if (URenderPagesBlueprint* RenderPagesBP = GetRenderPagesBlueprint())
	{
		RenderPagesBP->Modify();
	}
}

void UE::RenderPages::Private::FRenderPageCollectionEditor::NotifyPostChange(const FPropertyChangedEvent& PropertyChangedEvent, FProperty* PropertyThatChanged)
{
	FBlueprintEditor::NotifyPostChange(PropertyChangedEvent, PropertyThatChanged);
}

void UE::RenderPages::Private::FRenderPageCollectionEditor::OnFinishedChangingProperties(const FPropertyChangedEvent& PropertyChangedEvent) {}

void UE::RenderPages::Private::FRenderPageCollectionEditor::BindCommands()
{
	const auto& Commands = FRenderPagesEditorCommands::Get();

	ToolkitCommands->MapAction(Commands.AddPage, FExecuteAction::CreateSP(this, &FRenderPageCollectionEditor::AddPageAction));
	ToolkitCommands->MapAction(Commands.CopyPage, FExecuteAction::CreateSP(this, &FRenderPageCollectionEditor::CopyPageAction));
	ToolkitCommands->MapAction(Commands.DeletePage, FExecuteAction::CreateSP(this, &FRenderPageCollectionEditor::DeletePageAction));
	ToolkitCommands->MapAction(Commands.BatchRenderList, FExecuteAction::CreateSP(this, &FRenderPageCollectionEditor::BatchRenderListAction));
}

void UE::RenderPages::Private::FRenderPageCollectionEditor::AddPageAction()
{
	if (URenderPageCollection* Instance = GetInstance(); IsValid(Instance))
	{
		if (URenderPage* Page = Instance->CreateAndAddNewRenderPage(); IsValid(Page))
		{
			OnRenderPageCreated().Broadcast(Page);
			MarkAsModified();
			OnRenderPagesChanged().Broadcast();
		}
	}
}

void UE::RenderPages::Private::FRenderPageCollectionEditor::CopyPageAction()
{
	TArray<URenderPage*> SelectedRenderPages = GetSelectedRenderPages();
	if (SelectedRenderPages.Num() <= 0)
	{
		return;
	}

	if (URenderPageCollection* Instance = GetInstance(); IsValid(Instance))
	{
		for (URenderPage* SelectedRenderPage : SelectedRenderPages)
		{
			Instance->DuplicateAndAddRenderPage(SelectedRenderPage);
		}
		MarkAsModified();
		OnRenderPagesChanged().Broadcast();
	}
}

void UE::RenderPages::Private::FRenderPageCollectionEditor::DeletePageAction()
{
	TArray<URenderPage*> SelectedRenderPages = GetSelectedRenderPages();
	if (SelectedRenderPages.Num() <= 0)
	{
		return;
	}

	const FText TitleText = LOCTEXT("ConfirmToDeleteTitle", "Confirm To Delete");
	const EAppReturnType::Type DialogResult = FMessageDialog::Open(
		EAppMsgType::OkCancel,
		((SelectedRenderPages.Num() == 1) ? LOCTEXT("ConfirmToDeleteSingleText", "Are you sure you want to delete the selected render page?") : LOCTEXT("ConfirmToDeleteMultipleText", "Are you sure you want to delete the selected render pages?")),
		&TitleText);

	if (DialogResult != EAppReturnType::Ok)
	{
		return;
	}

	if (URenderPageCollection* Instance = GetInstance(); IsValid(Instance))
	{
		for (URenderPage* SelectedRenderPage : SelectedRenderPages)
		{
			Instance->RemoveRenderPage(SelectedRenderPage);
		}

		MarkAsModified();
		OnRenderPagesChanged().Broadcast();
	}
}

void UE::RenderPages::Private::FRenderPageCollectionEditor::BatchRenderListAction()
{
	if (!CanCurrentlyRender())
	{
		bRunRenderNewBatch = true;
		return;
	}

	if (URenderPageCollection* Instance = GetInstance(); IsValid(Instance))
	{
		if (Instance->GetRenderPages().Num() <= 0)
		{
			const FText TitleText = LOCTEXT("NoPagesToRenderTitle", "No Pages To Render");
			FMessageDialog::Open(
				EAppMsgType::Ok,
				LOCTEXT("NoPagesToRenderText", "There are no render pages in this collection, and so nothing can be rendered. Please make a page and try again."),
				&TitleText);
			return;
		}

		if (URenderPagesMoviePipelineRenderJob* RenderJob = IRenderPagesModule::Get().GetManager().CreateBatchRenderJob(Instance); IsValid(RenderJob))
		{
			RenderJob->OnExecuteFinished().AddRaw(this, &FRenderPageCollectionEditor::OnBatchRenderListActionFinished);
			BatchRenderJob = RenderJob;
			OnRenderPagesBatchRenderingStarted().Broadcast(RenderJob);
			RenderJob->Execute();
		}
	}
}

void UE::RenderPages::Private::FRenderPageCollectionEditor::OnBatchRenderListActionFinished(URenderPagesMoviePipelineRenderJob* RenderJob, bool bSuccess)
{
	URenderPagesMoviePipelineRenderJob* FinishedRenderJob = BatchRenderJob;
	BatchRenderJob = nullptr;
	OnRenderPagesBatchRenderingFinished().Broadcast(FinishedRenderJob);
}

void UE::RenderPages::Private::FRenderPageCollectionEditor::UndoAction()
{
	GEditor->UndoTransaction();
}

void UE::RenderPages::Private::FRenderPageCollectionEditor::RedoAction()
{
	GEditor->RedoTransaction();
}

void UE::RenderPages::Private::FRenderPageCollectionEditor::ExtendMenu()
{
	if (MenuExtender.IsValid())
	{
		RemoveMenuExtender(MenuExtender);
		MenuExtender.Reset();
	}

	MenuExtender = MakeShareable(new FExtender);

	AddMenuExtender(MenuExtender);

	// add extensible menu if exists
	IRenderPagesEditorModule& RenderPagesEditorModule = IRenderPagesEditorModule::Get();
	AddMenuExtender(RenderPagesEditorModule.GetMenuExtensibilityManager()->GetAllExtenders(GetToolkitCommands(), GetEditingObjects()));
}

void UE::RenderPages::Private::FRenderPageCollectionEditor::ExtendToolbar()
{
	// If the ToolbarExtender is valid, remove it before rebuilding it
	if (ToolbarExtender.IsValid())
	{
		RemoveToolbarExtender(ToolbarExtender);
		ToolbarExtender.Reset();
	}

	ToolbarExtender = MakeShareable(new FExtender);

	AddToolbarExtender(ToolbarExtender);

	IRenderPagesEditorModule& RenderPagesEditorModule = IRenderPagesEditorModule::Get();
	AddToolbarExtender(RenderPagesEditorModule.GetToolBarExtensibilityManager()->GetAllExtenders(GetToolkitCommands(), GetEditingObjects()));

	ToolbarExtender->AddToolBarExtension(
		"Asset",
		EExtensionHook::After,
		GetToolkitCommands(),
		FToolBarExtensionDelegate::CreateSP(this, &FRenderPageCollectionEditor::FillToolbar)
	);
}

void UE::RenderPages::Private::FRenderPageCollectionEditor::FillToolbar(FToolBarBuilder& ToolbarBuilder)
{
	ToolbarBuilder.BeginSection("Common");
	{
		ToolbarBuilder.AddToolBarButton(FRenderPagesEditorCommands::Get().BatchRenderList, NAME_None, TAttribute<FText>(), TAttribute<FText>(), FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.OpenCinematic"));
	}
	ToolbarBuilder.EndSection();
}

void UE::RenderPages::Private::FRenderPageCollectionEditor::DestroyInstance()
{
	if (URenderPageCollection* Instance = GetInstance(); IsValid(Instance))
	{
		Instance->OnClose();
		RenderPageCollectionWeakPtr.Reset();
		Instance->MarkAsGarbage();
	}
}

void UE::RenderPages::Private::FRenderPageCollectionEditor::UpdateInstance(UBlueprint* InBlueprint, bool bInForceFullUpdate)
{
	// If the Blueprint is changing
	if ((InBlueprint != PreviewBlueprint.Get()) || bInForceFullUpdate)
	{
		// Destroy the previous instance
		DestroyInstance();

		// Save the Blueprint we're creating a preview for
		PreviewBlueprint = Cast<URenderPagesBlueprint>(InBlueprint);

		URenderPageCollection* RenderPageCollection;
		// Create the Widget, we have to do special swapping out of the widget tree.
		{
			// Assign the outer to the game instance if it exists, otherwise use the world
			{
				FMakeClassSpawnableOnScope TemporarilySpawnable(PreviewBlueprint->GeneratedClass);
				RenderPageCollection = NewObject<URenderPageCollection>(PreviewScene.GetWorld(), PreviewBlueprint->GeneratedClass);
			}
		}

		// Make sure the object being debugged is cleared out
		GetBlueprintObj()->SetObjectBeingDebugged(nullptr);

		// Store a reference to the preview actor.
		RenderPageCollectionWeakPtr = RenderPageCollection;

		// Broadcast the events
		OnRenderPagesChanged().Broadcast();
		OnRenderPagesSelectionChanged().Broadcast();
	}
}

FActionMenuContent UE::RenderPages::Private::FRenderPageCollectionEditor::HandleCreateGraphActionMenu(UEdGraph* InGraph, const FVector2D& InNodePosition, const TArray<UEdGraphPin*>& InDraggedPins, bool bAutoExpand, SGraphEditor::FActionMenuClosed InOnMenuClosed)
{
	return OnCreateGraphActionMenu(InGraph, InNodePosition, InDraggedPins, bAutoExpand, InOnMenuClosed);
}


#undef LOCTEXT_NAMESPACE
