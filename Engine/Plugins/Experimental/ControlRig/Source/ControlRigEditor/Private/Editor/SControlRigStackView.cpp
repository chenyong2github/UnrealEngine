// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

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
FRigStackEntry::FRigStackEntry(int32 InEntryIndex, ERigStackEntry::Type InEntryType, int32 InOpIndex, EControlRigOpCode InOpCode, const FName& InName, const FString& InLabel)
	: EntryIndex(InEntryIndex)
	, EntryType(InEntryType)
	, OpIndex(InOpIndex)
	, OpCode(InOpCode)
	, Name(InName)
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
		if (OnGraphNodeSelectionChanged.IsValid())
		{
			ControlRigEditor.Pin()->OnGraphNodeSelectionChanged().Remove(OnGraphNodeSelectionChanged);
		}
		if (OnControlRigInitializedHandle.IsValid() && ControlRigEditor.Pin()->ControlRig)
		{
			ControlRigEditor.Pin()->ControlRig->OnInitialized().Remove(OnControlRigInitializedHandle);
		}
	}
	if (ControlRigBlueprint.IsValid() && OnBlueprintCompiledHandle.IsValid())
	{
		ControlRigBlueprint->OnCompiled().Remove(OnBlueprintCompiledHandle);
	}
}

void SControlRigStackView::Construct( const FArguments& InArgs, TSharedRef<FControlRigEditor> InControlRigEditor)
{
	ControlRigEditor = InControlRigEditor;
	ControlRigBlueprint = ControlRigEditor.Pin()->GetControlRigBlueprint();
	Graph = Cast<UControlRigGraph>(ControlRigBlueprint->GetLastEditedUberGraph());
	CommandList = MakeShared<FUICommandList>();
	bSelecting = false;

	OnGraphNodeSelectionChanged = InControlRigEditor->OnGraphNodeSelectionChanged().AddSP(this, &SControlRigStackView::HandleGraphSelectionChanged);

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

	RefreshTreeView();

	if (ControlRigBlueprint.IsValid())
	{
		if(OnBlueprintCompiledHandle.IsValid())
		{
			ControlRigBlueprint->OnCompiled().Remove(OnBlueprintCompiledHandle);
		}
		OnBlueprintCompiledHandle = ControlRigBlueprint->OnCompiled().AddSP(this, &SControlRigStackView::OnBlueprintCompiled);
	}
}

void SControlRigStackView::HandleGraphSelectionChanged(const TSet<UObject*>& SelectedNodes)
{
	if (bSelecting)
	{
		return;
	}

	{
		TGuardValue<bool> SelectingGuard(bSelecting, true);
		TreeView->ClearSelection();
	}

	if (SelectedNodes.Num() > 0)
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

		TMap<FName, const UControlRigGraphNode*> SelectedNodeNameMap;
		for (const UObject* SelectedNode : SelectedNodes)
		{
			const UControlRigGraphNode* RigNode = Cast<const UControlRigGraphNode>(SelectedNode);
			if (RigNode)
			{
				SelectedNodeNameMap.Add(RigNode->GetPropertyName(), RigNode);
			}
		}


		TGuardValue<bool> SelectingGuard(bSelecting, true);
		for (const TSharedPtr<FRigStackEntry>& Entry : Operators)
		{
			if (SelectedNodeNameMap.Contains(Entry->Name))
			{
				TreeView->SetItemSelection(Entry, true, ESelectInfo::Direct);
			}
		}
	}
}

void SControlRigStackView::OnSelectionChanged(TSharedPtr<FRigStackEntry> Selection, ESelectInfo::Type SelectInfo)
{
	if (bSelecting)
	{
		return;
	}

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

		UControlRig* ControlRigCDO = CastChecked<UControlRig>(GeneratedClass->ClassDefaultObject);
		TArray<FString> SelectedNodes;
		for (TSharedPtr<FRigStackEntry>& Entry : SelectedItems)
		{
			if (Entry->OpIndex >= ControlRigCDO->Operators.Num())
			{
				return;
			}

			FControlRigOperator& Operator = ControlRigCDO->Operators[Entry->OpIndex];
			if (Operator.OpCode == EControlRigOpCode::Exec)
			{
				FString PropertyPath = Operator.CachedPropertyPath1.ToString();
				SelectedNodes.Add(PropertyPath);
			}
		}

		TGuardValue<bool> SelectingGuard(bSelecting, true);
		ControlRigEditor.Pin()->SetSelectedNodes(SelectedNodes);
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

void SControlRigStackView::RefreshTreeView(UControlRig* ControlRig)
{
	Operators.Reset();

	if (ControlRigBlueprint.IsValid())
	{
		UControlRigBlueprintGeneratedClass* GeneratedClass = ControlRigBlueprint->GetControlRigBlueprintGeneratedClass();
		if (GeneratedClass)
		{
			TMap<FName, int32> UnitToOperatorIndex;

			for (int32 OperatorIndex=0;OperatorIndex< GeneratedClass->Operators.Num();OperatorIndex++)
			{
				FControlRigOperator Operator = GeneratedClass->Operators[OperatorIndex];
				switch (Operator.OpCode)
				{
					case EControlRigOpCode::Exec:
					{
						FName UnitPath = *Operator.CachedPropertyPath1.ToString();
						FString OperatorLabel;

						UStructProperty * StructProperty = Cast<UStructProperty>(GeneratedClass->FindPropertyByName(UnitPath));
						if (StructProperty)
						{
							if (StructProperty->Struct->IsChildOf(FRigUnit::StaticStruct()))
							{
								StructProperty->Struct->GetStringMetaDataHierarchical(UControlRig::DisplayNameMetaName, &OperatorLabel);
								if (OperatorLabel.IsEmpty())
								{
									OperatorLabel = FName::NameToDisplayString(StructProperty->Struct->GetFName().ToString(), false);
								}

								if (ControlRig)
								{
									const FRigUnit* UnitPtr = StructProperty->ContainerPtrToValuePtr<FRigUnit>(ControlRig);
									if (UnitPtr)
									{
										FString UnitLabel = UnitPtr->GetUnitLabel();
										if (!UnitLabel.IsEmpty())
										{
											OperatorLabel = UnitLabel;
										}
									}
								}
							}
						}

						if (OperatorLabel.IsEmpty())
						{
							OperatorLabel = UnitPath.ToString();
						}

						TSharedPtr<FRigStackEntry> NewEntry = MakeShared<FRigStackEntry>(Operators.Num(), ERigStackEntry::Operator, OperatorIndex, Operator.OpCode, UnitPath, OperatorLabel);
						UnitToOperatorIndex.Add(UnitPath, Operators.Num());
						Operators.Add(NewEntry);
						break;
					}
					default:
					{
						break;
					}
				}
			}

			// fill the children from the log
			if (ControlRig)
			{
				if(ControlRig->ControlRigLog)
				{
					const TArray<FControlRigLog::FLogEntry>& LogEntries = ControlRig->ControlRigLog->Entries;
					for (const FControlRigLog::FLogEntry& LogEntry : LogEntries)
					{
						if (!UnitToOperatorIndex.Contains(LogEntry.Unit))
						{
							continue;
						}
						int32 OperatorIndex = UnitToOperatorIndex.FindChecked(LogEntry.Unit);
						int32 ChildIndex = Operators[OperatorIndex]->Children.Num();
						switch (LogEntry.Severity)
						{
						case EMessageSeverity::Info:
						{
							Operators[OperatorIndex]->Children.Add(MakeShared<FRigStackEntry>(ChildIndex, ERigStackEntry::Info, OperatorIndex, EControlRigOpCode::Invalid, LogEntry.Unit, LogEntry.Message));
							break;
						}
						case EMessageSeverity::Warning:
						case EMessageSeverity::PerformanceWarning:
						{
							Operators[OperatorIndex]->Children.Add(MakeShared<FRigStackEntry>(ChildIndex, ERigStackEntry::Warning, OperatorIndex, EControlRigOpCode::Invalid, LogEntry.Unit, LogEntry.Message));
							break;
						}
						case EMessageSeverity::Error:
						case EMessageSeverity::CriticalError:
						{
							Operators[OperatorIndex]->Children.Add(MakeShared<FRigStackEntry>(ChildIndex, ERigStackEntry::Error, OperatorIndex, EControlRigOpCode::Invalid, LogEntry.Unit, LogEntry.Message));
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
	}

	TreeView->RequestTreeRefresh();
}

TSharedPtr< SWidget > SControlRigStackView::CreateContextMenu()
{
	const FControlRigStackCommands& Actions = FControlRigStackCommands::Get();

	TArray<TSharedPtr<FRigStackEntry>> SelectedItems = TreeView->GetSelectedItems();

	const bool CloseAfterSelection = true;
	FMenuBuilder MenuBuilder(CloseAfterSelection, CommandList);
	{
		MenuBuilder.BeginSection("RigStackToolsAction", LOCTEXT("ToolsAction", "Tools"));
		if (SelectedItems.Num() > 0)
		{
			MenuBuilder.AddMenuEntry(Actions.FocusOnSelection);
		}
		MenuBuilder.EndSection();
	}

	return MenuBuilder.MakeWidget();
}

void SControlRigStackView::HandleFocusOnSelectedGraphNode()
{
	OnSelectionChanged(TSharedPtr<FRigStackEntry>(), ESelectInfo::Direct);
	ControlRigEditor.Pin()->ZoomToSelection_Clicked();
}

void SControlRigStackView::OnBlueprintCompiled(UBlueprint* InCompiledBlueprint)
{
	UControlRig* ControlRig = ControlRigEditor.Pin()->ControlRig;
	if (ControlRig == nullptr)
	{
		return;
	}

	RefreshTreeView(ControlRig);
	OnSelectionChanged(TSharedPtr<FRigStackEntry>(), ESelectInfo::Direct);

	if (ControlRigEditor.IsValid())
	{
		OnControlRigInitializedHandle = ControlRig->OnInitialized().AddSP(this, &SControlRigStackView::OnControlRigInitialized);
	}
}

void SControlRigStackView::OnControlRigInitialized(UControlRig* ControlRig, EControlRigState State)
{
	RefreshTreeView(ControlRig);
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
}

#undef LOCTEXT_NAMESPACE
