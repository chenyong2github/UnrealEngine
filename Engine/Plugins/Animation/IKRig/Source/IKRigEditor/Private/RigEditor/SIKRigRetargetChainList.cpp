// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigEditor/SIKRigRetargetChainList.h"

#include "RigEditor/IKRigEditorStyle.h"
#include "RigEditor/IKRigToolkit.h"
#include "RigEditor/IKRigEditorController.h"

#include "Framework/Commands/UICommandList.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "IPersonaToolkit.h"
#include "SKismetInspector.h"
#include "Dialogs/Dialogs.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "SPositiveActionButton.h"

#define LOCTEXT_NAMESPACE "SIKRigRetargetChains"

static const FName ColumnId_ChainNameLabel( "Chain Name" );
static const FName ColumnId_ChainStartLabel( "Start Bone" );
static const FName ColumnId_ChainEndLabel( "End Bone" );
static const FName ColumnId_IKGoalLabel( "IK Goal" );

TSharedRef<ITableRow> FRetargetChainElement::MakeListRowWidget(
	const TSharedRef<STableViewBase>& InOwnerTable,
	TSharedRef<FRetargetChainElement> InChainElement,
	TSharedPtr<SIKRigRetargetChainList> InChainList)
{
	return SNew(SIKRigRetargetChainRow, InOwnerTable, InChainElement, InChainList);
}

void SIKRigRetargetChainRow::Construct(
	const FArguments& InArgs,
	const TSharedRef<STableViewBase>& InOwnerTableView,
	TSharedRef<FRetargetChainElement> InChainElement,
	TSharedPtr<SIKRigRetargetChainList> InChainList)
{
	ChainElement = InChainElement;
	ChainList = InChainList;

	// generate list of goals
	// NOTE: cannot just use literal "None" because Combobox considers that a "null" entry and will discard it from the list.
	GoalOptions.Add(MakeShareable(new FString("None")));
	const UIKRigDefinition* IKRigAsset = InChainList->EditorController.Pin()->AssetController->GetAsset();
	const TArray<UIKRigEffectorGoal*>& AssetGoals =  IKRigAsset->GetGoalArray();
	for (const UIKRigEffectorGoal* Goal : AssetGoals)
	{
		GoalOptions.Add(MakeShareable(new FString(Goal->GoalName.ToString())));
	}

	SMultiColumnTableRow< TSharedPtr<FRetargetChainElement> >::Construct( FSuperRowType::FArguments(), InOwnerTableView );
}

TSharedRef<SWidget> SIKRigRetargetChainRow::GenerateWidgetForColumn(const FName& ColumnName)
{
	if (ColumnName == ColumnId_ChainNameLabel)
	{
		TSharedRef<SWidget> ChainWidget =
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Center)
		.Padding(3.0f, 1.0f)
		[
			SNew(SEditableTextBox)
			.Text(FText::FromName(ChainElement.Pin()->ChainName))
			.Font(FEditorStyle::GetFontStyle(TEXT("BoldFont")))
			.OnTextCommitted(this, &SIKRigRetargetChainRow::OnRenameChain)
		];
		return ChainWidget;
	}

	if (ColumnName == ColumnId_ChainStartLabel)
	{
		TSharedRef<SWidget> StartWidget =
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Center)
		.Padding(3.0f, 1.0f)
		[
			SNew(SComboBox<FName>)
			.OptionsSource(&ChainList.Pin()->EditorController.Pin()->AssetController->GetIKRigSkeleton().BoneNames)
			.OnGenerateWidget(this, &SIKRigRetargetChainRow::MakeBoneComboEntryWidget)
			.OnSelectionChanged(this, &SIKRigRetargetChainRow::OnStartBoneComboSelectionChanged)
			[
				SNew(STextBlock)
				.Text(this, &SIKRigRetargetChainRow::GetStartBoneName)
			]
		];
		return StartWidget;
	}

	if (ColumnName == ColumnId_ChainEndLabel)
	{
		TSharedRef<SWidget> EndWidget =
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Center)
		.Padding(3.0f, 1.0f)
		[
			SNew(SComboBox<FName>)
			.OptionsSource(&ChainList.Pin()->EditorController.Pin()->AssetController->GetIKRigSkeleton().BoneNames)
			.OnGenerateWidget(this, &SIKRigRetargetChainRow::MakeBoneComboEntryWidget)
			.OnSelectionChanged(this, &SIKRigRetargetChainRow::OnEndBoneComboSelectionChanged)
			[
				SNew(STextBlock)
				.Text(this, &SIKRigRetargetChainRow::GetEndBoneName)
			]
		];
		return EndWidget;
	}
	else
	{
		// ColumnId_IKGoalLabel
		
		TSharedRef<SWidget> GoalWidget =
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Center)
		.Padding(3.0f, 1.0f)
		[
			SNew(SComboBox<TSharedPtr<FString>>)
			.OptionsSource(&GoalOptions)
			.OnGenerateWidget(this, &SIKRigRetargetChainRow::MakeGoalComboEntryWidget)
			.OnSelectionChanged(this, &SIKRigRetargetChainRow::OnGoalComboSelectionChanged)
			[
				SNew(STextBlock)
				.Text(this, &SIKRigRetargetChainRow::GetGoalName)
			]
		];
		return GoalWidget;
	}
}

TSharedRef<SWidget> SIKRigRetargetChainRow::MakeBoneComboEntryWidget(FName InItem) const
{
	return SNew(STextBlock).Text(FText::FromName(InItem));
}

TSharedRef<SWidget> SIKRigRetargetChainRow::MakeGoalComboEntryWidget(TSharedPtr<FString> InItem) const
{
	return SNew(STextBlock).Text(FText::FromString(*InItem.Get()));
}

void SIKRigRetargetChainRow::OnStartBoneComboSelectionChanged(FName InName, ESelectInfo::Type SelectInfo)
{
	const TSharedPtr<FIKRigEditorController> Controller = ChainList.Pin()->EditorController.Pin();
	if (!Controller.IsValid())
	{
		return; 
	}
	
	Controller->AssetController->SetRetargetChainStartBone(ChainElement.Pin()->ChainName, InName);
	ChainList.Pin()->RefreshView();
}

FText SIKRigRetargetChainRow::GetStartBoneName() const
{
	const TSharedPtr<FIKRigEditorController> Controller = ChainList.Pin()->EditorController.Pin();
	if (!Controller.IsValid())
	{
		return FText::GetEmpty();
	}
	
	const FName StartBoneName = Controller->AssetController->GetRetargetChainStartBone(ChainElement.Pin()->ChainName);
	return FText::FromName(StartBoneName);
}

FText SIKRigRetargetChainRow::GetEndBoneName() const
{	
	const TSharedPtr<FIKRigEditorController> Controller = ChainList.Pin()->EditorController.Pin();
	if (!Controller.IsValid())
	{
		return FText::GetEmpty();
	}
	
	const FName EndBoneName = Controller->AssetController->GetRetargetChainEndBone(ChainElement.Pin()->ChainName);
	return FText::FromName(EndBoneName);
}

FText SIKRigRetargetChainRow::GetGoalName() const
{
	const TSharedPtr<FIKRigEditorController> Controller = ChainList.Pin()->EditorController.Pin();
	if (!Controller.IsValid())
	{
		return FText::GetEmpty();
	}
	
	const FName GoalName = Controller->AssetController->GetRetargetChainGoal(ChainElement.Pin()->ChainName);
	return FText::FromName(GoalName);
}

void SIKRigRetargetChainRow::OnEndBoneComboSelectionChanged(FName InName, ESelectInfo::Type SelectInfo)
{
	const TSharedPtr<FIKRigEditorController> Controller = ChainList.Pin()->EditorController.Pin();
	if (!Controller.IsValid())
	{
		return; 
	}
	
	Controller->AssetController->SetRetargetChainEndBone(ChainElement.Pin()->ChainName, InName);
	ChainList.Pin()->RefreshView();
}

void SIKRigRetargetChainRow::OnGoalComboSelectionChanged(TSharedPtr<FString> InGoalName, ESelectInfo::Type SelectInfo)
{
	const TSharedPtr<FIKRigEditorController> Controller = ChainList.Pin()->EditorController.Pin();
	if (!Controller.IsValid())
	{
		return; 
	}

	Controller->AssetController->SetRetargetChainGoal(ChainElement.Pin()->ChainName, FName(*InGoalName.Get()));
	ChainList.Pin()->RefreshView();
}

void SIKRigRetargetChainRow::OnRenameChain(const FText& InText, ETextCommit::Type) const
{
	const TSharedPtr<FIKRigEditorController> Controller = ChainList.Pin()->EditorController.Pin();
	if (!Controller.IsValid())
	{
		return; 
	}

	const FName OldName = ChainElement.Pin()->ChainName;
	const FName NewName = FName(*InText.ToString());
	ChainElement.Pin()->ChainName = Controller->AssetController->RenameRetargetChain(OldName, NewName);
	ChainList.Pin()->RefreshView();
}

void SIKRigRetargetChainList::Construct(const FArguments& InArgs, TSharedRef<FIKRigEditorController> InEditorController)
{
	EditorController = InEditorController;
	EditorController.Pin()->RetargetingView = SharedThis(this);
	
	CommandList = MakeShared<FUICommandList>();

	ChildSlot
    [
        SNew(SVerticalBox)
        +SVerticalBox::Slot()
        .AutoHeight()
        .VAlign(VAlign_Top)
        [
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Center)
			.Padding(3.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("RetargetRootLabel", "Retarget Root:"))
				.TextStyle(FEditorStyle::Get(), "NormalText")
			]
				
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Center)
			.Padding(3.0f, 0.0f)
			[
				SAssignNew(RetargetRootTextBox, SEditableTextBox)
				.Text(FText::FromName(InEditorController->AssetController->GetRetargetRoot()))
				.Font(FEditorStyle::GetFontStyle(TEXT("BoldFont")))
				.IsReadOnly(true)
			]
        ]

        +SVerticalBox::Slot()
		[
			SAssignNew(ListView, SRetargetChainListViewType )
			.SelectionMode(ESelectionMode::Single)
			.IsEnabled(this, &SIKRigRetargetChainList::IsAddChainEnabled)
			.ListItemsSource( &ListViewItems )
			.OnGenerateRow( this, &SIKRigRetargetChainList::MakeListRowWidget )
			.OnMouseButtonClick(this, &SIKRigRetargetChainList::OnItemClicked)
			.ItemHeight( 22.0f )
			.HeaderRow
			(
				SNew( SHeaderRow )
				+ SHeaderRow::Column( ColumnId_ChainNameLabel )
				.DefaultLabel( LOCTEXT( "ChainNameColumnLabel", "Chain Name" ) )

				+ SHeaderRow::Column( ColumnId_ChainStartLabel )
				.DefaultLabel( LOCTEXT( "ChainStartColumnLabel", "Start Bone" ) )

				+ SHeaderRow::Column( ColumnId_ChainEndLabel )
				.DefaultLabel( LOCTEXT( "ChainEndColumnLabel", "End Bone" ) )

				+ SHeaderRow::Column( ColumnId_IKGoalLabel )
				.DefaultLabel( LOCTEXT( "IKGoalColumnLabel", "IK Goal" ) )
			)
		]
    ];

	RefreshView();
}

FName SIKRigRetargetChainList::GetSelectedChain()
{
	TArray<TSharedPtr<FRetargetChainElement>> SelectedItems = ListView->GetSelectedItems();
	if (SelectedItems.IsEmpty())
	{
		return NAME_None;
	}
	return SelectedItems[0].Get()->ChainName;
}

bool SIKRigRetargetChainList::IsAddChainEnabled() const
{
	const TSharedPtr<FIKRigEditorController> Controller = EditorController.Pin();
	if (!Controller.IsValid())
	{
		return false;
	}
	
	if (UIKRigController* AssetController = Controller->AssetController)
	{
		if (AssetController->GetIKRigSkeleton().BoneNames.Num() > 0)
		{
			return true;
		}
	}
	
	return false;
}

void SIKRigRetargetChainList::RefreshView()
{
	const TSharedPtr<FIKRigEditorController> Controller = EditorController.Pin();
	if (!Controller.IsValid())
	{
		return;
	}
	
	// refresh retarget root
	RetargetRootTextBox.Get()->SetText(FText::FromName(Controller->AssetController->GetRetargetRoot()));

	// refresh list of chains
	ListViewItems.Reset();
	const TArray<FBoneChain>& Chains = Controller->AssetController->GetRetargetChains();
	for (const FBoneChain& Chain : Chains)
	{
		TSharedPtr<FRetargetChainElement> ChainItem = FRetargetChainElement::Make(Chain.ChainName);
		ListViewItems.Add(ChainItem);
	}

	// select first item if none others selected
	if (ListViewItems.Num() > 0 && ListView->GetNumItemsSelected() == 0)
	{
		ListView->SetSelection(ListViewItems[0]);
	}

	ListView->RequestListRefresh();
}

TSharedRef<ITableRow> SIKRigRetargetChainList::MakeListRowWidget(
	TSharedPtr<FRetargetChainElement> InElement,
    const TSharedRef<STableViewBase>& OwnerTable)
{
	return InElement->MakeListRowWidget(
		OwnerTable,
		InElement.ToSharedRef(),
		SharedThis(this));
}

void SIKRigRetargetChainList::OnItemClicked(TSharedPtr<FRetargetChainElement> InItem)
{
	EditorController.Pin()->SetLastSelectedType(EIKRigSelectionType::RetargetChains);
}

FReply SIKRigRetargetChainList::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();

	// handle deleting selected chain
	if (Key == EKeys::Delete)
	{
		TArray<TSharedPtr<FRetargetChainElement>> SelectedItems = ListView->GetSelectedItems();
		if (SelectedItems.IsEmpty())
		{
			return FReply::Unhandled();
		}
		
		const TSharedPtr<FIKRigEditorController> Controller = EditorController.Pin();
		if (!Controller.IsValid())
		{
			return FReply::Unhandled();
		}

		UIKRigController* AssetController = Controller->AssetController;
		AssetController->RemoveRetargetChain(SelectedItems[0]->ChainName);

		RefreshView();
		
		return FReply::Handled();
	}
	
	return FReply::Unhandled();
}

#undef LOCTEXT_NAMESPACE
