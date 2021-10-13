// Copyright Epic Games, Inc. All Rights Reserved.

#include "SPinValueInspector.h"
#include "UObject/WeakFieldPtr.h"
#include "Styling/AppStyle.h"
#include "Widgets/SBoxPanel.h"
#include "PropertyInfoViewStyle.h"
#include "Widgets/Input/SSearchBox.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetDebugUtilities.h"
#include "IDetailPropertyRow.h"

#define LOCTEXT_NAMESPACE "SPinValueInspector"

class SPinValueInspector_ConstrainedBox : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SPinValueInspector_ConstrainedBox)
		: _MinWidth()
		, _MaxWidth()
		, _MinHeight()
		, _MaxHeight()
	{}
		SLATE_DEFAULT_SLOT(FArguments, Content)
		SLATE_ATTRIBUTE(TOptional<float>, MinWidth)
		SLATE_ATTRIBUTE(TOptional<float>, MaxWidth)
		SLATE_ATTRIBUTE(TOptional<float>, MinHeight)
		SLATE_ATTRIBUTE(TOptional<float>, MaxHeight)

	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		MinWidth = InArgs._MinWidth;
		MaxWidth = InArgs._MaxWidth;
		MinHeight = InArgs._MinHeight;
		MaxHeight = InArgs._MaxHeight;

		ChildSlot
		[
			InArgs._Content.Widget
		];
	}
	
	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override
	{
		const float MinWidthVal = MinWidth.Get().Get(0.0f);
		const float MaxWidthVal = MaxWidth.Get().Get(0.0f);
		const float MinHeightVal = MinHeight.Get().Get(0.0f);
		const float MaxHeightVal = MaxHeight.Get().Get(0.0f);

		if (MinWidthVal == 0.0f && MaxWidthVal == 0.0f && MinHeightVal == 0.0f && MaxHeightVal == 0.0f)
		{
			return SCompoundWidget::ComputeDesiredSize(LayoutScaleMultiplier);
		}

		FVector2D ChildSize = ChildSlot.GetWidget()->GetDesiredSize();

		if (MinWidthVal != 0.0f || MaxWidthVal != 0.0f)
		{
			ChildSize.X = FMath::Max(MinWidthVal, ChildSize.X);
			if (MaxWidthVal > MinWidthVal)
			{
				ChildSize.X = FMath::Min(MaxWidthVal, ChildSize.X);
			}
		}
		if (CachedXSize.IsSet() && ChildSize.X < *CachedXSize)
		{
			ChildSize.X = *CachedXSize;
		}

		if (MinHeightVal != 0.0f || MaxHeightVal != 0.0f)
		{
			ChildSize.Y = FMath::Max(MinHeightVal, ChildSize.Y);
			if (MaxHeightVal > MinHeightVal)
			{
				ChildSize.Y = FMath::Min(MaxHeightVal, ChildSize.Y);
			}
		}

		if ((!CachedXSize.IsSet() && ChildSize.X != 0.0f) ||
			(CachedXSize.IsSet() && ChildSize.X > *CachedXSize))
		{
			CachedXSize.Reset();
			CachedXSize.Emplace(ChildSize.X);
		}

		return ChildSize;
	}

	void RequestResize()
	{
		CachedXSize.Reset();
	}

private:
	TAttribute<TOptional<float>> MinWidth;
	TAttribute<TOptional<float>> MaxWidth;
	TAttribute<TOptional<float>> MinHeight;
	TAttribute<TOptional<float>> MaxHeight;

	// must be mutable to be changed in ComputeDesiredSize
	mutable TOptional<float> CachedXSize;
};

void SPinValueInspector::Construct(const FArguments& InArgs, const FEdGraphPinReference& InPinRef)
{
	PinRef = InPinRef;

	ChildSlot
	[
		SNew(SVerticalBox)
		+SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SSearchBox)
				.Visibility(this, &SPinValueInspector::GetSearchFilterVisibility)
				.OnTextChanged(this, &SPinValueInspector::OnSearchTextChanged)
		]
		+SVerticalBox::Slot()
		[
			SAssignNew(ConstrainedBox, SPinValueInspector_ConstrainedBox)
				.MinWidth(300.0f)
				.MaxWidth(800.0f)
				.MaxHeight(300.0f)
				[
					SAssignNew(TreeViewWidget, SKismetDebugTreeView)
						//.OnContextMenuOpening
						.OnExpansionChanged(this, &SPinValueInspector::OnExpansionChanged)
						.HeaderRow
						(
							SNew(SHeaderRow)
								.Visibility(EVisibility::Collapsed)
							+ SHeaderRow::Column(SKismetDebugTreeView::ColumnId_Name)
							+ SHeaderRow::Column(SKismetDebugTreeView::ColumnId_Value)
						)
				]
		]
	];

	PopulateTreeView();

	// Expand all root-level nodes that have children by default.
	for (const FDebugTreeItemPtr& RootNode : TreeViewWidget->GetRootTreeItems())
	{
		TreeViewWidget->SetItemExpansion(RootNode, RootNode->HasChildren());
	}
}

bool SPinValueInspector::ShouldShowSearchFilter() const
{
	// Only expose the search filter if child nodes are present (e.g. struct/container types).
	for (const FDebugTreeItemPtr& RootNode : TreeViewWidget->GetRootTreeItems())
	{
		if (RootNode->HasChildren())
		{
			return true;
		}
	}

	return false;
}

EVisibility SPinValueInspector::GetSearchFilterVisibility() const
{
	return ShouldShowSearchFilter() ? EVisibility::Visible : EVisibility::Collapsed;
}

void SPinValueInspector::OnSearchTextChanged(const FText& InSearchText)
{
	TreeViewWidget->ClearExpandedItems();
	TreeViewWidget->SetSearchText(InSearchText);
}

void SPinValueInspector::OnExpansionChanged(FDebugTreeItemPtr InItem, bool bItemIsExpanded)
{
	ConstrainedBox->RequestResize();
}

void SPinValueInspector::PopulateTreeView()
{
	// Locate the class property associated with the source pin and set it to the root node.
	const UEdGraphPin* GraphPin = PinRef.Get();
	if (!GraphPin)
	{
		TreeViewWidget->AddTreeItemUnique(SKismetDebugTreeView::MakeMessageItem(LOCTEXT("InvalidPin", "Pin Not Found").ToString()));
		return;
	}

	const UEdGraphNode* GraphNode = GraphPin->GetOwningNodeUnchecked();
	if (!GraphNode)
	{
		TreeViewWidget->AddTreeItemUnique(SKismetDebugTreeView::MakeMessageItem(LOCTEXT("InvalidNode", "Owning Node Not Found").ToString()));
		return;
	}

	UBlueprint* Blueprint = FBlueprintEditorUtils::FindBlueprintForNode(GraphNode);
	if (!Blueprint)
	{
		TreeViewWidget->AddTreeItemUnique(SKismetDebugTreeView::MakeMessageItem(LOCTEXT("InvalidBlueprint", "Owning Blueprint Not Found").ToString()));
		return;
	}
	
	TSharedPtr<FPropertyInstanceInfo> DebugInfo;
	const FKismetDebugUtilities::EWatchTextResult WatchStatus = FKismetDebugUtilities::GetDebugInfo(DebugInfo, Blueprint, Blueprint->GetObjectBeingDebugged(), GraphPin);
	if (WatchStatus != FKismetDebugUtilities::EWTR_Valid)
	{
		switch (WatchStatus)
		{
		case FKismetDebugUtilities::EWTR_NotInScope:
			TreeViewWidget->AddTreeItemUnique(SKismetDebugTreeView::MakeMessageItem(LOCTEXT("NotInScope", "Not in scope").ToString()));
			break;

		case FKismetDebugUtilities::EWTR_NoProperty:
			TreeViewWidget->AddTreeItemUnique(SKismetDebugTreeView::MakeMessageItem(LOCTEXT("UnknownProperty", "No debug data").ToString()));
			break;

		default:
		case FKismetDebugUtilities::EWTR_NoDebugObject:
			TreeViewWidget->AddTreeItemUnique(SKismetDebugTreeView::MakeMessageItem(LOCTEXT("NoDebugObject", "No debug object").ToString()));
			break;
		}

		return;
	}

	if (ensureMsgf(DebugInfo.IsValid(), TEXT("GetDebugInfo returned EWTR_Valid, but DebugInfo wasn't valid")))
	{
		TreeViewWidget->AddTreeItemUnique(SKismetDebugTreeView::MakeWatchChildItem(DebugInfo));
	}
}

#undef LOCTEXT_NAMESPACE