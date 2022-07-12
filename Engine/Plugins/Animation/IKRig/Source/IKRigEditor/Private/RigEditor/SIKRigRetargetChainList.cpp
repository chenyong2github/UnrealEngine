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
#include "SSearchableComboBox.h"
#include "BoneSelectionWidget.h"
#include "Engine/SkeletalMesh.h"

#define LOCTEXT_NAMESPACE "SIKRigRetargetChains"

static const FName ColumnId_ChainNameLabel( "Chain Name" );
static const FName ColumnId_ChainStartLabel( "Start Bone" );
static const FName ColumnId_ChainEndLabel( "End Bone" );
static const FName ColumnId_IKGoalLabel( "IK Goal" );
static const FName ColumnId_DeleteChainLabel( "Delete Chain" );

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
			.Font(FAppStyle::GetFontStyle(TEXT("BoldFont")))
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
			SNew(SBoneSelectionWidget)
			.OnBoneSelectionChanged(this, &SIKRigRetargetChainRow::OnStartBoneComboSelectionChanged)
			.OnGetSelectedBone(this, &SIKRigRetargetChainRow::GetStartBoneName)
			.OnGetReferenceSkeleton(this, &SIKRigRetargetChainRow::GetReferenceSkeleton)
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
			SNew(SBoneSelectionWidget)
			.OnBoneSelectionChanged(this, &SIKRigRetargetChainRow::OnEndBoneComboSelectionChanged)
			.OnGetSelectedBone(this, &SIKRigRetargetChainRow::GetEndBoneName)
			.OnGetReferenceSkeleton(this, &SIKRigRetargetChainRow::GetReferenceSkeleton)
		];
		return EndWidget;
	}

	if (ColumnName == ColumnId_IKGoalLabel)
	{
		TSharedRef<SWidget> GoalWidget =
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Center)
		.Padding(3.0f, 1.0f)
		[
			SNew(SSearchableComboBox)
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

	// ColumnName == ColumnId_DeleteChainLabel
	{
		TSharedRef<SWidget> DeleteWidget =
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.HAlign(HAlign_Right)
		.VAlign(VAlign_Center)
		.Padding(3)
		[
			SNew(SButton)
			.ToolTipText(LOCTEXT("DeleteChain", "Remove retarget bone chain from list."))
			.OnClicked_Lambda([this]() -> FReply
			{
				const TSharedPtr<FIKRigEditorController> Controller = ChainList.Pin()->EditorController.Pin();
				if (!Controller.IsValid())
				{
					return FReply::Unhandled();
				}

				UIKRigController* AssetController = Controller->AssetController;
				AssetController->RemoveRetargetChain(ChainElement.Pin()->ChainName);

				ChainList.Pin()->RefreshView();
				return FReply::Handled();
			})
			.Content()
			[
				SNew(SImage)
				.Image(FAppStyle::Get().GetBrush("Icons.Delete"))
				.ColorAndOpacity(FSlateColor::UseForeground())
			]
		];
		return DeleteWidget;
	}
}

TSharedRef<SWidget> SIKRigRetargetChainRow::MakeGoalComboEntryWidget(TSharedPtr<FString> InItem) const
{
	return SNew(STextBlock).Text(FText::FromString(*InItem.Get()));
}

void SIKRigRetargetChainRow::OnStartBoneComboSelectionChanged(FName InName) const
{
	const TSharedPtr<FIKRigEditorController> Controller = ChainList.Pin()->EditorController.Pin();
	if (!Controller.IsValid())
	{
		return; 
	}
	
	Controller->AssetController->SetRetargetChainStartBone(ChainElement.Pin()->ChainName, InName);
	ChainList.Pin()->RefreshView();
}

FName SIKRigRetargetChainRow::GetStartBoneName(bool& bMultipleValues) const
{
	const TSharedPtr<FIKRigEditorController> Controller = ChainList.Pin()->EditorController.Pin();
	if (!Controller.IsValid())
	{
		return NAME_None;
	}

	bMultipleValues = false;
	return Controller->AssetController->GetRetargetChainStartBone(ChainElement.Pin()->ChainName);
}

FName SIKRigRetargetChainRow::GetEndBoneName(bool& bMultipleValues) const
{	
	const TSharedPtr<FIKRigEditorController> Controller = ChainList.Pin()->EditorController.Pin();
	if (!Controller.IsValid())
	{
		return NAME_None;
	}

	bMultipleValues = false;
	return Controller->AssetController->GetRetargetChainEndBone(ChainElement.Pin()->ChainName);
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

void SIKRigRetargetChainRow::OnEndBoneComboSelectionChanged(FName InName) const
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

void SIKRigRetargetChainRow::OnRenameChain(const FText& InText, ETextCommit::Type CommitType) const
{
	const TSharedPtr<FIKRigEditorController> Controller = ChainList.Pin()->EditorController.Pin();
	if (!Controller.IsValid())
	{
		return; 
	}

	const FName OldName = ChainElement.Pin()->ChainName;
	const FName NewName = FName(*InText.ToString());
	if (OldName == NewName)
	{
		// most reliable way to catch multiple commits
		return;
	}
	
	ChainElement.Pin()->ChainName = Controller->AssetController->RenameRetargetChain(OldName, NewName);
	ChainList.Pin()->RefreshView();
}

const FReferenceSkeleton& SIKRigRetargetChainRow::GetReferenceSkeleton() const
{
	static const FReferenceSkeleton DummySkeleton;
	
	const TSharedPtr<FIKRigEditorController> Controller = ChainList.Pin()->EditorController.Pin();
	if (!Controller.IsValid())
	{
		return DummySkeleton; 
	}

	USkeletalMesh* SkeletalMesh = Controller->AssetController->GetAsset()->GetPreviewMesh();
	if (SkeletalMesh == nullptr)
	{
		return DummySkeleton;
	}

	return SkeletalMesh->GetRefSkeleton();
}

void SIKRigRetargetChainList::Construct(const FArguments& InArgs, TSharedRef<FIKRigEditorController> InEditorController)
{
	EditorController = InEditorController;
	EditorController.Pin()->SetRetargetingView(SharedThis(this));
	
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
			.Padding(3.0f, 3.0f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("RetargetRootLabel", "Retarget Root:"))
				.TextStyle(FAppStyle::Get(), "NormalText")
			]
				
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Center)
			.Padding(3.0f, 3.0f)
			[
				SAssignNew(RetargetRootTextBox, SEditableTextBox)
				.Text(FText::FromName(InEditorController->AssetController->GetRetargetRoot()))
				.Font(FAppStyle::GetFontStyle(TEXT("BoldFont")))
				.IsReadOnly(true)
			]
        ]
        
		+ SVerticalBox::Slot()
		.AutoHeight()
		.VAlign(VAlign_Top)
		.HAlign(HAlign_Left)
		[
			SNew(SPositiveActionButton)
			.Icon(FAppStyle::Get().GetBrush("Icons.Plus"))
			.Text(LOCTEXT("AddNewChainLabel", "Add New Chain"))
			.ToolTipText(LOCTEXT("AddNewChainToolTip", "Add a new retarget bone chain."))
			.OnClicked_Lambda([this]()
			{
				const UIKRigController* Controller = EditorController.Pin()->AssetController;
				static FText NewChainText = LOCTEXT("NewRetargetChainLabel", "NewRetargetChain");
				static FName NewChainName = FName(*NewChainText.ToString());
				Controller->AddRetargetChain(NewChainName, NAME_None, NAME_None);
				RefreshView();
				return FReply::Handled();
			})
		]

        +SVerticalBox::Slot()
		[
			SAssignNew(ListView, SRetargetChainListViewType )
			.SelectionMode(ESelectionMode::Single)
			.IsEnabled(this, &SIKRigRetargetChainList::IsAddChainEnabled)
			.ListItemsSource( &ListViewItems )
			.OnGenerateRow( this, &SIKRigRetargetChainList::MakeListRowWidget )
			.OnMouseButtonClick(this, &SIKRigRetargetChainList::OnItemClicked)
			.OnContextMenuOpening(this, &SIKRigRetargetChainList::CreateContextMenu)
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
				
				+ SHeaderRow::Column( ColumnId_DeleteChainLabel )
				.DefaultLabel( LOCTEXT( "DeleteChainColumnLabel", "Delete Chain" ) )
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

TSharedPtr<SWidget> SIKRigRetargetChainList::CreateContextMenu()
{
	static constexpr bool CloseAfterSelection = true;
	FMenuBuilder MenuBuilder(CloseAfterSelection, CommandList);

	MenuBuilder.BeginSection("Chains", LOCTEXT("ChainsSection", "Chains"));

	const FUIAction Action = FUIAction( FExecuteAction::CreateSP(this, &SIKRigRetargetChainList::SortChainList));
	static const FText Label = LOCTEXT("SortChainsLabel", "Sort Chains");
	static const FText Tooltip = LOCTEXT("SortChainsTooltip", "Sort chain list in hierarchical order. This does not affect the retargeting behavior.");
	MenuBuilder.AddMenuEntry(Label, Tooltip, FSlateIcon(), Action);

	MenuBuilder.EndSection();
	
	return MenuBuilder.MakeWidget();
}

void SIKRigRetargetChainList::SortChainList()
{
	const TSharedPtr<FIKRigEditorController> Controller = EditorController.Pin();
	if (!Controller.IsValid())
	{
		return;
	}
	
	if (const UIKRigController* AssetController = Controller->AssetController)
	{
		AssetController->SortRetargetChains();
		RefreshView();
	}
}

#undef LOCTEXT_NAMESPACE
