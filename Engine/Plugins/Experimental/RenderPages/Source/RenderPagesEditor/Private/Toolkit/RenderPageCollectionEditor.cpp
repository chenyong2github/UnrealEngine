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
#include "Graph/RenderPagesGraph.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Misc/MessageDialog.h"
#include "PropertyCustomizationHelpers.h"
#include "SBlueprintEditorToolbar.h"
#include "SGraphPanel.h"
#include "SKismetInspector.h"
#include "SMyBlueprint.h"
#include "SNodePanel.h"
#include "Stats/StatsHierarchical.h"
#include "Styling/AppStyle.h"

#define LOCTEXT_NAMESPACE "FRenderPageCollectionEditor"


namespace UE::RenderPages::Private
{
	const FName RenderPagesEditorAppName(TEXT("RenderPagesEditorApp"));

	struct FRenderPagesZoomLevelsContainer : FZoomLevelsContainer
	{
		struct FRenderPagesZoomLevelEntry
		{
		public:
			FRenderPagesZoomLevelEntry(const float InZoomAmount, const FText& InDisplayText, const EGraphRenderingLOD::Type InLOD)
				: DisplayText(FText::Format(NSLOCTEXT("GraphEditor", "Zoom", "Zoom {0}"), InDisplayText))
				, ZoomAmount(InZoomAmount)
				, LOD(InLOD)
			{}

		public:
			FText DisplayText;
			float ZoomAmount;
			EGraphRenderingLOD::Type LOD;
		};

		FRenderPagesZoomLevelsContainer()
		{
			ZoomLevels.Reserve(22);
			ZoomLevels.Add(FRenderPagesZoomLevelEntry(0.025f, FText::FromString(TEXT("-14")), EGraphRenderingLOD::LowestDetail));
			ZoomLevels.Add(FRenderPagesZoomLevelEntry(0.070f, FText::FromString(TEXT("-13")), EGraphRenderingLOD::LowestDetail));
			ZoomLevels.Add(FRenderPagesZoomLevelEntry(0.100f, FText::FromString(TEXT("-12")), EGraphRenderingLOD::LowestDetail));
			ZoomLevels.Add(FRenderPagesZoomLevelEntry(0.125f, FText::FromString(TEXT("-11")), EGraphRenderingLOD::LowestDetail));
			ZoomLevels.Add(FRenderPagesZoomLevelEntry(0.150f, FText::FromString(TEXT("-10")), EGraphRenderingLOD::LowestDetail));
			ZoomLevels.Add(FRenderPagesZoomLevelEntry(0.175f, FText::FromString(TEXT("-9")), EGraphRenderingLOD::LowestDetail));
			ZoomLevels.Add(FRenderPagesZoomLevelEntry(0.200f, FText::FromString(TEXT("-8")), EGraphRenderingLOD::LowestDetail));
			ZoomLevels.Add(FRenderPagesZoomLevelEntry(0.225f, FText::FromString(TEXT("-7")), EGraphRenderingLOD::LowDetail));
			ZoomLevels.Add(FRenderPagesZoomLevelEntry(0.250f, FText::FromString(TEXT("-6")), EGraphRenderingLOD::LowDetail));
			ZoomLevels.Add(FRenderPagesZoomLevelEntry(0.375f, FText::FromString(TEXT("-5")), EGraphRenderingLOD::MediumDetail));
			ZoomLevels.Add(FRenderPagesZoomLevelEntry(0.500f, FText::FromString(TEXT("-4")), EGraphRenderingLOD::MediumDetail));
			ZoomLevels.Add(FRenderPagesZoomLevelEntry(0.675f, FText::FromString(TEXT("-3")), EGraphRenderingLOD::MediumDetail));
			ZoomLevels.Add(FRenderPagesZoomLevelEntry(0.750f, FText::FromString(TEXT("-2")), EGraphRenderingLOD::DefaultDetail));
			ZoomLevels.Add(FRenderPagesZoomLevelEntry(0.875f, FText::FromString(TEXT("-1")), EGraphRenderingLOD::DefaultDetail));
			ZoomLevels.Add(FRenderPagesZoomLevelEntry(1.000f, FText::FromString(TEXT("1:1")), EGraphRenderingLOD::DefaultDetail));
			ZoomLevels.Add(FRenderPagesZoomLevelEntry(1.250f, FText::FromString(TEXT("+1")), EGraphRenderingLOD::DefaultDetail));
			ZoomLevels.Add(FRenderPagesZoomLevelEntry(1.375f, FText::FromString(TEXT("+2")), EGraphRenderingLOD::DefaultDetail));
			ZoomLevels.Add(FRenderPagesZoomLevelEntry(1.500f, FText::FromString(TEXT("+3")), EGraphRenderingLOD::FullyZoomedIn));
			ZoomLevels.Add(FRenderPagesZoomLevelEntry(1.675f, FText::FromString(TEXT("+4")), EGraphRenderingLOD::FullyZoomedIn));
			ZoomLevels.Add(FRenderPagesZoomLevelEntry(1.750f, FText::FromString(TEXT("+5")), EGraphRenderingLOD::FullyZoomedIn));
			ZoomLevels.Add(FRenderPagesZoomLevelEntry(1.875f, FText::FromString(TEXT("+6")), EGraphRenderingLOD::FullyZoomedIn));
			ZoomLevels.Add(FRenderPagesZoomLevelEntry(2.000f, FText::FromString(TEXT("+7")), EGraphRenderingLOD::FullyZoomedIn));
		}

		virtual float GetZoomAmount(int32 InZoomLevel) const override
		{
			checkSlow(ZoomLevels.IsValidIndex(InZoomLevel));
			return ZoomLevels[InZoomLevel].ZoomAmount;
		}

		virtual int32 GetNearestZoomLevel(float InZoomAmount) const override
		{
			for (int32 ZoomLevelIndex = 0; ZoomLevelIndex < GetNumZoomLevels(); ++ZoomLevelIndex)
			{
				if (InZoomAmount <= GetZoomAmount(ZoomLevelIndex))
				{
					return ZoomLevelIndex;
				}
			}

			return GetDefaultZoomLevel();
		}

		virtual FText GetZoomText(int32 InZoomLevel) const override
		{
			checkSlow(ZoomLevels.IsValidIndex(InZoomLevel));
			return ZoomLevels[InZoomLevel].DisplayText;
		}

		virtual int32 GetNumZoomLevels() const override
		{
			return ZoomLevels.Num();
		}

		virtual int32 GetDefaultZoomLevel() const override
		{
			return 14;
		}

		virtual EGraphRenderingLOD::Type GetLOD(int32 InZoomLevel) const override
		{
			checkSlow(ZoomLevels.IsValidIndex(InZoomLevel));
			return ZoomLevels[InZoomLevel].LOD;
		}

		TArray<FRenderPagesZoomLevelEntry> ZoomLevels;
	};
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

	{
		TArray<UEdGraph*> EdGraphs;
		InRenderPagesBlueprint->GetAllGraphs(EdGraphs);

		for (UEdGraph* Graph : EdGraphs)
		{
			if (URenderPagesGraph* RenderPagesGraph = Cast<URenderPagesGraph>(Graph))
			{
				RenderPagesGraph->Initialize(InRenderPagesBlueprint);
			}
		}
	}

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

	RenderPagesEditorClosedDelegate.Broadcast(this, RenderPagesBlueprint);

	if (RenderPagesBlueprint)
	{
		// clear editor related data from the debugged control rig instance 
		RenderPagesBlueprint->SetObjectBeingDebugged(nullptr);
		RenderPagesBlueprint = nullptr;
	}
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

TSharedRef<SGraphEditor> UE::RenderPages::Private::FRenderPageCollectionEditor::CreateGraphEditorWidget(TSharedRef<FTabInfo> InTabInfo, UEdGraph* InGraph)
{
	TSharedRef<SGraphEditor> GraphEditor = FBlueprintEditor::CreateGraphEditorWidget(InTabInfo, InGraph);

	GraphEditor->GetGraphPanel()->SetZoomLevelsContainer<FRenderPagesZoomLevelsContainer>();

	return GraphEditor;
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

bool UE::RenderPages::Private::FRenderPageCollectionEditor::IsEditable(UEdGraph* InGraph) const
{
	return IsGraphInCurrentBlueprint(InGraph);
}

bool UE::RenderPages::Private::FRenderPageCollectionEditor::IsCompilingEnabled() const
{
	return true;
}

bool UE::RenderPages::Private::FRenderPageCollectionEditor::IsSectionVisible(NodeSectionID::Type InSectionID) const
{
	switch (InSectionID)
	{
		case NodeSectionID::GRAPH:
		case NodeSectionID::VARIABLE:
		case NodeSectionID::FUNCTION:
		{
			return true;
		}
		case NodeSectionID::LOCAL_VARIABLE:
		{
			return IsValid(Cast<URenderPagesGraph>(GetFocusedGraph()));
		}
		default:
		{
			break;
		}
	}
	return false;
}

FText UE::RenderPages::Private::FRenderPageCollectionEditor::GetGraphDecorationString(UEdGraph* InGraph) const
{
	return FText::GetEmpty();
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

void UE::RenderPages::Private::FRenderPageCollectionEditor::FocusInspectorOnGraphSelection(const TSet<UObject*>& NewSelection, bool bForceRefresh)
{
	// nothing to do here for render pages
}

bool UE::RenderPages::Private::FRenderPageCollectionEditor::CanAddNewLocalVariable() const
{
	return (Cast<URenderPagesGraph>(GetFocusedGraph()) != nullptr);
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
	return BatchRenderJob && IsValid(BatchRenderJob);
}

bool UE::RenderPages::Private::FRenderPageCollectionEditor::IsPreviewRendering() const
{
	return PreviewRenderJob && IsValid(PreviewRenderJob);
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

	URenderPageCollection* RenderPageCollection = GetInstance();
	Collector.AddReferencedObject(RenderPageCollection);
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
	ToolkitCommands->MapAction(Commands.AutoCompileGraph, FExecuteAction::CreateSP(this, &FRenderPageCollectionEditor::AutoCompileGraphAction));
}

void UE::RenderPages::Private::FRenderPageCollectionEditor::AddPageAction()
{
	URenderPage* Page = IRenderPagesModule::Get().GetManager().AddNewPage(GetInstance());
	if (!IsValid(Page))
	{
		return;
	}

	OnRenderPageCreated().Broadcast(Page);
	MarkAsModified();
	OnRenderPagesChanged().Broadcast();
}

void UE::RenderPages::Private::FRenderPageCollectionEditor::CopyPageAction()
{
	TArray<URenderPage*> SelectedRenderPages = GetSelectedRenderPages();
	if (SelectedRenderPages.Num() <= 0)
	{
		return;
	}

	URenderPageCollection* RenderPageCollection = GetInstance();
	for (URenderPage* SelectedRenderPage : SelectedRenderPages)
	{
		IRenderPagesModule::Get().GetManager().CopyPage(RenderPageCollection, SelectedRenderPage);
	}

	MarkAsModified();
	OnRenderPagesChanged().Broadcast();
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

	URenderPageCollection* RenderPageCollection = GetInstance();
	for (URenderPage* SelectedRenderPage : SelectedRenderPages)
	{
		IRenderPagesModule::Get().GetManager().DeletePage(RenderPageCollection, SelectedRenderPage);
	}

	MarkAsModified();
	OnRenderPagesChanged().Broadcast();
}

void UE::RenderPages::Private::FRenderPageCollectionEditor::BatchRenderListAction()
{
	if (!CanCurrentlyRender())
	{
		bRunRenderNewBatch = true;
		return;
	}

	URenderPageCollection* PageCollection = GetInstance();
	if (PageCollection->GetRenderPages().Num() <= 0)
	{
		const FText TitleText = LOCTEXT("NoPagesToRenderTitle", "No Pages To Render");
		FMessageDialog::Open(
			EAppMsgType::Ok,
			LOCTEXT("NoPagesToRenderText", "There are no render pages in this collection, and so nothing can be rendered. Please make a page and try again."),
			&TitleText);
		return;
	}

	URenderPagesMoviePipelineRenderJob* RenderJob = IRenderPagesModule::Get().GetManager().CreateBatchRenderJob(PageCollection);
	if (!RenderJob)
	{
		return;
	}
	RenderJob->OnExecuteFinished().AddRaw(this, &FRenderPageCollectionEditor::OnBatchRenderListActionFinished);
	BatchRenderJob = RenderJob;
	OnRenderPagesBatchRenderingStarted().Broadcast(RenderJob);
	RenderJob->Execute();
}

void UE::RenderPages::Private::FRenderPageCollectionEditor::OnBatchRenderListActionFinished(URenderPagesMoviePipelineRenderJob* RenderJob, bool bSuccess)
{
	URenderPagesMoviePipelineRenderJob* FinishedRenderJob = BatchRenderJob;
	BatchRenderJob = nullptr;
	OnRenderPagesBatchRenderingFinished().Broadcast(FinishedRenderJob);
}

void UE::RenderPages::Private::FRenderPageCollectionEditor::AutoCompileGraphAction() {}

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
	if (URenderPageCollection* RenderPageCollection = GetInstance())
	{
		RenderPageCollection->OnClose();
		RenderPageCollectionWeakPtr.Reset();
		RenderPageCollection->MarkAsGarbage();
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

		// Make sure the object being debugged is the preview instance
		GetBlueprintObj()->SetObjectBeingDebugged(RenderPageCollection);

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
