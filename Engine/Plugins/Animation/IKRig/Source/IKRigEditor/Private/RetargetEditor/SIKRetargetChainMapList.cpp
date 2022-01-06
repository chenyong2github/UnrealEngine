// Copyright Epic Games, Inc. All Rights Reserved.

#include "RetargetEditor/SIKRetargetChainMapList.h"


#include "Framework/Commands/UICommandList.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "IPersonaToolkit.h"
#include "SKismetInspector.h"
#include "Dialogs/Dialogs.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "SPositiveActionButton.h"
#include "Retargeter/IKRetargeter.h"
#include "RigEditor/IKRigEditorStyle.h"
#include "Widgets/Input/SComboBox.h"
#include "SSearchableComboBox.h"

#define LOCTEXT_NAMESPACE "SIKRigRetargetChains"

static const FName ColumnId_TargetChainLabel( "Target Bone Chain" );
static const FName ColumnId_SourceChainLabel( "Source Bone Chain" );

TSharedRef<ITableRow> FRetargetChainMapElement::MakeListRowWidget(
	const TSharedRef<STableViewBase>& InOwnerTable,
	TSharedRef<FRetargetChainMapElement> InChainElement,
	TSharedPtr<SIKRetargetChainMapList> InChainList)
{
	return SNew(SIKRetargetChainMapRow, InOwnerTable, InChainElement, InChainList);
}

void SIKRetargetChainMapRow::Construct(
	const FArguments& InArgs,
	const TSharedRef<STableViewBase>& InOwnerTableView,
	TSharedRef<FRetargetChainMapElement> InChainElement,
	TSharedPtr<SIKRetargetChainMapList> InChainList)
{
	ChainMapElement = InChainElement;
	ChainMapList = InChainList;

	// generate list of source chains
	// NOTE: cannot just use FName because "None" is considered a null entry and removed from ComboBox.
	SourceChainOptions.Reset();
	SourceChainOptions.Add(MakeShareable(new FString(TEXT("None"))));
	const UIKRigDefinition* SourceIKRig = ChainMapList.Pin()->EditorController.Pin()->AssetController->GetAsset()->GetSourceIKRig();
	if (SourceIKRig)
	{
		const TArray<FBoneChain>& Chains = SourceIKRig->GetRetargetChains();
		for (const FBoneChain& BoneChain : Chains)
		{
			SourceChainOptions.Add(MakeShareable(new FString(BoneChain.ChainName.ToString())));
		}
	}

	SMultiColumnTableRow< FRetargetChainMapElementPtr >::Construct( FSuperRowType::FArguments(), InOwnerTableView );
}

TSharedRef< SWidget > SIKRetargetChainMapRow::GenerateWidgetForColumn( const FName& ColumnName )
{
	if (ColumnName == ColumnId_TargetChainLabel)
	{
		TSharedRef<SWidget> NewWidget =
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Center)
		.Padding(3.0f, 1.0f)
		[
			SNew(STextBlock)
			.Text(FText::FromName(ChainMapElement.Pin()->TargetChainName))
			.Font(FEditorStyle::GetFontStyle(TEXT("BoldFont")))
		];
		return NewWidget;
	}
	else
	{
		TSharedRef<SWidget> NewWidget =
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Center)
		.Padding(3.0f, 1.0f)
		[
			SNew(SSearchableComboBox)
			.OptionsSource(&SourceChainOptions)
			.OnGenerateWidget_Lambda([](TSharedPtr<FString> InItem)
			{
				return SNew(STextBlock).Text(FText::FromString(*InItem.Get()));
			})
			.OnSelectionChanged(this, &SIKRetargetChainMapRow::OnSourceChainComboSelectionChanged)
			[
				SNew(STextBlock)
				.Text(this, &SIKRetargetChainMapRow::GetSourceChainName)
			]
		];
		return NewWidget;
	}
}

void SIKRetargetChainMapRow::OnSourceChainComboSelectionChanged(TSharedPtr<FString> InName, ESelectInfo::Type SelectInfo)
{
	UIKRetargeterController* RetargeterController = ChainMapList.Pin()->GetRetargetController();
	if (!RetargeterController)
	{
		return; 
	}
	
	const FName TargetChainName = ChainMapElement.Pin()->TargetChainName;
	const FName SourceChainName = FName(*InName.Get());
	RetargeterController->SetSourceChainForTargetChain(TargetChainName, SourceChainName);
}

FText SIKRetargetChainMapRow::GetSourceChainName() const
{
	UIKRetargeterController* RetargeterController = ChainMapList.Pin()->GetRetargetController();
	if (!RetargeterController)
	{
		return FText::FromName(NAME_None); 
	}
	
	const FName TargetChainName = ChainMapElement.Pin()->TargetChainName;
	const FName SourceChainName = RetargeterController->GetSourceChainForTargetChain(TargetChainName);
	return FText::FromName(SourceChainName);
}

void SIKRetargetChainMapList::Construct(
	const FArguments& InArgs,
	TSharedRef<FIKRetargetEditorController> InEditorController)
{
	EditorController = InEditorController;
	EditorController.Pin()->ChainsView = SharedThis(this);

	ChildSlot
    [
	    SNew(SVerticalBox)
		+SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("TargetRootLabel", "Target Root: "))
				.TextStyle(FEditorStyle::Get(), "NormalText")
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(5, 0)
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(this, &SIKRetargetChainMapList::GetTargetRootBone)
				.IsEnabled(false)
			]
				
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("SourceRootLabel", "Source Root: "))
				.TextStyle(FEditorStyle::Get(), "NormalText")
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(5, 0)
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(this, &SIKRetargetChainMapList::GetSourceRootBone)
				.IsEnabled(false)
			]
		]

		+SVerticalBox::Slot()
		.AutoHeight()
		.VAlign(VAlign_Center)
		.HAlign(HAlign_Fill)
		.Padding(5)
		[
			SNew(SPositiveActionButton)
			.Visibility(this, &SIKRetargetChainMapList::IsAutoMapButtonVisible)
			.Icon(FAppStyle::Get().GetBrush("Icons.Refresh"))
			.Text(LOCTEXT("AutoMapButtonLabel", "Auto-Map Chains"))
			.ToolTipText(LOCTEXT("AutoMapButtonToolTip", "Automatically assign source chains based on fuzzy string match"))
			.OnClicked(this, &SIKRetargetChainMapList::OnAutoMapButtonClicked)
		]
	    
        +SVerticalBox::Slot()
        [
			SAssignNew(ListView, SRetargetChainMapListViewType )
			.SelectionMode(ESelectionMode::Multi)
			.IsEnabled(this, &SIKRetargetChainMapList::IsChainMapEnabled)
			.ListItemsSource( &ListViewItems )
			.OnGenerateRow( this, &SIKRetargetChainMapList::MakeListRowWidget )
			.OnMouseButtonClick(this, &SIKRetargetChainMapList::OnItemClicked)
			.ItemHeight( 22.0f )
			.HeaderRow
			(
				SNew( SHeaderRow )
				+ SHeaderRow::Column( ColumnId_TargetChainLabel )
				.DefaultLabel( LOCTEXT( "TargetColumnLabel", "Target Chain" ) )

				+ SHeaderRow::Column( ColumnId_SourceChainLabel )
				.DefaultLabel( LOCTEXT( "SourceColumnLabel", "Source Chain" ) )
			)
        ]
    ];

	RefreshView();
}

UIKRetargeterController* SIKRetargetChainMapList::GetRetargetController() const
{
	const TSharedPtr<FIKRetargetEditorController> Controller = EditorController.Pin();
	if (!Controller.IsValid())
	{
		return nullptr;
	}

	return Controller->AssetController;
}

FText SIKRetargetChainMapList::GetSourceRootBone() const
{
	UIKRetargeterController* RetargeterController = GetRetargetController();
	if (!RetargeterController)
	{
		return FText::FromName(NAME_None); 
	}
	
	return FText::FromName(RetargeterController->GetSourceRootBone());
}

FText SIKRetargetChainMapList::GetTargetRootBone() const
{
	UIKRetargeterController* RetargeterController = GetRetargetController();
	if (!RetargeterController)
	{
		return FText::FromName(NAME_None); 
	}
	
	return FText::FromName(RetargeterController->GetTargetRootBone());
}

bool SIKRetargetChainMapList::IsChainMapEnabled() const
{
	UIKRetargeterController* RetargeterController = GetRetargetController();
	if (!RetargeterController)
	{
		return false; 
	}
	
	if (RetargeterController->GetAsset()->GetTargetIKRig())
	{
		const TArray<FBoneChain>& Chains = RetargeterController->GetAsset()->GetTargetIKRig()->GetRetargetChains();
		return Chains.Num() > 0;
	}

	return false;
}

void SIKRetargetChainMapList::RefreshView()
{
	UIKRetargeterController* RetargeterController = GetRetargetController();
	if (!RetargeterController)
	{
		return; 
	}
	
	// refresh list of chains
	ListViewItems.Reset();
	const TArray<FRetargetChainMap>& ChainMappings = RetargeterController->GetChainMappings();
	for (const FRetargetChainMap& ChainMap : ChainMappings)
	{
		TSharedPtr<FRetargetChainMapElement> ChainItem = FRetargetChainMapElement::Make(ChainMap.TargetChain);
		ListViewItems.Add(ChainItem);
	}

	ListView->RequestListRefresh();
}

TSharedRef<ITableRow> SIKRetargetChainMapList::MakeListRowWidget(
	TSharedPtr<FRetargetChainMapElement> InElement,
    const TSharedRef<STableViewBase>& OwnerTable)
{
	return InElement->MakeListRowWidget(
		OwnerTable,
		InElement.ToSharedRef(),
		SharedThis(this));
}

void SIKRetargetChainMapList::OnItemClicked(TSharedPtr<FRetargetChainMapElement> InItem)
{
	// TODO highlight chain in skeleton view
}

EVisibility SIKRetargetChainMapList::IsAutoMapButtonVisible() const
{
	return IsChainMapEnabled() ? EVisibility::Visible : EVisibility::Hidden;
}

FReply SIKRetargetChainMapList::OnAutoMapButtonClicked() const
{
	UIKRetargeterController* RetargeterController = GetRetargetController();
	if (!RetargeterController)
	{
		return FReply::Unhandled();
	}
	
	RetargeterController->CleanChainMapping();
	RetargeterController->AutoMapChains();
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
