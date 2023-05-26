// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_STATETREE_DEBUGGER

#include "SStateTreeDebuggerView.h"
#include "Debugger/StateTreeDebugger.h"
#include "DetailsViewArgs.h"
#include "Editor.h"
#include "Factories.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "IStructureDetailsView.h"
#include "Kismet2/DebuggerCommands.h"
#include "Modules/ModuleManager.h"
#include "ProfilingDebugging/TraceAuxiliary.h"
#include "PropertyEditorModule.h"
#include "SStateTreeDebuggerInstanceTree.h"
#include "SStateTreeDebuggerTimelines.h"
#include "StateTree.h"
#include "StateTreeDebuggerCommands.h"
#include "StateTreeDebuggerTrack.h"
#include "StateTreeEditorSettings.h"
#include "StateTreeExecutionContext.h"
#include "StateTreeModule.h"
#include "StateTreeState.h"
#include "StateTreeViewModel.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSpacer.h"

#define LOCTEXT_NAMESPACE "StateTreeEditor"


namespace UE::StateTreeDebugger
{
//----------------------------------------------------------------------//
// FTraceTextObjectFactory
//----------------------------------------------------------------------//
struct FTraceTextObjectFactory : FCustomizableTextObjectFactory
{
	UObject* NodeInstanceObject = nullptr;
	FTraceTextObjectFactory() : FCustomizableTextObjectFactory(GWarn) {}

protected:
	virtual bool CanCreateClass(UClass* ObjectClass, bool& bOmitSubObjs) const override
	{
		return true;
	}

	virtual void ProcessConstructedObject(UObject* CreatedObject) override
	{
		NodeInstanceObject = CreatedObject;
	}
};


//----------------------------------------------------------------------//
// FEventTreeElement
//----------------------------------------------------------------------//
/** An item in the trace event tree */
struct FEventTreeElement : TSharedFromThis<FEventTreeElement>
{
	explicit FEventTreeElement(const TraceServices::FFrame& Frame, const FStateTreeTraceEventVariantType& Event)
		: Frame(Frame), Event(Event)
	{
	}

	TraceServices::FFrame Frame;
	FStateTreeTraceEventVariantType Event;
	TArray<TSharedPtr<FEventTreeElement>> Children;
};
} // UE::StateTreeDebugger


//----------------------------------------------------------------------//
// SStateTreeDebuggerTableRow
//----------------------------------------------------------------------//
class SStateTreeDebuggerTableRow : public SMultiColumnTableRow<TSharedPtr<UE::StateTreeDebugger::FEventTreeElement>>
{
public:
	void Construct(const FArguments& InArgs,
		const TSharedPtr<STableViewBase>& InOwnerTableView,
		const TSharedPtr<UE::StateTreeDebugger::FEventTreeElement>& InElement,
		const TSharedRef<FStateTreeViewModel>& InStateTreeViewModel)
	{
		Item = InElement;
		StateTreeViewModel = InStateTreeViewModel.ToSharedPtr();
		SMultiColumnTableRow::Construct(InArgs, InOwnerTableView.ToSharedRef());
	}

protected:
	// SMultiColumnTableRow overrides
	virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& ColumnName) override
	{
		const TSharedPtr<SHorizontalBox> Contents = SNew(SHorizontalBox);

		Contents->AddSlot()
			.VAlign(VAlign_Fill)
			.HAlign(HAlign_Left)
			.AutoWidth()
			[
				SNew(SExpanderArrow, SharedThis(this))
				.ShouldDrawWires(true)
				.IndentAmount(32)
				.BaseIndentLevel(0)
			];
		
		if (ColumnName == FName("Desc"))
		{
			Contents->AddSlot()
				.Padding(5, 0)
				.FillWidth(1.f)
				[
					SNew(STextBlock)
					.Font(FCoreStyle::GetDefaultFontStyle("Mono", 9))
					.Text_Lambda([&Event=Item->Event, this]()
					{
						FString EventDescription;
						if (const UStateTree* StateTree = StateTreeViewModel->GetStateTree())
						{
							Visit([&EventDescription, StateTree](auto& TypedEvent)
								{
									EventDescription = TypedEvent.ToString(*StateTree);
								}, Event);
						}
						return FText::FromString(EventDescription);
					})
				];
		}

		return Contents.ToSharedRef();
	}

	TSharedPtr<FStateTreeViewModel> StateTreeViewModel;
	TSharedPtr<UE::StateTreeDebugger::FEventTreeElement> Item;
};


//----------------------------------------------------------------------//
// SStateTreeDebuggerView
//----------------------------------------------------------------------//
SStateTreeDebuggerView::SStateTreeDebuggerView()
{
	FEditorDelegates::BeginPIE.AddRaw(this, &SStateTreeDebuggerView::OnPIEStarted);
	FEditorDelegates::EndPIE.AddRaw(this, &SStateTreeDebuggerView::OnPIEStopped);
	FEditorDelegates::PausePIE.AddRaw(this, &SStateTreeDebuggerView::OnPIEPaused);
	FEditorDelegates::ResumePIE.AddRaw(this, &SStateTreeDebuggerView::OnPIEResumed);
	FEditorDelegates::SingleStepPIE.AddRaw(this, &SStateTreeDebuggerView::OnPIESingleStepped);
}

SStateTreeDebuggerView::~SStateTreeDebuggerView()
{
	if (SelectedNodeDataObject.IsValid())
	{
		SelectedNodeDataObject->RemoveFromRoot();
	}

	if (Debugger)
	{
		Debugger->OnScrubStateChanged.Unbind();
		Debugger->OnBreakpointHit.Unbind();
		Debugger->OnNewInstance.Unbind();
		Debugger->OnSelectedInstanceCleared.Unbind();
	}

	FEditorDelegates::BeginPIE.RemoveAll(this);
	FEditorDelegates::EndPIE.RemoveAll(this);
	FEditorDelegates::PausePIE.RemoveAll(this);
	FEditorDelegates::ResumePIE.RemoveAll(this);
	FEditorDelegates::SingleStepPIE.RemoveAll(this);
}

void SStateTreeDebuggerView::StartRecording()
{
	if (!CanStartRecording())
	{
		return;
	}

	IStateTreeModule& StateTreeModule = FModuleManager::GetModuleChecked<IStateTreeModule>("StateTreeModule");
	StateTreeModule.StartTraces();

	bRecording = true;
}

void SStateTreeDebuggerView::StopRecording()
{
	if (bRecording)
	{
		IStateTreeModule& StateTreeModule = FModuleManager::GetModuleChecked<IStateTreeModule>("StateTreeModule");
		StateTreeModule.StopTraces();
		
		bRecording = false;

		// Mark all tracks from the stopped session as stale to have different look.
		for (const TSharedPtr<RewindDebugger::FRewindDebuggerTrack>& DebugTrack : InstanceTracks)
		{
			if (FStateTreeDebuggerTrack* StateTreeTrack = static_cast<FStateTreeDebuggerTrack*>(DebugTrack.Get()))
			{
				StateTreeTrack->MarkAsStale();
			}
		}
	}
}

void SStateTreeDebuggerView::OnPIEStarted(const bool bIsSimulating)
{
	if (UStateTreeEditorSettings::Get().bShouldDebuggerAutoRecordOnPIE)
	{
		StartRecording();

		if (!Debugger->IsAnalysisSessionActive())
		{
			Debugger->StartLastLiveSessionAnalysis();
		}
	}
}

void SStateTreeDebuggerView::OnPIEStopped(const bool bIsSimulating)
{
	StopRecording();
	Debugger->StopAnalysis();
	Debugger->Unpause();
}

void SStateTreeDebuggerView::OnPIEPaused(const bool bIsSimulating) const
{
	Debugger->Pause();
}

void SStateTreeDebuggerView::OnPIEResumed(const bool bIsSimulating) const
{
	Debugger->Unpause();
}

void SStateTreeDebuggerView::OnPIESingleStepped(bool bSimulating) const
{
	Debugger->SyncToCurrentSessionDuration();
}

void SStateTreeDebuggerView::Construct(const FArguments& InArgs, const UStateTree* InStateTree, const TSharedRef<FStateTreeViewModel>& InStateTreeViewModel, const TSharedRef<FUICommandList>& InCommandList)
{
	StateTreeViewModel = InStateTreeViewModel;
	StateTree = InStateTree;

	Debugger = InStateTreeViewModel->GetDebugger();
	bRecording = FTraceAuxiliary::IsConnected();

	// Bind callbacks to the debugger delegates
	Debugger->OnNewInstance.BindSP(this, &SStateTreeDebuggerView::OnNewInstance);
	Debugger->OnScrubStateChanged.BindSP(this, &SStateTreeDebuggerView::OnDebuggerScrubStateChanged);
	Debugger->OnBreakpointHit.BindSP(this, &SStateTreeDebuggerView::OnBreakpointHit, InCommandList);
	Debugger->OnSelectedInstanceCleared.BindSP(this, &SStateTreeDebuggerView::OnSelectedInstanceCleared);

	// Bind our scrub time attribute to follow the value computed by the debugger
	ScrubTimeAttribute = TAttribute<double>(InStateTreeViewModel->GetDebugger(), &FStateTreeDebugger::GetScrubTime);

	// Put debugger in proper simulation state when view is constructed after PIE/SIE was started
	if (FPlayWorldCommandCallbacks::HasPlayWorldAndPaused())
	{
		Debugger->Pause();
	}

	// Add & Bind commands
	BindDebuggerToolbarCommands(InCommandList);

	// Register the play world commands
	InCommandList->Append(FPlayWorldCommands::GlobalPlayWorldActions.ToSharedRef());

	InCommandList->MapAction(
		FStateTreeDebuggerCommands::Get().ToggleBreakpoint,
		FExecuteAction::CreateSP(this, &SStateTreeDebuggerView::ToggleBreakpoint),
		FCanExecuteAction::CreateSP(this, &SStateTreeDebuggerView::CanToggleBreakpoint)
	);
	
	// Toolbar
	FSlimHorizontalToolBarBuilder ToolbarBuilder(InCommandList, FMultiBoxCustomization::None, /*InExtender*/ nullptr, /*InForceSmallIcons*/ true);
	ToolbarBuilder.BeginSection(TEXT("Debugging"));
	{
		const FStateTreeDebuggerCommands& DebuggerCommands = FStateTreeDebuggerCommands::Get();
		ToolbarBuilder.AddToolBarButton(DebuggerCommands.StartRecording);
		ToolbarBuilder.AddToolBarButton(DebuggerCommands.StopRecording);

		const FPlayWorldCommands& PlayWorldCommands = FPlayWorldCommands::Get();
		ToolbarBuilder.AddToolBarButton(PlayWorldCommands.RepeatLastPlay);
		ToolbarBuilder.AddToolBarButton(PlayWorldCommands.PausePlaySession,
					NAME_None, TAttribute<FText>(), TAttribute<FText>(), FSlateIcon(FAppStyle::GetAppStyleSetName(), "PlayWorld.PausePlaySession.Small"));
		ToolbarBuilder.AddToolBarButton(PlayWorldCommands.ResumePlaySession,
					NAME_None, TAttribute<FText>(), TAttribute<FText>(), FSlateIcon(FAppStyle::GetAppStyleSetName(), "PlayWorld.ResumePlaySession.Small"));	
		ToolbarBuilder.AddToolBarButton(PlayWorldCommands.StopPlaySession,
					NAME_None, TAttribute<FText>(), TAttribute<FText>(), FSlateIcon(FAppStyle::GetAppStyleSetName(), "PlayWorld.StopPlaySession.Small"));
		ToolbarBuilder.AddSeparator();
		ToolbarBuilder.AddToolBarButton(DebuggerCommands.PreviousFrameWithStateChange);
		ToolbarBuilder.AddToolBarButton(DebuggerCommands.PreviousFrameWithEvents);
		ToolbarBuilder.AddToolBarButton(DebuggerCommands.NextFrameWithEvents);
		ToolbarBuilder.AddToolBarButton(DebuggerCommands.NextFrameWithStateChange);
	}
	ToolbarBuilder.EndSection();

	// Auto-select session if there is only one available
	TArray<FStateTreeDebugger::FTraceDescriptor> TraceDescriptors;
	Debugger->GetLiveTraces(TraceDescriptors);
	
	if (TraceDescriptors.Num() == 1)
	{
		Debugger->StartSessionAnalysis(TraceDescriptors.Last());
	}

	// Trace selection combo
	const TSharedRef<SWidget> TraceSelectionBox = SNew(SComboButton)
		.OnGetMenuContent(this, &SStateTreeDebuggerView::OnGetDebuggerTracesMenu)
		.ButtonContent()
		[
			SNew(STextBlock)
			.ToolTipText(LOCTEXT("SelectTraceSession", "Pick trace session to debug"))
			.Text_Lambda([Debugger = Debugger]()
			{
				return Debugger.IsValid() ? Debugger->GetSelectedTraceDescription() : FText::GetEmpty();
			})
		];

	// Instances TreeView
	InstancesTreeView =	SNew(SStateTreeDebuggerInstanceTree)
		.OnExpansionChanged_Lambda([this]() { InstanceTimelinesTreeView->RestoreExpansion(); })
		.OnScrolled_Lambda([this](double ScrollOffset)
		{
			InstanceTimelinesTreeView->ScrollTo(ScrollOffset);
		})
		.InstanceTracks(&InstanceTracks)
		.OnSelectionChanged_Lambda([this](TSharedPtr<RewindDebugger::FRewindDebuggerTrack> SelectedItem, ESelectInfo::Type SelectInfo)
			{
				InstanceTimelinesTreeView->SetSelection(SelectedItem);

				const FStateTreeDebuggerTrack* StateTreeTrack = static_cast<FStateTreeDebuggerTrack*>(SelectedItem.Get());
				Debugger->SelectInstance(StateTreeTrack != nullptr ? StateTreeTrack->GetInstanceId() : FStateTreeInstanceDebugId::Invalid);
			});
	
	// Timelines TreeView
	InstanceTimelinesTreeView = SNew(SStateTreeDebuggerTimelines)
		.OnExpansionChanged_Lambda([this]() { InstancesTreeView->RestoreExpansion(); })
		.OnScrolled_Lambda([this](double ScrollOffset) { InstancesTreeView->ScrollTo(ScrollOffset); })
		.InstanceTracks(&InstanceTracks)
		.ViewRange_Lambda([this]() { return ViewRange; })
		.ClampRange_Lambda([this]() { return TRange<double>(0, Debugger->GetRecordingDuration()); })
		.OnViewRangeChanged_Lambda([this](TRange<double> NewRange) { ViewRange = NewRange; })
		.ScrubPosition(ScrubTimeAttribute)
		.OnScrubPositionChanged_Lambda([this](double NewScrubTime, bool bIsScrubbing) { OnTimeLineScrubPositionChanged(NewScrubTime, bIsScrubbing); });

	// EventsTreeView
	EventsTreeView = SNew(STreeView<TSharedPtr<UE::StateTreeDebugger::FEventTreeElement>>)
		.HeaderRow(SNew(SHeaderRow)
			+SHeaderRow::Column("Desc")
			.DefaultLabel(LOCTEXT("FrameDetailsColumnHeader", "Frame Details")))
			.OnGenerateRow_Lambda([this](const TSharedPtr<UE::StateTreeDebugger::FEventTreeElement>& InElement, const TSharedRef<STableViewBase>& InOwnerTableView)
			{
				return SNew(SStateTreeDebuggerTableRow, InOwnerTableView, InElement, StateTreeViewModel.ToSharedRef());
			})
			.OnGetChildren_Lambda([](const TSharedPtr<const UE::StateTreeDebugger::FEventTreeElement>& InParent, TArray<TSharedPtr<UE::StateTreeDebugger::FEventTreeElement>>& OutChildren)
			{
				if (const UE::StateTreeDebugger::FEventTreeElement* Parent = InParent.Get())
				{
					OutChildren.Append(Parent->Children);
				}
			})
		.TreeItemsSource(&EventsTreeElements)
		.ItemHeight(32)
		.OnSelectionChanged_Lambda([this](const TSharedPtr<UE::StateTreeDebugger::FEventTreeElement>& InSelectedItem, ESelectInfo::Type SelectionType)
		{
			if (!InSelectedItem.IsValid())
			{
				return;
			}

			TSharedPtr<SWidget> DetailsView;

			FString TypePath;
			FString InstanceDataAsText;

			Visit([&TypePath, &InstanceDataAsText](auto& TypedEvent)
				{
					TypePath = TypedEvent.GetDataTypePath();
					InstanceDataAsText = TypedEvent.GetDataAsText();
				}, InSelectedItem->Event);
				
			if (!TypePath.IsEmpty())
			{
				FDetailsViewArgs DetailsViewArgs;
				DetailsViewArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;

				UScriptStruct* ScriptStruct = FindObject<UScriptStruct>(nullptr, *TypePath, /*ExactClass*/false);
				if (ScriptStruct == nullptr)
				{
					ScriptStruct = LoadObject<UScriptStruct>(nullptr, *TypePath);
				}

				if (ScriptStruct != nullptr)
				{
					SelectedNodeDataStruct.InitializeAs(ScriptStruct);

					ScriptStruct->ImportText(*InstanceDataAsText, SelectedNodeDataStruct.GetMutableMemory(), /*OwnerObject*/nullptr, PPF_None, GLog, ScriptStruct->GetName());

					SelectedNodeDataStruct.GetScriptStruct();
					FPropertyEditorModule& PropertyEditorModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
					const TSharedPtr<FStructOnScope> InstanceDataStruct = MakeShared<FStructOnScope>(SelectedNodeDataStruct.GetScriptStruct(), const_cast<uint8*>(SelectedNodeDataStruct.GetMemory()));

					FStructureDetailsViewArgs StructureViewArgs;
					StructureViewArgs.bShowObjects = true;
					StructureViewArgs.bShowAssets = true;
					StructureViewArgs.bShowClasses = true;
					StructureViewArgs.bShowInterfaces = true;
					const TSharedRef<IStructureDetailsView> StructDetailsView = PropertyEditorModule.CreateStructureDetailView(DetailsViewArgs, StructureViewArgs, InstanceDataStruct);

					DetailsView = StructDetailsView->GetDetailsView()->AsShared();
				}

				// UObject
				UClass* Class = FindObject<UClass>(nullptr, *TypePath, /*ExactClass*/false);
				if (Class == nullptr)
				{
					Class = LoadObject<UClass>(nullptr, *TypePath);
				}

				if (Class != nullptr)
				{
					if (SelectedNodeDataObject.IsValid())
					{
						SelectedNodeDataObject->RemoveFromRoot();
					}
					UE::StateTreeDebugger::FTraceTextObjectFactory ObjectFactory;
					if (ObjectFactory.CanCreateObjectsFromText(InstanceDataAsText))
					{
						ObjectFactory.ProcessBuffer(GetTransientPackage(), RF_Transactional, InstanceDataAsText);
						SelectedNodeDataObject = ObjectFactory.NodeInstanceObject;
						SelectedNodeDataObject->AddToRoot();

						FPropertyEditorModule& PropertyEditorModule = FModuleManager::Get().GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
						const TSharedRef<IDetailsView> ObjectDetailsView = PropertyEditorModule.CreateDetailView(DetailsViewArgs);
						ObjectDetailsView->SetObject(SelectedNodeDataObject.Get());
						DetailsView = ObjectDetailsView->AsShared();
					}
				}
			}

			if (DetailsView)
			{
				PropertiesBorder->SetContent(DetailsView.ToSharedRef());
			}
			else
			{
				PropertiesBorder->ClearContent();
			}
		})
		.AllowOverscroll(EAllowOverscroll::No);

	ChildSlot
	[
		SNew(SBorder)
		.Padding(4.0f)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 4.0f)
			[
				ToolbarBuilder.MakeWidget()
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SBorder)
				[
					SAssignNew(HeaderSplitter, SSplitter)
					.Orientation(Orient_Horizontal)
					+ SSplitter::Slot()
					.Value(0.2f)
					.MinSize(350)
					.Resizable(false)
					[
						TraceSelectionBox
					]
					+ SSplitter::Slot()
					.Resizable(false)
					[
						SNew(SSimpleTimeSlider)
						.DesiredSize({100, 24})
						.ClampRangeHighlightSize(0.15f)
						.ClampRangeHighlightColor(FLinearColor::Red.CopyWithNewOpacity(0.5f))
						.ScrubPosition(ScrubTimeAttribute)
						.ViewRange_Lambda([this]() { return ViewRange; })
						.OnViewRangeChanged_Lambda([this](TRange<double> NewRange) { ViewRange = NewRange; })
						.ClampRange_Lambda([this]() { return TRange<double>(0, Debugger->GetRecordingDuration()); })
						.OnScrubPositionChanged_Lambda([this](double NewScrubTime, bool bIsScrubbing) { OnTimeLineScrubPositionChanged(NewScrubTime, bIsScrubbing); })
					]
				]
			]
			+ SVerticalBox::Slot()
			[
				SNew(SSplitter)
				.Orientation(Orient_Vertical)
				+ SSplitter::Slot()
				.Value(0.2f)
				[
					SAssignNew(TreeViewsSplitter, SSplitter)
					.Orientation(Orient_Horizontal)
					+ SSplitter::Slot()
					.Value(0.2f)
					.MinSize(350)
					.OnSlotResized_Lambda([this](float Size)
						{
							// Sync both header and content
							TreeViewsSplitter->SlotAt(0).SetSizeValue(Size);
							HeaderSplitter->SlotAt(0).SetSizeValue(Size);
						})
					[
						SNew(SScrollBox)
						.Orientation(Orient_Horizontal)
						+ SScrollBox::Slot()
						.FillSize(1.0f)
						[
							InstancesTreeView.ToSharedRef()
						]
					]
					+ SSplitter::Slot()
					.OnSlotResized_Lambda([this](float Size)
						{
							TreeViewsSplitter->SlotAt(1).SetSizeValue(Size);
							HeaderSplitter->SlotAt(1).SetSizeValue(Size);
						})
					[
						SNew(SScrollBox)
						.Orientation(Orient_Vertical)
						+ SScrollBox::Slot()
						[
							InstanceTimelinesTreeView.ToSharedRef()
						]
					]
				]
				+ SSplitter::Slot()
				[
					SNew(SSplitter)
					.Orientation(Orient_Horizontal)
					+ SSplitter::Slot()
					.MinSize(400)
					[
						SNew(SScrollBox)
						.Orientation(Orient_Horizontal)
						+ SScrollBox::Slot()
						.FillSize(1.0f)
						[
							EventsTreeView.ToSharedRef()
						]
					]
					+ SSplitter::Slot()
					.MinSize(400)
					[
						SNew(SScrollBox)
						.Orientation(Orient_Horizontal)
						+ SScrollBox::Slot()
						.FillSize(1.0f)
						[
							SAssignNew(PropertiesBorder, SBorder)
						]
					]
				]
			]
		]
	];
}

void SStateTreeDebuggerView::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_StateTreeDebuggerView_TickView);

	//Stick to most recent data
	if (Debugger->IsPaused() == false)
	{
		Debugger->SetScrubTime(Debugger->GetRecordingDuration());
	}
	
	RefreshTracks();
}

void SStateTreeDebuggerView::RefreshTracks()
{
	bool bChanged = false;
	for (const TSharedPtr<RewindDebugger::FRewindDebuggerTrack>& DebugTrack : InstanceTracks)
	{
		bChanged = DebugTrack->Update() || bChanged;
	}

	if (bChanged)
	{
		InstancesTreeView->Refresh();
		InstanceTimelinesTreeView->Refresh();
		TrackCursor();
	}
}

void SStateTreeDebuggerView::BindDebuggerToolbarCommands(const TSharedRef<FUICommandList>& ToolkitCommands)
{
	const FStateTreeDebuggerCommands& Commands = FStateTreeDebuggerCommands::Get();

	ToolkitCommands->MapAction(
		Commands.StartRecording,
		FExecuteAction::CreateSP(this, &SStateTreeDebuggerView::StartRecording),
		FCanExecuteAction::CreateSP(this, &SStateTreeDebuggerView::CanStartRecording),
		FIsActionChecked(),
		FIsActionButtonVisible::CreateLambda([this]() { return !IsRecording();}));

	ToolkitCommands->MapAction(
		Commands.StopRecording,
		FExecuteAction::CreateSP(this, &SStateTreeDebuggerView::StopRecording),
		FCanExecuteAction::CreateSP(this, &SStateTreeDebuggerView::CanStopRecording),
		FIsActionChecked(),
		FIsActionButtonVisible::CreateRaw(this, &SStateTreeDebuggerView::CanStopRecording));
	
	ToolkitCommands->MapAction(
		Commands.PreviousFrameWithStateChange,
		FExecuteAction::CreateSP(this, &SStateTreeDebuggerView::StepBackToPreviousStateChange),
		FCanExecuteAction::CreateSP(this, &SStateTreeDebuggerView::CanStepBackToPreviousStateChange));

	ToolkitCommands->MapAction(
		Commands.PreviousFrameWithEvents,
		FExecuteAction::CreateSP(this, &SStateTreeDebuggerView::StepBackToPreviousStateWithEvents),
		FCanExecuteAction::CreateSP(this, &SStateTreeDebuggerView::CanStepBackToPreviousStateWithEvents));

	ToolkitCommands->MapAction(
		Commands.NextFrameWithEvents,
		FExecuteAction::CreateSP(this, &SStateTreeDebuggerView::StepForwardToNextStateWithEvents),
		FCanExecuteAction::CreateSP(this, &SStateTreeDebuggerView::CanStepForwardToNextStateWithEvents));

	ToolkitCommands->MapAction(
		Commands.NextFrameWithStateChange,
		FExecuteAction::CreateSP(this, &SStateTreeDebuggerView::StepForwardToNextStateChange),
		FCanExecuteAction::CreateSP(this, &SStateTreeDebuggerView::CanStepForwardToNextStateChange));
}

bool SStateTreeDebuggerView::CanStepBackToPreviousStateWithEvents() const
{
	return Debugger->CanStepBackToPreviousStateWithEvents();
}

void SStateTreeDebuggerView::StepBackToPreviousStateWithEvents()
{
	Debugger->StepBackToPreviousStateWithEvents();
}

bool SStateTreeDebuggerView::CanStepForwardToNextStateWithEvents() const
{
	return Debugger->CanStepForwardToNextStateWithEvents();
}

void SStateTreeDebuggerView::StepForwardToNextStateWithEvents()
{
	Debugger->StepForwardToNextStateWithEvents();
}

bool SStateTreeDebuggerView::CanStepBackToPreviousStateChange() const
{
	return Debugger->CanStepBackToPreviousStateChange();
}

void SStateTreeDebuggerView::StepBackToPreviousStateChange()
{
	Debugger->StepBackToPreviousStateChange();
}

bool SStateTreeDebuggerView::CanStepForwardToNextStateChange() const
{
	return Debugger->CanStepForwardToNextStateChange();
}

void SStateTreeDebuggerView::StepForwardToNextStateChange()
{
	Debugger->StepForwardToNextStateChange();
}

bool SStateTreeDebuggerView::CanToggleBreakpoint() const
{
	return (Debugger.IsValid() && StateTreeViewModel.IsValid() && StateTreeViewModel->HasSelection());
}

void SStateTreeDebuggerView::ToggleBreakpoint() const
{
	check(StateTreeViewModel);
	check(Debugger);
	check(StateTree.IsValid());

	TArray<UStateTreeState*> States;
	StateTreeViewModel->GetSelectedStates(States);

	TArray<FStateTreeStateHandle> StateHandles;
	StateHandles.Reserve(States.Num());
	for (const UStateTreeState* SelectedState : States)
	{
		if (SelectedState->Type != EStateTreeStateType::State || SelectedState->Parent == nullptr)
		{
			continue;
		}
		
		FStateTreeStateHandle Handle = StateTree->GetStateHandleFromId(SelectedState->ID);
		if (Handle.IsValid())
		{
			StateHandles.Add(Handle);
		}
	}
	Debugger->ToggleBreakpoints(StateHandles);
}

void SStateTreeDebuggerView::OnTimeLineScrubPositionChanged(double Time, bool bIsScrubbing)
{
	Debugger->Pause();
	Debugger->SetScrubTime(Time);
}

void SStateTreeDebuggerView::OnDebuggerScrubStateChanged(const UE::StateTreeDebugger::FScrubState& ScrubState)
{
	// Rebuild frame details from the events of that frame
	EventsTreeElements.Reset();
	ON_SCOPE_EXIT
	{
		EventsTreeView->ClearExpandedItems();
		ExpandAll(EventsTreeElements);
		EventsTreeView->RequestTreeRefresh();
	};

	const UE::StateTreeDebugger::FInstanceEventCollection& EventCollection = ScrubState.GetEventCollection();
	const TConstArrayView<const FStateTreeTraceEventVariantType> Events = EventCollection.Events;

	if (Events.IsEmpty() || ScrubState.IsInBounds() == false)
	{
		return;
	}

	const TConstArrayView<UE::StateTreeDebugger::FFrameSpan> Spans = EventCollection.FrameSpans;
	check(Spans.Num());
	check(StateTree.IsValid());

	struct FParentInfo
	{
		TSharedPtr<UE::StateTreeDebugger::FEventTreeElement> Element;
		FStateTreeStateHandle State;
	};
	TArray<FParentInfo, TInlineAllocator<8>> ParentStack;
	EStateTreeUpdatePhase LastPhase = EStateTreeUpdatePhase::Unset;
	EStateTreeUpdatePhase EventPhase = EStateTreeUpdatePhase::Unset;

	const int32 SpanIdx = ScrubState.FrameSpanIndex;
	const int32 FirstEventIdx = Spans[SpanIdx].EventIdx;
	const TraceServices::FFrame Frame = Spans[SpanIdx].Frame;
	const int32 MaxEventIdx = Spans.IsValidIndex(SpanIdx+1) ? Spans[SpanIdx+1].EventIdx : Events.Num();
	
	auto CreateParentFunc = [this, &ParentStack, Frame](const EStateTreeUpdatePhase NodePhase, const FStateTreeStateHandle StateHandle)
		{
			// Create log event to describe the phase
			FString NodeDesc;
			if (StateHandle.IsValid())
			{
				const FCompactStateTreeState* CompactState = StateTree->GetStateFromHandle(StateHandle);
				NodeDesc = FString::Printf(TEXT(" '%s'"), CompactState != nullptr ? *CompactState->Name.ToString() : *StateHandle.Describe());
			}

			FStateTreeTraceLogEvent LogEvent(/*RecordingWorldTime*/0, NodePhase, FString::Printf(TEXT("%s%s"), *UEnum::GetDisplayValueAsText(NodePhase).ToString(), *NodeDesc));

			// Create Tree element to hold the event
			const TSharedPtr<UE::StateTreeDebugger::FEventTreeElement> NewElement = MakeShareable(
				new UE::StateTreeDebugger::FEventTreeElement(Frame, FStateTreeTraceEventVariantType(TInPlaceType<FStateTreeTraceLogEvent>(), LogEvent)));

			// Push tree element to the proper hierarchy level
			TArray<TSharedPtr<UE::StateTreeDebugger::FEventTreeElement>>& TreeElements = (ParentStack.IsEmpty()) ? EventsTreeElements : ParentStack.Top().Element->Children;
			return ParentStack.Push({TreeElements.Add_GetRef(NewElement), StateHandle});
		};
	
	for (int32 EventIdx = FirstEventIdx; EventIdx < MaxEventIdx; EventIdx++)
	{
		const FStateTreeTraceEventVariantType& Event = Events[EventIdx];
		Visit([&EventPhase](auto& TypedEvent)
		{
			EventPhase = TypedEvent.Phase;
		}, Event);

		// Keep track of required state push/pop to be processed after phases
		FStateTreeStateHandle StateToPush;
		bool bPopState = false;

		if (const FStateTreeTraceStateEvent* StateEvent = Event.TryGet<FStateTreeTraceStateEvent>())
		{
			if (StateEvent->EventType == EStateTreeTraceNodeEventType::PushStateSelection)
			{
				StateToPush = StateEvent->GetStateHandle();
			}
			else if (StateEvent->EventType == EStateTreeTraceNodeEventType::PopStateSelection)
			{
				bPopState = true;
			}
		}

		// Create a hierarchy level for each update phase
		if (EventPhase != LastPhase)
		{
			const UEnum* PhaseEnum = StaticEnum<EStateTreeUpdatePhase>();
			EStateTreeUpdatePhase PhasesDiff = EventPhase ^ LastPhase;
			const int32 NumEnum = PhaseEnum->NumEnums();
			check(NumEnum > 0);

			// Pop phases first from last enum (every bit in the previous phase that differs must be popped)
			if (EnumHasAnyFlags(LastPhase, PhasesDiff))
			{
				for (int i = NumEnum - 1; i >= 0; --i)
				{
					const EStateTreeUpdatePhase Phase = static_cast<EStateTreeUpdatePhase>(PhaseEnum->GetValueByIndex(i));
					if (EnumHasAnyFlags(LastPhase & PhasesDiff, Phase))
					{
						check(ParentStack.Num());
						ParentStack.Pop();
						EnumRemoveFlags(PhasesDiff, Phase);
						if (EnumHasAnyFlags(LastPhase, PhasesDiff) == false)
						{
							break;
						}
					}
				}
			}

			// Push required phases from first enum
			if (EnumHasAnyFlags(EventPhase, PhasesDiff))
			{
				const FStateTreeStateHandle StateHandle = ParentStack.IsEmpty() ? FStateTreeStateHandle() : ParentStack.Top().State; 
				for (int i = 0; i < NumEnum; ++i)
				{
					const EStateTreeUpdatePhase Phase = static_cast<EStateTreeUpdatePhase>(PhaseEnum->GetValueByIndex(i));
					if (EnumHasAnyFlags(EventPhase & PhasesDiff, Phase))
					{
						CreateParentFunc(Phase, StateHandle);
						
						EnumRemoveFlags(PhasesDiff, Phase);
						if (EnumHasAnyFlags(EventPhase, PhasesDiff) == false)
						{
							break;
						}
					}
				}
			}
		}

		LastPhase = EventPhase;

		if (bPopState)
		{
			ParentStack.Pop();
			// We use pop state to maintain the state stack but there is no element to visualize
			continue;
		}

		if (StateToPush.IsValid())
		{
			// We use pushed state to create a new parent only
			CreateParentFunc(EStateTreeUpdatePhase::StateSelection, StateToPush);
			continue;
		}

		TArray<TSharedPtr<UE::StateTreeDebugger::FEventTreeElement>>& Elements = ParentStack.IsEmpty() ? EventsTreeElements : ParentStack.Top().Element->Children;
        Elements.Add(MakeShareable(new UE::StateTreeDebugger::FEventTreeElement(Spans[SpanIdx].Frame, Event)));
	}
}

void SStateTreeDebuggerView::ExpandAll(const TArray<TSharedPtr<UE::StateTreeDebugger::FEventTreeElement>>& Items)
{
	for (const TSharedPtr<UE::StateTreeDebugger::FEventTreeElement>& Item : Items)
	{
		EventsTreeView->SetItemExpansion(Item, true);
		ExpandAll(Item->Children);
	}
}

void SStateTreeDebuggerView::OnBreakpointHit(const FStateTreeInstanceDebugId InstanceId, const FStateTreeStateHandle StateHandle, const TSharedRef<FUICommandList> ActionList) const
{
	if (FPlayWorldCommands::Get().PausePlaySession.IsValid())
	{
		if (ActionList->CanExecuteAction(FPlayWorldCommands::Get().PausePlaySession.ToSharedRef()))
		{
			ActionList->ExecuteAction(FPlayWorldCommands::Get().PausePlaySession.ToSharedRef());
		}
	}
}

void SStateTreeDebuggerView::OnNewInstance(FStateTreeInstanceDebugId InstanceId)
{
	const TSharedPtr<RewindDebugger::FRewindDebuggerTrack>* ExistingTrack = InstanceTracks.FindByPredicate([InstanceId](const TSharedPtr<RewindDebugger::FRewindDebuggerTrack>& Track)
	{
		return static_cast<FStateTreeDebuggerTrack*>(Track.Get())->GetInstanceId() == InstanceId;
	});

	if (ExistingTrack == nullptr)
	{
		InstanceTracks.Add(MakeShared<FStateTreeDebuggerTrack>(Debugger, InstanceId, Debugger->GetInstanceDescription(InstanceId), ViewRange));
	}

	InstancesTreeView->Refresh();
	InstanceTimelinesTreeView->Refresh();
}

void SStateTreeDebuggerView::OnSelectedInstanceCleared()
{
	EventsTreeElements.Reset();
	if (EventsTreeView)
	{
		EventsTreeView->RequestTreeRefresh();	
	}

	PropertiesBorder->ClearContent();
}

TSharedRef<SWidget> SStateTreeDebuggerView::OnGetDebuggerTracesMenu() const
{
	FMenuBuilder MenuBuilder(/*bInShouldCloseWindowAfterMenuSelection*/true, /*InCommandList*/nullptr);

	if (Debugger.IsValid())
	{
		TArray<FStateTreeDebugger::FTraceDescriptor> TraceDescriptors;
		Debugger->GetLiveTraces(TraceDescriptors);

		for (const FStateTreeDebugger::FTraceDescriptor& TraceDescriptor : TraceDescriptors)
		{
			const FText Desc = Debugger->DescribeTrace(TraceDescriptor);

			FUIAction ItemAction(FExecuteAction::CreateLambda([WeakDebugger = Debugger, TraceDescriptor]()
			{
				if (WeakDebugger)
				{
					WeakDebugger->StartSessionAnalysis(TraceDescriptor);
				}
			}));
			MenuBuilder.AddMenuEntry(Desc, TAttribute<FText>(), FSlateIcon(), ItemAction);
		}

		// Failsafe when no match
		if (TraceDescriptors.Num() == 0)
		{
			const FText Desc = LOCTEXT("NoLiveSessions","Can't find live trace sessions");
			FUIAction ItemAction(FExecuteAction::CreateLambda([WeakDebugger = Debugger]()
			{
				if (WeakDebugger)
				{
					WeakDebugger->StartSessionAnalysis(FStateTreeDebugger::FTraceDescriptor());
				}
			}));
			MenuBuilder.AddMenuEntry(Desc, TAttribute<FText>(), FSlateIcon(), ItemAction);
		}
	}

	return MenuBuilder.MakeWidget();
}

void SStateTreeDebuggerView::TrackCursor()
{
	const double ScrubTime = ScrubTimeAttribute.Get();
	TRange<double> CurrentViewRange = ViewRange;
	const double ViewRangeDuration = CurrentViewRange.GetUpperBoundValue() - CurrentViewRange.GetLowerBoundValue();

	static constexpr double LeadingMarginFraction = 0.05;
	static constexpr double TrailingMarginFraction = 0.01;

	if (ScrubTime > CurrentViewRange.GetUpperBoundValue() - ViewRangeDuration * LeadingMarginFraction)
	{
		CurrentViewRange.SetUpperBound(ScrubTime + ViewRangeDuration * LeadingMarginFraction);
		CurrentViewRange.SetLowerBound(CurrentViewRange.GetUpperBoundValue() - ViewRangeDuration);
	}

	if (ScrubTime < CurrentViewRange.GetLowerBoundValue() - ViewRangeDuration * TrailingMarginFraction)
	{
		CurrentViewRange.SetLowerBound(ScrubTime);
		CurrentViewRange.SetUpperBound(CurrentViewRange.GetLowerBoundValue() + ViewRangeDuration);
	}

	ViewRange = CurrentViewRange;
}

#undef LOCTEXT_NAMESPACE

#endif // WITH_STATETREE_DEBUGGER