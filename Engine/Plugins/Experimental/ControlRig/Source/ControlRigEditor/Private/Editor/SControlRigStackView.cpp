// Copyright Epic Games, Inc. All Rights Reserved.

#include "SControlRigStackView.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SScrollBar.h"
#include "ControlRigEditor.h"
#include "ControlRigEditorStyle.h"
#include "ControlRigStackCommands.h"
#include "ControlRig.h"
#include "ControlRigBlueprintGeneratedClass.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "EditorStyleSet.h"

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
	FString IndexStr = FString::FromInt(WeakStackEntry.Pin()->EntryIndex + 1) + TEXT(".");
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
		if (OnPreviewControlRigUpdatedHandle.IsValid())
		{
			ControlRigEditor.Pin()->ControlRig->OnInitialized_AnyThread().Remove(OnControlRigInitializedHandle);
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
			+ SVerticalBox::Slot()
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

void SControlRigStackView::RefreshTreeView(URigVM* InVM)
{
	Operators.Reset();

	if (InVM)
	{
		FRigVMInstructionArray Instructions = InVM->GetInstructions();

		for (int32 InstructionIndex=0; InstructionIndex < Instructions.Num(); InstructionIndex++)
		{
			FRigVMInstruction Instruction = Instructions[InstructionIndex];
			switch (Instruction.OpCode)
			{
				case ERigVMOpCode::Execute_0_Operands:
				case ERigVMOpCode::Execute_1_Operands:
				case ERigVMOpCode::Execute_2_Operands:
				case ERigVMOpCode::Execute_3_Operands:
				case ERigVMOpCode::Execute_4_Operands:
				case ERigVMOpCode::Execute_5_Operands:
				case ERigVMOpCode::Execute_6_Operands:
				case ERigVMOpCode::Execute_7_Operands:
				case ERigVMOpCode::Execute_8_Operands:
				case ERigVMOpCode::Execute_9_Operands:
				case ERigVMOpCode::Execute_10_Operands:
				case ERigVMOpCode::Execute_11_Operands:
				case ERigVMOpCode::Execute_12_Operands:
				case ERigVMOpCode::Execute_13_Operands:
				case ERigVMOpCode::Execute_14_Operands:
				case ERigVMOpCode::Execute_15_Operands:
				case ERigVMOpCode::Execute_16_Operands:
				case ERigVMOpCode::Execute_17_Operands:
				case ERigVMOpCode::Execute_18_Operands:
				case ERigVMOpCode::Execute_19_Operands:
				case ERigVMOpCode::Execute_20_Operands:
				case ERigVMOpCode::Execute_21_Operands:
				case ERigVMOpCode::Execute_22_Operands:
				case ERigVMOpCode::Execute_23_Operands:
				case ERigVMOpCode::Execute_24_Operands:
				case ERigVMOpCode::Execute_25_Operands:
				case ERigVMOpCode::Execute_26_Operands:
				case ERigVMOpCode::Execute_27_Operands:
				case ERigVMOpCode::Execute_28_Operands:
				case ERigVMOpCode::Execute_29_Operands:
				case ERigVMOpCode::Execute_30_Operands:
				case ERigVMOpCode::Execute_31_Operands:
				case ERigVMOpCode::Execute_32_Operands:
				case ERigVMOpCode::Execute_33_Operands:
				case ERigVMOpCode::Execute_34_Operands:
				case ERigVMOpCode::Execute_35_Operands:
				case ERigVMOpCode::Execute_36_Operands:
				case ERigVMOpCode::Execute_37_Operands:
				case ERigVMOpCode::Execute_38_Operands:
				case ERigVMOpCode::Execute_39_Operands:
				case ERigVMOpCode::Execute_40_Operands:
				case ERigVMOpCode::Execute_41_Operands:
				case ERigVMOpCode::Execute_42_Operands:
				case ERigVMOpCode::Execute_43_Operands:
				case ERigVMOpCode::Execute_44_Operands:
				case ERigVMOpCode::Execute_45_Operands:
				case ERigVMOpCode::Execute_46_Operands:
				case ERigVMOpCode::Execute_47_Operands:
				case ERigVMOpCode::Execute_48_Operands:
				case ERigVMOpCode::Execute_49_Operands:
				case ERigVMOpCode::Execute_50_Operands:
				case ERigVMOpCode::Execute_51_Operands:
				case ERigVMOpCode::Execute_52_Operands:
				case ERigVMOpCode::Execute_53_Operands:
				case ERigVMOpCode::Execute_54_Operands:
				case ERigVMOpCode::Execute_55_Operands:
				case ERigVMOpCode::Execute_56_Operands:
				case ERigVMOpCode::Execute_57_Operands:
				case ERigVMOpCode::Execute_58_Operands:
				case ERigVMOpCode::Execute_59_Operands:
				case ERigVMOpCode::Execute_60_Operands:
				case ERigVMOpCode::Execute_61_Operands:
				case ERigVMOpCode::Execute_62_Operands:
				case ERigVMOpCode::Execute_63_Operands:
				case ERigVMOpCode::Execute_64_Operands:
				{
					FRigVMExecuteOp Op = InVM->ByteCode.GetOpAt<FRigVMExecuteOp>(Instruction);
					FString OperatorLabel = InVM->GetRigVMFunctionName(Op.FunctionIndex);
					TSharedPtr<FRigStackEntry> NewEntry = MakeShared<FRigStackEntry>(Operators.Num(), ERigStackEntry::Operator, InstructionIndex, Instruction.OpCode, OperatorLabel);
					Operators.Add(NewEntry);
					break;
				}
				default:
				{
					FString OperatorLabel = StaticEnum<ERigVMOpCode>()->GetNameStringByValue((int64)Instruction.OpCode);
					TSharedPtr<FRigStackEntry> NewEntry = MakeShared<FRigStackEntry>(Operators.Num(), ERigStackEntry::Operator, InstructionIndex, Instruction.OpCode, OperatorLabel);
					Operators.Add(NewEntry);
					break;
				}
			}
		}

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

void SControlRigStackView::HandleControlRigInitializedEvent(UControlRig* InControlRig, const EControlRigState InState)
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
