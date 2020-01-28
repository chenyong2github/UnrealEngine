// Copyright Epic Games, Inc. All Rights Reserved.

#include "DetailMultiTopLevelObjectRootNode.h"
#include "IDetailRootObjectCustomization.h"
#include "DetailWidgetRow.h"

void SDetailMultiTopLevelObjectTableRow::Construct(const FArguments& InArgs, const TSharedRef<FDetailTreeNode>& InOwnerTreeNode, const TSharedRef<SWidget>& InCustomizedWidgetContents, const TSharedRef<STableViewBase>& InOwnerTableView)
{
	Construct(InArgs, InOwnerTreeNode);
	ChildSlotConstruct(InCustomizedWidgetContents, InOwnerTableView);
	// Disable toggle functionality
	bShowExpansionArrow = false;
}

void SDetailMultiTopLevelObjectTableRow::Construct(const FArguments& InArgs, const TSharedRef<FDetailTreeNode>& InOwnerTreeNode)
{
	OwnerTreeNode = InOwnerTreeNode;
	bShowExpansionArrow = InArgs._ShowExpansionArrow;
}

void SDetailMultiTopLevelObjectTableRow::ChildSlotConstruct(const TSharedRef<SWidget>& InCustomizedWidgetContents, const TSharedRef<STableViewBase>& InOwnerTableView)
{
	ChildSlot
	[
		SNew( SBox )
		.Padding( FMargin( 0.0f, 0.0f, SDetailTableRowBase::ScrollbarPaddingSize, 0.0f ) )
		[
			InCustomizedWidgetContents
		]
	];

	STableRow< TSharedPtr< FDetailTreeNode > >::ConstructInternal(
		STableRow::FArguments()
			.Style(FEditorStyle::Get(), "DetailsView.TreeView.TableRow")
			.ShowSelection(false),
		InOwnerTableView
	);
	// Enable toggle functionality
	bShowExpansionArrow = true;
}


const FSlateBrush* SDetailMultiTopLevelObjectTableRow::GetBackgroundImage() const
{
	if (IsHovered())
	{
		return IsItemExpanded() ? FEditorStyle::GetBrush("DetailsView.CategoryTop_Hovered") : FEditorStyle::GetBrush("DetailsView.CollapsedCategory_Hovered");
	}
	else
	{
		return IsItemExpanded() ? FEditorStyle::GetBrush("DetailsView.CategoryTop") : FEditorStyle::GetBrush("DetailsView.CollapsedCategory");
	}
}

FReply SDetailMultiTopLevelObjectTableRow::OnMouseButtonDown( const FGeometry& MyGeometry, const FPointerEvent& MouseEvent )
{
	if (bShowExpansionArrow && MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		ToggleExpansion();
		return FReply::Handled();
	}
	else
	{
		return FReply::Unhandled();
	}
}

FReply SDetailMultiTopLevelObjectTableRow::OnMouseButtonDoubleClick( const FGeometry& InMyGeometry, const FPointerEvent& InMouseEvent )
{
	return OnMouseButtonDown(InMyGeometry, InMouseEvent);
}


FDetailMultiTopLevelObjectRootNode::FDetailMultiTopLevelObjectRootNode( const FDetailNodeList& InChildNodes, const TSharedPtr<IDetailRootObjectCustomization>& InRootObjectCustomization, IDetailsViewPrivate* InDetailsView, const UObject& InRootObject )
	: ChildNodes(InChildNodes)
	, DetailsView(InDetailsView)
	, RootObjectCustomization(InRootObjectCustomization)
	, RootObject(const_cast<UObject*>(&InRootObject))
	, NodeName(InRootObject.GetFName())
	, bShouldBeVisible(false)
{
}

ENodeVisibility FDetailMultiTopLevelObjectRootNode::GetVisibility() const
{
	ENodeVisibility FinalVisibility = ENodeVisibility::Visible;
	if(RootObjectCustomization.IsValid() && RootObject.IsValid() && !RootObjectCustomization.Pin()->IsObjectVisible(RootObject.Get()))
	{
		FinalVisibility = ENodeVisibility::ForcedHidden;
	}
	else
	{
		FinalVisibility = bShouldBeVisible ? ENodeVisibility::Visible : ENodeVisibility::HiddenDueToFiltering;
	}

	return FinalVisibility;
}

TSharedRef< ITableRow > FDetailMultiTopLevelObjectRootNode::GenerateWidgetForTableView(const TSharedRef<STableViewBase>& OwnerTable, const FDetailColumnSizeData& ColumnSizeData, bool bAllowFavoriteSystem)
{
	FDetailWidgetRow Row;
	TSharedPtr<SDetailMultiTopLevelObjectTableRow> DetailMultiTopLevelObjectTableRow;
	if (RootObjectCustomization.IsValid())
	{
		
		DetailMultiTopLevelObjectTableRow = SNew(SDetailMultiTopLevelObjectTableRow, AsShared());
		GenerateStandaloneWidget(Row, DetailMultiTopLevelObjectTableRow.ToSharedRef());
		DetailMultiTopLevelObjectTableRow->ChildSlotConstruct(Row.NameWidget.Widget, OwnerTable);
		bShouldShowOnlyChildren = false;
	}
	else
	{
		GenerateStandaloneWidget(Row);
		DetailMultiTopLevelObjectTableRow = SNew(SDetailMultiTopLevelObjectTableRow, AsShared(), Row.NameWidget.Widget, OwnerTable);
		bShouldShowOnlyChildren = true;
	}
	
	return DetailMultiTopLevelObjectTableRow.ToSharedRef();
}


bool GenerateStandaloneWidgetInternal(FDetailWidgetRow& OutRow, TSharedPtr<SWidget>& HeaderWidget, const FName& NodeName)
{
	if (!HeaderWidget.IsValid())
	{
		// no customization was supplied or was passed back from the interface as invalid
		// just make a text block with the name
		HeaderWidget =
			SNew(STextBlock)
			.Font(FEditorStyle::GetFontStyle("DetailsView.CategoryFontStyle"))
			.Text(FText::FromName(NodeName));
	}

	OutRow.NameContent()
	[
		HeaderWidget.ToSharedRef()
	];

	return true;
}

bool FDetailMultiTopLevelObjectRootNode::GenerateStandaloneWidget(FDetailWidgetRow& OutRow) const
{
	TSharedPtr<SWidget> HeaderWidget;
	if (RootObjectCustomization.IsValid() && RootObject.IsValid())
	{
		HeaderWidget = RootObjectCustomization.Pin()->CustomizeObjectHeader(RootObject.Get());
	}

	return GenerateStandaloneWidgetInternal(OutRow, HeaderWidget, NodeName);
}


bool FDetailMultiTopLevelObjectRootNode::GenerateStandaloneWidget(FDetailWidgetRow& OutRow, const TSharedRef<ITableRow>& InTableRow) const
{
	TSharedPtr<SWidget> HeaderWidget;
	if (RootObjectCustomization.IsValid() && RootObject.IsValid())
	{
		HeaderWidget = RootObjectCustomization.Pin()->CustomizeObjectHeader(RootObject.Get(), InTableRow);
	}

	return GenerateStandaloneWidgetInternal(OutRow, HeaderWidget, NodeName);
}


void FDetailMultiTopLevelObjectRootNode::GetChildren(FDetailNodeList& OutChildren )
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

void FDetailMultiTopLevelObjectRootNode::FilterNode( const FDetailFilter& InFilter )
{
	bShouldBeVisible = false;
	for( int32 ChildIndex = 0; ChildIndex < ChildNodes.Num(); ++ChildIndex )
	{
		TSharedRef<FDetailTreeNode>& Child = ChildNodes[ChildIndex];

		Child->FilterNode( InFilter );

		if( Child->GetVisibility() == ENodeVisibility::Visible )
		{
			bShouldBeVisible = true;

			if (DetailsView)
			{
				DetailsView->RequestItemExpanded(Child, Child->ShouldBeExpanded());
			}
		}
	}
}

bool FDetailMultiTopLevelObjectRootNode::ShouldShowOnlyChildren() const
{
	return RootObjectCustomization.IsValid() && RootObject.IsValid() ? !RootObjectCustomization.Pin()->ShouldDisplayHeader(RootObject.Get()) : bShouldShowOnlyChildren;
}
