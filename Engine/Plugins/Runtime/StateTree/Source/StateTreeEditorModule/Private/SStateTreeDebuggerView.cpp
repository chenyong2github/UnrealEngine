// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_STATETREE_DEBUGGER

#include "SStateTreeDebuggerView.h"

#include "Editor.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "IMessageLogListing.h"
#include "Kismet2/DebuggerCommands.h"
#include "MessageLogModule.h"
#include "Modules/ModuleManager.h"
#include "StateTree.h"
#include "Debugger/StateTreeDebugger.h"
#include "Debugger/StateTreeTraceTypes.h"
#include "StateTreeDebuggerCommands.h"
#include "StateTreeState.h"
#include "StateTreeTaskBase.h"
#include "StateTreeViewModel.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Layout/SBorder.h"

#define LOCTEXT_NAMESPACE "StateTreeEditor"

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
	FEditorDelegates::BeginPIE.RemoveAll(this);
	FEditorDelegates::EndPIE.RemoveAll(this);
	FEditorDelegates::PausePIE.RemoveAll(this);
	FEditorDelegates::ResumePIE.RemoveAll(this);
	FEditorDelegates::SingleStepPIE.RemoveAll(this);
}

void SStateTreeDebuggerView::OnPIEStarted(const bool bIsSimulating) const
{
	if (DebuggerTraceListing)
	{
		DebuggerTraceListing->ClearMessages();
	}

	if (!Debugger->IsAnalysisSessionActive())
	{
		Debugger->StartLastLiveSessionAnalysis();
	}
}

void SStateTreeDebuggerView::OnPIEStopped(const bool bIsSimulating) const
{
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

void SStateTreeDebuggerView::Construct(const FArguments& InArgs, const UStateTree* InStateTree, const TSharedRef<FStateTreeViewModel> InStateTreeViewModel, const TSharedRef<FUICommandList> InCommandList)
{
	StateTreeViewModel = InStateTreeViewModel;
	StateTree = InStateTree;
	
	Debugger = InStateTreeViewModel->GetDebugger();

	// Put debugger in proper simulation state when view is constructed after PIE/SIE was started
	if (FPlayWorldCommandCallbacks::HasPlayWorldAndPaused())
	{
		Debugger->Pause();
	}

	BindDebuggerToolbarCommands(InCommandList);

	FMessageLogModule& MessageLogModule = FModuleManager::LoadModuleChecked<FMessageLogModule>("MessageLog");
	FMessageLogInitializationOptions LogOptions;
	LogOptions.bShowPages = false;
	LogOptions.bShowFilters = false;
	LogOptions.bAllowClear = false;
	LogOptions.bScrollToBottom = true;
	LogOptions.MaxPageCount = 1;

	DebuggerTraceListing = MessageLogModule.CreateLogListing("StateTreeDebugger", LogOptions);
	DebuggerTraces = MessageLogModule.CreateLogListingWidget(DebuggerTraceListing.ToSharedRef());

	const TSharedRef<FUICommandList> CommandList = MakeShareable(new FUICommandList);

	// Register the play world commands
	CommandList->Append(FPlayWorldCommands::GlobalPlayWorldActions.ToSharedRef());
	
	CommandList->MapAction(
		FStateTreeDebuggerCommands::Get().ToggleBreakpoint,
		FExecuteAction::CreateSP(this, &SStateTreeDebuggerView::ToggleBreakpoint),
		FCanExecuteAction::CreateSP(this, &SStateTreeDebuggerView::CanToggleBreakpoint)
	);
	
	const TSharedRef<SWidget> TraceSelectionBox = SNew(SComboButton)
		.OnGetMenuContent(this, &SStateTreeDebuggerView::OnGetDebuggerTracesMenu)
		.ButtonContent()
		[
			SNew(STextBlock)
			.ToolTipText(LOCTEXT("SelectTraceSession", "Pick trace session to debug"))
			.Text_Lambda([Debugger = Debugger]()
			{
				return Debugger.IsValid() ? FText::FromString(Debugger->GetDebuggedTraceDescription()) : FText::GetEmpty();
			})
		];
	
	const TSharedRef<SWidget> InstanceSelectionBox = SNew(SComboButton)
		.OnGetMenuContent(this, &SStateTreeDebuggerView::OnGetDebuggerInstancesMenu)
		.ButtonContent()
		[
			SNew(STextBlock)
			.ToolTipText(LOCTEXT("SelectDebugInstance", "Pick instance to debug"))
			.Text_Lambda([Debugger = Debugger]()
			{
				return Debugger.IsValid() ? FText::FromString(Debugger->GetDebuggedInstanceDescription()) : FText::GetEmpty();
			})
		];
	
	// Build debug toolbar
	FToolBarBuilder ToolbarBuilder(CommandList, FMultiBoxCustomization::None);
	ToolbarBuilder.BeginSection(TEXT("Debugging"));
	{
		const FPlayWorldCommands& PlayWorldCommand = FPlayWorldCommands::Get();
		ToolbarBuilder.AddToolBarButton(PlayWorldCommand.RepeatLastPlay);
		ToolbarBuilder.AddToolBarButton(PlayWorldCommand.PausePlaySession,
					NAME_None, TAttribute<FText>(), TAttribute<FText>(), FSlateIcon(FAppStyle::GetAppStyleSetName(), "PlayWorld.PausePlaySession.Small"));
		ToolbarBuilder.AddToolBarButton(PlayWorldCommand.ResumePlaySession,
					NAME_None, TAttribute<FText>(), TAttribute<FText>(), FSlateIcon(FAppStyle::GetAppStyleSetName(), "PlayWorld.ResumePlaySession.Small"));	
		ToolbarBuilder.AddToolBarButton(PlayWorldCommand.StopPlaySession,
					NAME_None, TAttribute<FText>(), TAttribute<FText>(), FSlateIcon(FAppStyle::GetAppStyleSetName(), "PlayWorld.StopPlaySession.Small"));
		ToolbarBuilder.AddSeparator();
		ToolbarBuilder.AddToolBarButton(FStateTreeDebuggerCommands::Get().Back);
		ToolbarBuilder.AddToolBarButton(FStateTreeDebuggerCommands::Get().Forward);
		ToolbarBuilder.AddSeparator();
		ToolbarBuilder.AddWidget(TraceSelectionBox);
		ToolbarBuilder.AddSeparator();
		ToolbarBuilder.AddWidget(InstanceSelectionBox);
	}
	ToolbarBuilder.EndSection();

	InCommandList->Append(CommandList);

	TSharedPtr<FUICommandList> ActionList = InCommandList;
		Debugger->OnTracesUpdated.BindSP(this, &SStateTreeDebuggerView::OnTracesUpdated);
	Debugger->OnBreakpointHit.BindSP(this, &SStateTreeDebuggerView::OnBreakpointHit, ActionList);

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
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					ToolbarBuilder.MakeWidget()
				]
			]
			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			.Padding(0.0f, 0.0f, 0.0f, 4.0f)
			[
				SNew(SBox)
				[
					DebuggerTraces.ToSharedRef()
				]
			]
		]
	];
}

void SStateTreeDebuggerView::BindDebuggerToolbarCommands(const TSharedRef<FUICommandList> ToolkitCommands)
{
	const FStateTreeDebuggerCommands& Commands = FStateTreeDebuggerCommands::Get();
	
	ToolkitCommands->MapAction(
		Commands.Back,
		FExecuteAction::CreateSP(this, &SStateTreeDebuggerView::StepBack),
		FCanExecuteAction::CreateSP(this, &SStateTreeDebuggerView::CanStepBack));

	ToolkitCommands->MapAction(
		Commands.Forward,
		FExecuteAction::CreateSP(this, &SStateTreeDebuggerView::StepForward),
		FCanExecuteAction::CreateSP(this, &SStateTreeDebuggerView::CanStepForward));
}

bool SStateTreeDebuggerView::CanStepBack() const
{
	return Debugger->CanStepBack();
}

void SStateTreeDebuggerView::StepBack() const
{
	Debugger->StepBack();
}

bool SStateTreeDebuggerView::CanStepForward() const
{
	return Debugger->CanStepForward();
}

void SStateTreeDebuggerView::StepForward() const
{
	Debugger->StepForward();
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

void SStateTreeDebuggerView::OnTracesUpdated(
	const TConstArrayView<const FStateTreeTraceEventVariantType> Events,
	const TConstArrayView<UE::StateTreeDebugger::FFrameIndexSpan> Spans
	) const
{
	DebuggerTraceListing->ClearMessages();
	
	if (Events.IsEmpty())
	{
		return;
	}

	check(Spans.Num());

	TArray<TSharedRef<FTokenizedMessage>> Messages;
	Messages.Reserve(Events.Num());

	check(StateTree.IsValid());

	int32 SpanIdx = INDEX_NONE;
	for (int32 EventIdx = 0; EventIdx < Events.Num(); EventIdx++)
	{
		const FStateTreeTraceEventVariantType& Event = Events[EventIdx];
		
		if (SpanIdx == INDEX_NONE
			|| (Spans.IsValidIndex(SpanIdx+1) && EventIdx >= Spans[SpanIdx+1].EventIdx))
		{
			SpanIdx = SpanIdx == INDEX_NONE ? 0 : SpanIdx+1;
			Messages.Add(FTokenizedMessage::Create(EMessageSeverity::Info,
				FText::FromString(FString::Printf(TEXT("---------------- Frame: %s ----------------"), *LexToString(Spans[SpanIdx].FrameIdx)))));
		}
		
		// Add log event messages directly
		if (const FStateTreeTraceLogEvent* LogEvent = Event.TryGet<FStateTreeTraceLogEvent>())
		{
			if (LogEvent->Message.Len())
			{
				Messages.Add(FTokenizedMessage::Create(EMessageSeverity::Info, FText::FromString(LogEvent->Message)));	
			}			
			continue;	
		}

		// Add messages for state events 
		if (const FStateTreeTraceStateEvent* StateEvent = Event.TryGet<FStateTreeTraceStateEvent>())
		{
			const FStateTreeStateHandle StateHandle(StateEvent->StateIdx);
			if (const FCompactStateTreeState* CompactState = StateTree->GetStateFromHandle(StateHandle))
			{
				Messages.Add(FTokenizedMessage::Create(EMessageSeverity::Info, FText::FromString(FString::Printf(TEXT("%s State '%s'"),
								*StaticEnum<EStateTreeTraceNodeEventType>()->GetNameStringByValue((int64)StateEvent->EventType),
								*CompactState->Name.ToString()))));
			}
		}

		if (const FStateTreeTraceTaskEvent* TaskEvent = Event.TryGet<FStateTreeTraceTaskEvent>())
		{
			FConstStructView NodeView = StateTree->GetNode(TaskEvent->TaskIdx);
			const FStateTreeTaskBase* Task = NodeView.GetPtr<const FStateTreeTaskBase>();
					
			Messages.Add(FTokenizedMessage::Create(EMessageSeverity::Info, FText::FromString(FString::Printf(TEXT("  %s Task '%s'"),
				*StaticEnum<EStateTreeTraceNodeEventType>()->GetNameStringByValue((int64)TaskEvent->EventType),
				Task != nullptr ? *Task->Name.ToString() : *LexToString(TaskEvent->TaskIdx)))));
		}
	}

	DebuggerTraceListing->AddMessages(Messages, /*bMirrorToOutputLog*/false);
}

void SStateTreeDebuggerView::OnBreakpointHit(const FStateTreeInstanceDebugId InstanceId, const FStateTreeStateHandle StateHandle, const TSharedPtr<FUICommandList> ActionList) const
{
	if (ActionList.IsValid() && FPlayWorldCommands::Get().PausePlaySession.IsValid())
	{
		if (ActionList->CanExecuteAction(FPlayWorldCommands::Get().PausePlaySession.ToSharedRef()))
		{
			// Make sure the instance is selected in case the breakpoint was set for all instances 
			const FStateTreeDebuggerInstanceDesc& DebuggerInstance = Debugger->GetDebuggedInstance();
			if (DebuggerInstance.Id != InstanceId)
			{
				const TConstArrayView<FStateTreeDebuggerInstanceDesc> ActiveInstances = Debugger->GetActiveInstances();
				const FStateTreeDebuggerInstanceDesc* FoundDesc = ActiveInstances.FindByPredicate([InstanceId](const FStateTreeDebuggerInstanceDesc& InstanceDesc)
				{
					return InstanceDesc.Id == InstanceId;
				});

				if (FoundDesc != nullptr)
				{
					Debugger->SetDebuggedInstance(*FoundDesc);
				}
			}

			ActionList->ExecuteAction(FPlayWorldCommands::Get().PausePlaySession.ToSharedRef());
		}
	}
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
			const FText Desc = FText::FromString(Debugger->DescribeTrace(TraceDescriptor));

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

TSharedRef<SWidget> SStateTreeDebuggerView::OnGetDebuggerInstancesMenu() const
{
	FMenuBuilder MenuBuilder(/*bInShouldCloseWindowAfterMenuSelection*/true, /*InCommandList*/nullptr);

	if (Debugger.IsValid() && StateTreeViewModel.IsValid())
	{
		const TConstArrayView<FStateTreeDebuggerInstanceDesc> ActiveInstances = Debugger->GetActiveInstances();
		const UStateTree* CurrentStateTreeAsset = StateTreeViewModel->GetStateTree();

		for (const FStateTreeDebuggerInstanceDesc& Instance : ActiveInstances)
		{
			// Do not list instances not matching the debugged asset
			if (Instance.StateTree.Get() != CurrentStateTreeAsset)
			{
				continue;
			}
			
			const FText Desc = FText::FromString(Debugger->DescribeInstance(Instance));
			FUIAction ItemAction(FExecuteAction::CreateLambda([WeakDebugger = Debugger, Instance]()
			{
				if (WeakDebugger)
				{
					WeakDebugger->SetDebuggedInstance(Instance);
				}
			}));
			MenuBuilder.AddMenuEntry(Desc, TAttribute<FText>(), FSlateIcon(), ItemAction);
		}

		// Failsafe when no match
		if (ActiveInstances.Num() == 0)
		{
			const FText Desc = LOCTEXT("NoMatchingInstance","Can't find matching instances");
			FUIAction ItemAction(FExecuteAction::CreateLambda([WeakDebugger = Debugger]()
			{
				if (WeakDebugger)
				{
					WeakDebugger->SetDebuggedInstance(FStateTreeDebuggerInstanceDesc());
				}
			}));
			MenuBuilder.AddMenuEntry(Desc, TAttribute<FText>(), FSlateIcon(), ItemAction);
		}
	}

	return MenuBuilder.MakeWidget();
}

#undef LOCTEXT_NAMESPACE

#endif // WITH_STATETREE_DEBUGGER