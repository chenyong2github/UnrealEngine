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
#include "PropertyEditorModule.h"
#include "StateTree.h"
#include "StateTreeDebuggerCommands.h"
#include "StateTreeExecutionContext.h"
#include "StateTreeState.h"
#include "StateTreeViewModel.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SSpacer.h"

#define LOCTEXT_NAMESPACE "StateTreeEditor"


//----------------------------------------------------------------------//
// FStateTreeTraceTextObjectFactory
//----------------------------------------------------------------------//
struct FStateTreeTraceTextObjectFactory : FCustomizableTextObjectFactory
{
	UObject* NodeInstanceObject = nullptr;
	FStateTreeTraceTextObjectFactory() : FCustomizableTextObjectFactory(GWarn) {}

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
// SStateTreeDebuggerTableRow
//----------------------------------------------------------------------//
class SStateTreeDebuggerTableRow : public SMultiColumnTableRow<TSharedPtr<FTreeElement>>
{
public:
	void Construct(const FArguments& InArgs, const TSharedPtr<STableViewBase>& InOwnerTableView, const TSharedPtr<FTreeElement>& InElement, const TSharedRef<FStateTreeViewModel>& InStateTreeViewModel)
	{
		Item = InElement;
		StateTreeViewModel = InStateTreeViewModel.ToSharedPtr();
		SMultiColumnTableRow::Construct(InArgs, InOwnerTableView.ToSharedRef());
	}

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
					.Text_Lambda([&Event=Item->Event, &StateTreeViewModel=StateTreeViewModel]()
					{
						const UStateTree* StateTree = StateTreeViewModel->GetStateTree();
						
						// Use log event messages directly
						if (const FStateTreeTraceLogEvent* LogEvent = Event.TryGet<FStateTreeTraceLogEvent>())
						{
							if (LogEvent->Message.Len())
							{
								return FText::FromString(*LogEvent->Message);
							}							
						}
						// Process state events (index has a different meaning)
						else if (const FStateTreeTraceStateEvent* StateEvent = Event.TryGet<FStateTreeTraceStateEvent>())
						{
							const FStateTreeStateHandle StateHandle(StateEvent->Idx);
							if (const FCompactStateTreeState* CompactState = StateTree->GetStateFromHandle(StateHandle))
							{
								return FText::FromString(FString::Printf(TEXT("%s State '%s'"),
												*StaticEnum<EStateTreeTraceNodeEventType>()->GetNameStringByValue((int64)StateEvent->EventType),
												*CompactState->Name.ToString()));
							}
						}
						// Process Tasks events
						else if (const FStateTreeTraceTaskEvent* TaskEvent = Event.TryGet<FStateTreeTraceTaskEvent>())
						{
							const FConstStructView NodeView = StateTree->GetNode(TaskEvent->Idx);
							const FStateTreeNodeBase* Node = NodeView.GetPtr<const FStateTreeNodeBase>();
							
							return FText::FromString(FString::Printf(TEXT("%s:%s %s '%s'"),
									*StaticEnum<EStateTreeTraceNodeEventType>()->GetNameStringByValue((int64)TaskEvent->EventType),
									*StaticEnum<EStateTreeRunStatus>()->GetNameStringByValue((int64)TaskEvent->Status),
									*NodeView.GetScriptStruct()->GetName(),
									Node != nullptr ? *Node->Name.ToString() : *LexToString(TaskEvent->Idx)));
						}
						// Process Conditions events
						else if (const FStateTreeTraceConditionEvent* ConditionEvent = Event.TryGet<FStateTreeTraceConditionEvent>())
						{
							const FConstStructView NodeView = StateTree->GetNode(ConditionEvent->Idx);
							const FStateTreeNodeBase* Node = NodeView.GetPtr<const FStateTreeNodeBase>();
							
							return FText::FromString(FString::Printf(TEXT("%s %s '%s'"),
									*StaticEnum<EStateTreeTraceNodeEventType>()->GetNameStringByValue((int64)ConditionEvent->EventType),
									*NodeView.GetScriptStruct()->GetName(),
									Node != nullptr ? *Node->Name.ToString() : *LexToString(ConditionEvent->Idx)));
						}
						
						return FText(); 
					})
				];
		}

		return Contents.ToSharedRef();
	}

	TSharedPtr<FStateTreeViewModel> StateTreeViewModel;
	TSharedPtr<FTreeElement> Item;
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
		Debugger->OnTracesUpdated.Unbind();
		Debugger->OnBreakpointHit.Unbind();
		Debugger->OnDebuggedInstanceSet.Unbind();
	}

	FEditorDelegates::BeginPIE.RemoveAll(this);
	FEditorDelegates::EndPIE.RemoveAll(this);
	FEditorDelegates::PausePIE.RemoveAll(this);
	FEditorDelegates::ResumePIE.RemoveAll(this);
	FEditorDelegates::SingleStepPIE.RemoveAll(this);
}

void SStateTreeDebuggerView::OnPIEStarted(const bool bIsSimulating) const
{
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

void SStateTreeDebuggerView::Construct(const FArguments& InArgs, const UStateTree* InStateTree, const TSharedRef<FStateTreeViewModel>& InStateTreeViewModel, const TSharedRef<FUICommandList>& InCommandList)
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
	Debugger->OnDebuggedInstanceSet.BindSP(this, &SStateTreeDebuggerView::OnDebuggedInstanceSet);

	PropertiesBorder = SNew(SBorder);
	
	// TreeView
	TreeView = SNew(STreeView<TSharedPtr<FTreeElement>>)
		.HeaderRow(SNew(SHeaderRow)
			+SHeaderRow::Column("Desc")
			.DefaultLabel(LOCTEXT("DescriptionColumnHeader", "Description")))
		    .OnGenerateRow_Lambda([this](const TSharedPtr<FTreeElement>& InElement, const TSharedRef<STableViewBase>& InOwnerTableView)
		    {
			    return SNew(SStateTreeDebuggerTableRow, InOwnerTableView, InElement, StateTreeViewModel.ToSharedRef());
		    } )
		    .OnGetChildren_Lambda([](const TSharedPtr<const FTreeElement>& InParent, TArray<TSharedPtr<FTreeElement>>& OutChildren)
		    {
			    if (const FTreeElement* Parent = InParent.Get())
			    {
				    OutChildren.Append(Parent->Children);
			    }
		    })
		.TreeItemsSource(&TreeElements)
		.ItemHeight(32)
		.OnSelectionChanged_Lambda([this](const TSharedPtr<FTreeElement>& InSelectedItem, ESelectInfo::Type SelectionType)
		{
			if (!InSelectedItem.IsValid())
			{
				return;
			}

			TSharedPtr<SWidget> DetailsView;

			FString TypePath;
			FString InstanceDataAsText;

			if (const FStateTreeTraceConditionEvent* ConditionEvent = InSelectedItem->Event.TryGet<FStateTreeTraceConditionEvent>())
			{
				TypePath = ConditionEvent->TypePath;
				InstanceDataAsText = ConditionEvent->InstanceDataAsText;
			}
			else if (const FStateTreeTraceTaskEvent* TaskEvent = InSelectedItem->Event.TryGet<FStateTreeTraceTaskEvent>())
			{
				TypePath = TaskEvent->TypePath;
				InstanceDataAsText = TaskEvent->InstanceDataAsText;
			}

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
					FStateTreeTraceTextObjectFactory ObjectFactory;
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
				SNew(SSplitter)
				.Orientation(Orient_Horizontal)
				+ SSplitter::Slot()
				[
					TreeView.ToSharedRef()
				]
				+ SSplitter::Slot()
				[
					PropertiesBorder.ToSharedRef()
				]
			]
		]
	];
}

void SStateTreeDebuggerView::BindDebuggerToolbarCommands(const TSharedRef<FUICommandList>& ToolkitCommands)
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
	)
{
	TreeElements.Reset();
	
	if (Events.IsEmpty())
	{
		return;
	}

	check(Spans.Num());
	check(StateTree.IsValid());

	int32 SpanIdx = INDEX_NONE;

	TArray<TSharedPtr<FTreeElement>, TInlineAllocator<8>> ParentStack;
	EStateTreeUpdatePhase LastPhase = EStateTreeUpdatePhase::Unset;
	EStateTreeUpdatePhase EventPhase = EStateTreeUpdatePhase::Unset;
	
	for (int32 EventIdx = 0; EventIdx < Events.Num(); EventIdx++)
	{
		const FStateTreeTraceEventVariantType& Event = Events[EventIdx];
		EventPhase = LastPhase;

		if (SpanIdx == INDEX_NONE
			|| (Spans.IsValidIndex(SpanIdx+1) && EventIdx >= Spans[SpanIdx+1].EventIdx))
		{
			SpanIdx = SpanIdx == INDEX_NONE ? 0 : SpanIdx+1;

			while (ParentStack.Num())
			{
				TSharedPtr<FTreeElement> RemovedElement = ParentStack.Pop();
				if (RemovedElement->Children.IsEmpty())
				{
					if (ParentStack.Num())
					{
						ParentStack.Last()->Children.Remove(RemovedElement);
					}
					TreeElements.Remove(RemovedElement);
				}
			}

			LastPhase = EStateTreeUpdatePhase::Unset;

			// Create fake event to create a parent node with frame info
			FStateTreeTraceLogEvent DummyEvent(EStateTreeUpdatePhase::Unset, FString::Printf(TEXT("Frame: %s"), *LexToString(Spans[SpanIdx].Frame.Index)));

			ParentStack.Push(TreeElements.Add_GetRef(MakeShareable(new FTreeElement(
				Spans[SpanIdx].Frame,
				FStateTreeTraceEventVariantType(TInPlaceType<FStateTreeTraceLogEvent>(), DummyEvent)))));
		}

		// Need to test each type explicitly with TVariant even if they are all FStateTreeTracePhaseEvent
		if (const FStateTreeTraceLogEvent* LogEvent = Event.TryGet<FStateTreeTraceLogEvent>())
		{
			EventPhase = LogEvent->Phase;
		}
		else if (const FStateTreeTraceStateEvent* StateEvent = Event.TryGet<FStateTreeTraceStateEvent>())
		{
			EventPhase = StateEvent->Phase;
		}
		else if (const FStateTreeTraceTaskEvent* TaskEvent = Event.TryGet<FStateTreeTraceTaskEvent>())
		{
			EventPhase = TaskEvent->Phase;
		}
		else if (const FStateTreeTraceConditionEvent* ConditionEvent = Event.TryGet<FStateTreeTraceConditionEvent>())
		{
			EventPhase = ConditionEvent->Phase;
		}

		// Create a hierarchy level for each update phase
		if (EventPhase != LastPhase)
		{
			// Remove valid previous phase from parent stack 
			if (ensure(ParentStack.Num()) && LastPhase != EStateTreeUpdatePhase::Unset)
			{
				TSharedPtr<FTreeElement> RemovedElement = ParentStack.Pop();
				if (RemovedElement->Children.IsEmpty())
				{
					if (ParentStack.Num())
					{
						ParentStack.Last()->Children.Remove(RemovedElement);	
					}
				}
			}

			LastPhase = EventPhase;

			// Create fake log event to describe the phase
			FStateTreeTraceLogEvent DummyEvent(EStateTreeUpdatePhase::Unset,
				FString::Printf(TEXT("%s"), *StaticEnum<EStateTreeUpdatePhase>()->GetNameStringByValue((int64)EventPhase)));

			// Add to the list of children of the frame element
			check(ParentStack.Num());
			ParentStack.Push(ParentStack.Last()->Children.Add_GetRef(MakeShareable(new FTreeElement(
				Spans[SpanIdx].Frame,
				FStateTreeTraceEventVariantType(TInPlaceType<FStateTreeTraceLogEvent>(), DummyEvent)))));
		}

		if (ensure(ParentStack.Num()))
		{
			ParentStack.Last()->Children.Add(MakeShareable(new FTreeElement(Spans[SpanIdx].Frame, Event)));
		}
	}

	while (ParentStack.Num())
	{
		TSharedPtr<FTreeElement> RemovedElement = ParentStack.Pop();
		if (RemovedElement->Children.IsEmpty())
		{
			if (ParentStack.Num())
			{
				ParentStack.Last()->Children.Remove(RemovedElement);
			}
			TreeElements.Remove(RemovedElement);
		}
	}

	TreeView->RequestTreeRefresh();
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

void SStateTreeDebuggerView::OnDebuggedInstanceSet()
{
	TreeElements.Reset();
	if (TreeView)
	{
		TreeView->RequestTreeRefresh();	
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