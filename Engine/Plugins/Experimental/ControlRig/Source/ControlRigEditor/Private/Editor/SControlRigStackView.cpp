// Copyright Epic Games, Inc. All Rights Reserved.

#include "SControlRigStackView.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SScrollBar.h"
#include "Widgets/Input/SSearchBox.h"
#include "ControlRigEditor.h"
#include "ControlRigEditorStyle.h"
#include "ControlRigStackCommands.h"
#include "ControlRig.h"
#include "ControlRigBlueprintGeneratedClass.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "EditorStyleSet.h"
#include "Widgets/Input/SSearchBox.h"

#define LOCTEXT_NAMESPACE "SControlRigStackView"

//////////////////////////////////////////////////////////////
/// FRigStackEntry
///////////////////////////////////////////////////////////
FRigStackEntry::FRigStackEntry(int32 InEntryIndex, ERigStackEntry::Type InEntryType, int32 InInstructionIndex, ERigVMOpCode InOpCode, const FString& InLabel)
	: EntryIndex(InEntryIndex)
	, EntryType(InEntryType)
	, InstructionIndex(InInstructionIndex)
	, OpCode(InOpCode)
	, Label(InLabel)
{

}

TSharedRef<ITableRow> FRigStackEntry::MakeTreeRowWidget(const TSharedRef<STableViewBase>& InOwnerTable, TSharedRef<FRigStackEntry> InEntry, TSharedRef<FUICommandList> InCommandList, TSharedPtr<SControlRigStackView> InStackView)
{
	return SNew(SRigStackItem, InOwnerTable, InEntry, InCommandList);
}

//////////////////////////////////////////////////////////////
/// SRigStackItem
///////////////////////////////////////////////////////////
void SRigStackItem::Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& OwnerTable, TSharedRef<FRigStackEntry> InStackEntry, TSharedRef<FUICommandList> InCommandList)
{
	WeakStackEntry = InStackEntry;
	WeakCommandList = InCommandList;

	TSharedPtr< STextBlock > NumberWidget;
	TSharedPtr< STextBlock > TextWidget;

	const FSlateBrush* Icon = nullptr;
	switch (InStackEntry->EntryType)
	{
		case ERigStackEntry::Operator:
		{
			Icon = FControlRigEditorStyle::Get().GetBrush("ControlRig.RigUnit");
			break;
		}
		case ERigStackEntry::Info:
		{
			Icon = FEditorStyle::GetBrush("Icons.Info");
			break;
		}
		case ERigStackEntry::Warning:
		{
			Icon = FEditorStyle::GetBrush("Icons.Warning");
			break;
		}
		case ERigStackEntry::Error:
		{
			Icon = FEditorStyle::GetBrush("Icons.Error");
			break;
		}
		default:
		{
			break;
		}
	}

	STableRow<TSharedPtr<FRigStackEntry>>::Construct(
		STableRow<TSharedPtr<FRigStackEntry>>::FArguments()
		.Content()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.MaxWidth(25.f)
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Right)
			[
				SAssignNew(NumberWidget, STextBlock)
				.Text(this, &SRigStackItem::GetIndexText)
			]
			+ SHorizontalBox::Slot()
			.MaxWidth(22.f)
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Center)
			[
				SNew(SImage)
				.Image(Icon)
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Left)
			[
				SAssignNew(TextWidget, STextBlock)
				.Text(this, &SRigStackItem::GetLabelText)
			]
		], OwnerTable);
}

FText SRigStackItem::GetIndexText() const
{
	FString IndexStr = FString::FromInt(WeakStackEntry.Pin()->EntryIndex) + TEXT(".");
	return FText::FromString(IndexStr);
}

FText SRigStackItem::GetLabelText() const
{
	return (FText::FromString(WeakStackEntry.Pin()->Label));
}

//////////////////////////////////////////////////////////////
/// SControlRigStackView
///////////////////////////////////////////////////////////

SControlRigStackView::~SControlRigStackView()
{
	if (ControlRigEditor.IsValid())
	{
		if (OnModelModified.IsValid() && ControlRigEditor.Pin()->GetControlRigBlueprint())
		{
			ControlRigEditor.Pin()->GetControlRigBlueprint()->OnModified().Remove(OnModelModified);
		}
		if (OnControlRigInitializedHandle.IsValid())
		{
			ControlRigEditor.Pin()->ControlRig->OnInitialized_AnyThread().Remove(OnControlRigInitializedHandle);
		}
		if (OnPreviewControlRigUpdatedHandle.IsValid())
		{
			ControlRigEditor.Pin()->OnPreviewControlRigUpdated().Remove(OnPreviewControlRigUpdatedHandle);
		}
	}
	if (ControlRigBlueprint.IsValid() && OnVMCompiledHandle.IsValid())
	{
		ControlRigBlueprint->OnVMCompiled().Remove(OnVMCompiledHandle);
	}
}

void SControlRigStackView::Construct( const FArguments& InArgs, TSharedRef<FControlRigEditor> InControlRigEditor)
{
	ControlRigEditor = InControlRigEditor;
	ControlRigBlueprint = ControlRigEditor.Pin()->GetControlRigBlueprint();
	Graph = Cast<UControlRigGraph>(ControlRigBlueprint->GetLastEditedUberGraph());
	CommandList = MakeShared<FUICommandList>();
	bSuspendModelNotifications = false;
	bSuspendControllerSelection = false;

	BindCommands();

	ChildSlot
	[
		SNew(SVerticalBox)
		+SVerticalBox::Slot()
		.AutoHeight()
		.VAlign(VAlign_Top)
		.Padding(0.0f)
		[
			SNew(SBorder)
			.Padding(0.0f)
			.BorderImage(FEditorStyle::GetBrush("DetailsView.CategoryTop"))
			.BorderBackgroundColor(FLinearColor(0.6f, 0.6f, 0.6f, 1.0f))
			[
				SNew(SVerticalBox)
				+SVerticalBox::Slot()
				.AutoHeight()
				.VAlign(VAlign_Top)
				[
					SNew(SHorizontalBox)
					//.Visibility(this, &SRigHierarchy::IsSearchbarVisible)
					+SHorizontalBox::Slot()
					.AutoWidth()
					+SHorizontalBox::Slot()
					.VAlign(VAlign_Center)
					.Padding(3.0f, 1.0f)
					[
						SAssignNew(FilterBox, SSearchBox)
						.OnTextChanged(this, &SControlRigStackView::OnFilterTextChanged)
					]
				]
			]
		]
		+SVerticalBox::Slot()
		.Padding(0.0f, 0.0f)
		[
			SNew(SBorder)
			.Padding(2.0f)
			.BorderImage(FEditorStyle::GetBrush("SCSEditor.TreePanel"))
			[
				SAssignNew(TreeView, STreeView<TSharedPtr<FRigStackEntry>>)
				.TreeItemsSource(&Operators)
				.SelectionMode(ESelectionMode::Multi)
				.OnGenerateRow(this, &SControlRigStackView::MakeTableRowWidget)
				.OnGetChildren(this, &SControlRigStackView::HandleGetChildrenForTree)
				.OnSelectionChanged(this, &SControlRigStackView::OnSelectionChanged)
				.OnContextMenuOpening(this, &SControlRigStackView::CreateContextMenu)
				.ItemHeight(28)
			]
		]
	];

	RefreshTreeView(nullptr);

	if (ControlRigBlueprint.IsValid())
	{
		if (OnVMCompiledHandle.IsValid())
		{
			ControlRigBlueprint->OnVMCompiled().Remove(OnVMCompiledHandle);
		}
		if (OnModelModified.IsValid())
		{
			ControlRigBlueprint->OnModified().Remove(OnModelModified);
		}
		OnVMCompiledHandle = ControlRigBlueprint->OnVMCompiled().AddSP(this, &SControlRigStackView::OnVMCompiled);
		OnModelModified = ControlRigBlueprint->OnModified().AddSP(this, &SControlRigStackView::HandleModifiedEvent);

		if (ControlRigEditor.IsValid())
		{
			if (UControlRig* ControlRig = ControlRigEditor.Pin()->ControlRig)
			{
				OnVMCompiled(ControlRigBlueprint.Get(), ControlRig->VM);
			}
		}
	}

	if (ControlRigEditor.IsValid())
	{
		OnPreviewControlRigUpdatedHandle = ControlRigEditor.Pin()->OnPreviewControlRigUpdated().AddSP(this, &SControlRigStackView::HandlePreviewControlRigUpdated);
	}
}

void SControlRigStackView::OnSelectionChanged(TSharedPtr<FRigStackEntry> Selection, ESelectInfo::Type SelectInfo)
{
	if (bSuspendModelNotifications || bSuspendControllerSelection)
	{
		return;
	}
	TGuardValue<bool> SuspendNotifs(bSuspendModelNotifications, true);

	TArray<TSharedPtr<FRigStackEntry>> SelectedItems = TreeView->GetSelectedItems();
	if (SelectedItems.Num() > 0)
	{
		if (!ControlRigBlueprint.IsValid())
		{
			return;
		}

		UControlRigBlueprintGeneratedClass* GeneratedClass = ControlRigBlueprint->GetControlRigBlueprintGeneratedClass();
		if (GeneratedClass == nullptr)
		{
			return;
		}

		TMap<int32, FName> InstructionIndexToNodeName;
		for (URigVMNode* ModelNode : ControlRigBlueprint->Model->GetNodes())
		{
			if (ModelNode->GetInstructionIndex() != INDEX_NONE)
			{
				InstructionIndexToNodeName.Add(ModelNode->GetInstructionIndex(), ModelNode->GetFName());
			}
		}

		TArray<FName> SelectedNodes;
		for (TSharedPtr<FRigStackEntry>& Entry : SelectedItems)
		{
			const FName* NodeName = InstructionIndexToNodeName.Find(Entry->InstructionIndex);
			if (NodeName)
			{
				SelectedNodes.Add(*NodeName);
			}
		}

		ControlRigBlueprint->Controller->SetNodeSelection(SelectedNodes);
	}
}

void SControlRigStackView::BindCommands()
{
	// create new command
	const FControlRigStackCommands& Commands = FControlRigStackCommands::Get();
	CommandList->MapAction(Commands.FocusOnSelection, FExecuteAction::CreateSP(this, &SControlRigStackView::HandleFocusOnSelectedGraphNode));
}

TSharedRef<ITableRow> SControlRigStackView::MakeTableRowWidget(TSharedPtr<FRigStackEntry> InItem, const TSharedRef<STableViewBase>& OwnerTable)
{
	return InItem->MakeTreeRowWidget(OwnerTable, InItem.ToSharedRef(), CommandList.ToSharedRef(), SharedThis(this));
}

void SControlRigStackView::HandleGetChildrenForTree(TSharedPtr<FRigStackEntry> InItem, TArray<TSharedPtr<FRigStackEntry>>& OutChildren)
{
	OutChildren = InItem->Children;
}

void SControlRigStackView::PopulateStackView(URigVM* InVM)
{
	if (InVM)
	{
		FRigVMInstructionArray Instructions = InVM->GetInstructions();
		const TArray<URigVMNode*>& Nodes = ControlRigBlueprint->Model->GetNodes();
		
		// 1. cache information about instructions/nodes, which will be used later 
		TArray<int32> InstructionIndexToNodeIndex;
		InstructionIndexToNodeIndex.Init(-1, Instructions.Num());
		TMap<FString, FString> NodeNameToDisplayName;
		for (int32 NodeIndex = 0; NodeIndex < Nodes.Num(); NodeIndex++)
		{
			URigVMNode* Node = Nodes[NodeIndex];
			// only struct nodes among all nodes has StaticExecute() that generates actual instructions
			if (URigVMUnitNode* UnitNode = Cast<URigVMUnitNode>(Node))
			{
				int32 InstructionIndex = UnitNode->GetInstructionIndex();
				// only active nodes are assigned a valid instruction index, hence the check here
				if (InstructionIndex >= 0 && InstructionIndex < InstructionIndexToNodeIndex.Num())
				{
					InstructionIndexToNodeIndex[InstructionIndex] = NodeIndex;
				}
				FString DisplayName = UnitNode->GetNodeTitle();
#if WITH_EDITOR
				UScriptStruct* Struct = UnitNode->GetScriptStruct();
				FString MenuDescSuffixMetadata;
				if (Struct)
				{
					Struct->GetStringMetaDataHierarchical(FRigVMStruct::MenuDescSuffixMetaName, &MenuDescSuffixMetadata);
				}
				if (!MenuDescSuffixMetadata.IsEmpty())
				{
					DisplayName = FString::Printf(TEXT("%s %s"), *UnitNode->GetNodeTitle(), *MenuDescSuffixMetadata);
				}
#endif
				// this is needed for name replacement later
				NodeNameToDisplayName.Add(UnitNode->GetName(), DisplayName);
			}
		}

		// 2. replace raw operand names with NodeTitle.PinName/PropertyName.OffsetName
		TArray<FString> Labels = InVM->DumpByteCodeAsTextArray(TArray<int32>(), false, [NodeNameToDisplayName](const FString& RegisterName, const FString& RegisterOffsetName)
		{
			FString NewRegisterName = RegisterName;
			FString NodeName;
			FString PinName;
			if (RegisterName.Split(TEXT("."), &NodeName, &PinName))
			{
				const FString* NodeTitle = NodeNameToDisplayName.Find(NodeName);
				NewRegisterName = FString::Printf(TEXT("%s.%s"), NodeTitle ? **NodeTitle : *NodeName, *PinName);
			}
			FString OperandLabel;
			OperandLabel = NewRegisterName;
			if (!RegisterOffsetName.IsEmpty())
			{
				OperandLabel = FString::Printf(TEXT("%s.%s"), *OperandLabel, *RegisterOffsetName);
			}
			return OperandLabel;
		});

		ensure(Labels.Num() == Instructions.Num());
		
		// 3. replace instruction names with node titles
		for (int32 InstructionIndex = 0; InstructionIndex < Labels.Num(); InstructionIndex++)
		{
			FString Label = Labels[InstructionIndex];
			int32 NodeIndex = InstructionIndexToNodeIndex[InstructionIndex];
			// note that some instructions don't map to a node, hence the check here
			if (NodeIndex >= 0 && NodeIndex < Nodes.Num())
			{
				URigVMNode* Node = Nodes[NodeIndex];
				Label = NodeNameToDisplayName[Node->GetName()];
			}
			// add the entry with the new label to the stack view
			const FRigVMInstruction& Instruction = Instructions[InstructionIndex];
			if (FilterText.IsEmpty() || Label.Contains(FilterText.ToString()))
			{
				TSharedPtr<FRigStackEntry> NewEntry = MakeShared<FRigStackEntry>(InstructionIndex, ERigStackEntry::Operator, InstructionIndex, Instruction.OpCode, Label);
				Operators.Add(NewEntry);
			}
		}
	}
}

void SControlRigStackView::RefreshTreeView(URigVM* InVM)
{
	Operators.Reset();

	// populate the stack with node names/instruction names
	PopulateStackView(InVM);
	
	if (InVM)
	{
		// fill the children from the log
		if (ControlRigEditor.IsValid())
		{
			UControlRig* ControlRig = ControlRigEditor.Pin()->ControlRig;
			if(ControlRig && ControlRig->ControlRigLog)
			{
				const TArray<FControlRigLog::FLogEntry>& LogEntries = ControlRig->ControlRigLog->Entries;
				for (const FControlRigLog::FLogEntry& LogEntry : LogEntries)
				{
					if (Operators.Num() <= LogEntry.InstructionIndex)
					{
						continue;
					}
					int32 ChildIndex = Operators[LogEntry.InstructionIndex]->Children.Num();
					switch (LogEntry.Severity)
					{
						case EMessageSeverity::Info:
						{
							Operators[LogEntry.InstructionIndex]->Children.Add(MakeShared<FRigStackEntry>(ChildIndex, ERigStackEntry::Info, LogEntry.InstructionIndex, ERigVMOpCode::Invalid, LogEntry.Message));
							break;
						}
						case EMessageSeverity::Warning:
						case EMessageSeverity::PerformanceWarning:
						{
							Operators[LogEntry.InstructionIndex]->Children.Add(MakeShared<FRigStackEntry>(ChildIndex, ERigStackEntry::Warning, LogEntry.InstructionIndex, ERigVMOpCode::Invalid, LogEntry.Message));
							break;
						}
						case EMessageSeverity::Error:
						case EMessageSeverity::CriticalError:
						{
							Operators[LogEntry.InstructionIndex]->Children.Add(MakeShared<FRigStackEntry>(ChildIndex, ERigStackEntry::Error, LogEntry.InstructionIndex, ERigVMOpCode::Invalid, LogEntry.Message));
							break;
						}
						default:
						{
							break;
						}
					}
				}
			}
		}
	}

	TreeView->RequestTreeRefresh();
}

TSharedPtr< SWidget > SControlRigStackView::CreateContextMenu()
{
	TArray<TSharedPtr<FRigStackEntry>> SelectedItems = TreeView->GetSelectedItems();
	if(SelectedItems.Num() == 0)
	{
		return nullptr;
	}

	const FControlRigStackCommands& Actions = FControlRigStackCommands::Get();

	const bool CloseAfterSelection = true;
	FMenuBuilder MenuBuilder(CloseAfterSelection, CommandList);
	{
		MenuBuilder.BeginSection("RigStackToolsAction", LOCTEXT("ToolsAction", "Tools"));
		MenuBuilder.AddMenuEntry(Actions.FocusOnSelection);
		MenuBuilder.EndSection();
	}

	return MenuBuilder.MakeWidget();
}

void SControlRigStackView::HandleFocusOnSelectedGraphNode()
{
	OnSelectionChanged(TSharedPtr<FRigStackEntry>(), ESelectInfo::Direct);
	ControlRigEditor.Pin()->ZoomToSelection_Clicked();
}

void SControlRigStackView::OnVMCompiled(UBlueprint* InCompiledBlueprint, URigVM* InCompiledVM)
{
	RefreshTreeView(InCompiledVM);

	if (ControlRigEditor.IsValid() && !OnControlRigInitializedHandle.IsValid())
	{
		UControlRig* ControlRig = ControlRigEditor.Pin()->ControlRig;
		OnControlRigInitializedHandle = ControlRig->OnInitialized_AnyThread().AddSP(this, &SControlRigStackView::HandleControlRigInitializedEvent);
	}
}

void SControlRigStackView::OnFilterTextChanged(const FText& SearchText)
{
	FilterText = SearchText;
	RefreshTreeView(ControlRigEditor.Pin()->ControlRig->GetVM());
}

void SControlRigStackView::HandleModifiedEvent(ERigVMGraphNotifType InNotifType, URigVMGraph* InGraph, UObject* InSubject)
{
	if (bSuspendModelNotifications)
	{
		return;
	}

	switch (InNotifType)
	{
		case ERigVMGraphNotifType::NodeSelected:
		case ERigVMGraphNotifType::NodeDeselected:
		{
			URigVMNode* Node = Cast<URigVMNode>(InSubject);
			if (Node)
			{
				if (Node->GetInstructionIndex() != INDEX_NONE)
				{
					if (Operators.Num() > Node->GetInstructionIndex())
					{
						TGuardValue<bool> SuspendNotifs(bSuspendModelNotifications, true);
						TreeView->SetItemSelection(Operators[Node->GetInstructionIndex()], InNotifType == ERigVMGraphNotifType::NodeSelected, ESelectInfo::Direct);
					}
				}
			}
			break;
		}
		default:
		{
			break;
		}
	}
}

void SControlRigStackView::HandleControlRigInitializedEvent(UControlRig* InControlRig, const EControlRigState InState, const FName& InEventName)
{
	TGuardValue<bool> SuspendControllerSelection(bSuspendControllerSelection, true);

	RefreshTreeView(InControlRig->VM);
	OnSelectionChanged(TSharedPtr<FRigStackEntry>(), ESelectInfo::Direct);

	for (TSharedPtr<FRigStackEntry>& Operator : Operators)
	{
		for (TSharedPtr<FRigStackEntry>& Child : Operator->Children)
		{
			if (Child->EntryType == ERigStackEntry::Warning || Child->EntryType == ERigStackEntry::Error)
			{
				TreeView->SetItemExpansion(Operator, true);
				break;
			}
		}
	}
	
	if (ControlRigEditor.IsValid())
	{
		InControlRig->OnInitialized_AnyThread().Remove(OnControlRigInitializedHandle);
		OnControlRigInitializedHandle.Reset();

		if (UControlRigBlueprint* RigBlueprint = ControlRigEditor.Pin()->GetControlRigBlueprint())
		{
			for (URigVMNode* ModelNode : RigBlueprint->Model->GetNodes())
			{
				if (ModelNode->IsSelected())
				{
					HandleModifiedEvent(ERigVMGraphNotifType::NodeSelected, RigBlueprint->Model, ModelNode);
				}
			}
		}
	}
}

void SControlRigStackView::HandlePreviewControlRigUpdated(FControlRigEditor* InEditor)
{
}

#undef LOCTEXT_NAMESPACE
