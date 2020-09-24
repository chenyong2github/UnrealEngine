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
		TArray<FString> Labels = InVM->DumpByteCodeAsTextArray(TArray<int32>(), false);
		FRigVMInstructionArray Instructions = InVM->GetInstructions();
		ensure(Labels.Num() == Instructions.Num());

		for (int32 InstructionIndex = 0; InstructionIndex < Labels.Num(); InstructionIndex++)
		{
			const FRigVMInstruction& Instruction = Instructions[InstructionIndex];
			FString Label = Labels[InstructionIndex];
			FString Left, Right;
			if (Label.Split(TEXT("("), &Left, &Right))
			{
				Label = FString::Printf(TEXT("%s(...)"), *Left);
			}
			
			TSharedPtr<FRigStackEntry> NewEntry = MakeShared<FRigStackEntry>(Operators.Num(), ERigStackEntry::Operator, InstructionIndex, Instruction.OpCode, Label);
			Operators.Add(NewEntry);
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
