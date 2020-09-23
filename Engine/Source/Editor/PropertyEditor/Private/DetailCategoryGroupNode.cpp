// Copyright Epic Games, Inc. All Rights Reserved.

#include "DetailCategoryGroupNode.h"
#include "PropertyCustomizationHelpers.h"
#include "PropertyEditorConstants.h"

void SDetailCategoryTableRow::Construct( const FArguments& InArgs, TSharedRef<FDetailTreeNode> InOwnerTreeNode, const TSharedRef<STableViewBase>& InOwnerTableView )
{
	OwnerTreeNode = InOwnerTreeNode;

	bIsInnerCategory = InArgs._InnerCategory;
	bShowBorder = InArgs._ShowBorder;

	TSharedPtr<SWidget> Widget = SNullWidget::NullWidget;

	float MyContentTopPadding = 2.0f;
	float MyContentBottomPadding = 2.0f;

	float ChildSlotPadding = 2.0f;
	float BorderVerticalPadding = 3.0f;

	FDetailColumnSizeData& ColumnSizeData = InOwnerTreeNode->GetDetailsView()->GetColumnSizeData();

	MyContentTopPadding += ChildSlotPadding + 2 * BorderVerticalPadding;
	MyContentBottomPadding += 2 * BorderVerticalPadding;

	ChildSlotPadding = 0.0f;
	BorderVerticalPadding = 0.0f;

	Widget = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.VAlign(VAlign_Center)
		.Padding(12.0f, MyContentTopPadding, 12.0f, MyContentBottomPadding)
		.AutoWidth()
		[
			SNew(SExpanderArrow, SharedThis(this))
		]
		+ SHorizontalBox::Slot()
		.VAlign(VAlign_Center)
		.FillWidth(0.5f)
		[
			SNew(STextBlock)
			.TransformPolicy(ETextTransformPolicy::ToUpper)
			.Text(InArgs._DisplayName)
			.Font(FAppStyle::Get().GetFontStyle(bIsInnerCategory ? PropertyEditorConstants::PropertyFontStyle : PropertyEditorConstants::CategoryFontStyle))
			.TextStyle(FAppStyle::Get(), "DetailsView.CategoryTextStyle")
		]
		+ SHorizontalBox::Slot()
		.VAlign(VAlign_Center)
		.FillWidth(0.5f)
		[
			InArgs._HeaderContent.IsValid() ? InArgs._HeaderContent.ToSharedRef() : SNullWidget::NullWidget
		];
	
	ChildSlot
	.Padding( 0.0f, bIsInnerCategory ? 0.0f : ChildSlotPadding, 0.0f, 0.0f )
	[	
		SNew( SBorder )
		.BorderImage( this, &SDetailCategoryTableRow::GetBackgroundImage )
		.Padding( FMargin( 0.0f, BorderVerticalPadding, SDetailTableRowBase::ScrollbarPaddingSize, BorderVerticalPadding ) )
		[
			Widget.ToSharedRef()
		]
	];

	STableRow< TSharedPtr< FDetailTreeNode > >::ConstructInternal(
		STableRow::FArguments()
			.Style(FEditorStyle::Get(), "DetailsView.TreeView.TableRow")
			.ShowSelection(false),
		InOwnerTableView
	);
}

EVisibility SDetailCategoryTableRow::IsSeparatorVisible() const
{
	return bIsInnerCategory || IsItemExpanded() ? EVisibility::Collapsed : EVisibility::Visible;
}

const FSlateBrush* SDetailCategoryTableRow::GetBackgroundImage() const
{
	if (bShowBorder)
	{
		if (IsHovered())
		{
			if (bIsInnerCategory)
			{
				return FEditorStyle::GetBrush("DetailsView.CategoryMiddle_Hovered");
			}

			return IsItemExpanded() ? FEditorStyle::GetBrush("DetailsView.CategoryTop_Hovered") : FEditorStyle::GetBrush("DetailsView.CollapsedCategory_Hovered");
		}
		else
		{
			if (bIsInnerCategory)
			{
				return FEditorStyle::GetBrush("DetailsView.CategoryMiddle");
			}
			
			return IsItemExpanded() ? FEditorStyle::GetBrush("DetailsView.CategoryTop") : FEditorStyle::GetBrush("DetailsView.CollapsedCategory");
		}
	}

	return nullptr;
}

FReply SDetailCategoryTableRow::OnMouseButtonDown( const FGeometry& MyGeometry, const FPointerEvent& MouseEvent )
{
	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		ToggleExpansion();
		return FReply::Handled();
	}
	else
	{
		return FReply::Unhandled();
	}
}

FReply SDetailCategoryTableRow::OnMouseButtonDoubleClick(const FGeometry& InMyGeometry, const FPointerEvent& InMouseEvent)
{
	return OnMouseButtonDown(InMyGeometry, InMouseEvent);
}

FDetailCategoryGroupNode::FDetailCategoryGroupNode( const FDetailNodeList& InChildNodes, FName InGroupName, FDetailCategoryImpl& InParentCategory )
	: ChildNodes( InChildNodes )
	, ParentCategory( InParentCategory )
	, GroupName( InGroupName )
	, bShouldBeVisible( false )
	, bShowBorder(true)
	, bHasSplitter(false)
{
}

TSharedRef< ITableRow > FDetailCategoryGroupNode::GenerateWidgetForTableView( const TSharedRef<STableViewBase>& OwnerTable, bool bAllowFavoriteSystem)
{
	return SNew( SDetailCategoryTableRow, AsShared(), OwnerTable )
		.DisplayName( FText::FromName(GroupName) )
		.InnerCategory( true )
		.ShowBorder( bShowBorder );
}


bool FDetailCategoryGroupNode::GenerateStandaloneWidget(FDetailWidgetRow& OutRow) const
{
	OutRow.NameContent()
	[
		SNew(STextBlock)
		.Font(FEditorStyle::GetFontStyle("PropertyWindow.NormalFont"))
		.Text(FText::FromName(GroupName))
	];

	return true;
}

void FDetailCategoryGroupNode::GetChildren( FDetailNodeList& OutChildren )
{
	for( int32 ChildIndex = 0; ChildIndex < ChildNodes.Num(); ++ChildIndex )
	{
		TSharedRef<FDetailTreeNode>& Child = ChildNodes[ChildIndex];
		if( Child->GetVisibility() == ENodeVisibility::Visible )
		{
			if( Child->ShouldShowOnlyChildren() )
			{
				Child->GetChildren( OutChildren );
			}
			else
			{
				OutChildren.Add( Child );
			}
		}
	}
}

void FDetailCategoryGroupNode::FilterNode( const FDetailFilter& InFilter )
{
	bShouldBeVisible = false;
	for( int32 ChildIndex = 0; ChildIndex < ChildNodes.Num(); ++ChildIndex )
	{
		TSharedRef<FDetailTreeNode>& Child = ChildNodes[ChildIndex];

		Child->FilterNode( InFilter );

		if( Child->GetVisibility() == ENodeVisibility::Visible )
		{
			bShouldBeVisible = true;

			ParentCategory.RequestItemExpanded( Child, Child->ShouldBeExpanded() );
		}
	}
}
