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

struct FPinValueInspectorTreeViewNode
{
	FText DisplayName;
	FText Description;
	FSlateColor IconColor;
	const FSlateBrush* IconBrush;
	TArray<TSharedPtr<FPropertyInstanceInfo>> Children;
	TSharedPtr<FDetailColumnSizeData> SharedColumnSizeData;

	FPinValueInspectorTreeViewNode()
		:IconBrush(FEditorStyle::GetBrush(TEXT("Kismet.AllClasses.VariableIcon")))
	{}
};

class SPinValueInspector_ConstrainedBox : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SPinValueInspector_ConstrainedBox)
		: _MinWidth()
		, _MaxWidth()
	{}
		SLATE_DEFAULT_SLOT(FArguments, Content)
		SLATE_ATTRIBUTE(TOptional<float>, MinWidth)
		SLATE_ATTRIBUTE(TOptional<float>, MaxWidth)

	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		MinWidth = InArgs._MinWidth;
		MaxWidth = InArgs._MaxWidth;

		ChildSlot
		[
			InArgs._Content.Widget
		];
	}
	
	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override
	{
		const float MinWidthVal = MinWidth.Get().Get(0.0f);
		const float MaxWidthVal = MaxWidth.Get().Get(0.0f);

		if (MinWidthVal == 0.0f && MaxWidthVal == 0.0f)
		{
			return SCompoundWidget::ComputeDesiredSize(LayoutScaleMultiplier);
		}
		else
		{
			FVector2D ChildSize = ChildSlot.GetWidget()->GetDesiredSize();

			float XVal = FMath::Max(MinWidthVal, ChildSize.X);
			if (MaxWidthVal > MinWidthVal)
			{
				XVal = FMath::Min(MaxWidthVal, XVal);
			}

			return FVector2D(XVal, ChildSize.Y);
		}
	}

private:
	TAttribute<TOptional<float>> MinWidth;
	TAttribute<TOptional<float>> MaxWidth;
};

class SPinValueInspector_TreeViewRow : public STableRow<FPinValueInspectorTreeViewNodePtr>
{
public:
	SLATE_BEGIN_ARGS(SPinValueInspector_TreeViewRow) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, TSharedRef<STableViewBase> OwnerTableView, FPinValueInspectorTreeViewNodePtr InNode, bool bShowExpanderArrows)
	{
		STableRow<FPinValueInspectorTreeViewNodePtr>::ConstructInternal(STableRow::FArguments(), OwnerTableView);

		if (!InNode.IsValid() || !InNode->SharedColumnSizeData.IsValid())
		{
			return;
		}

		TSharedRef<SWidget> RowWidget =
			SNew(SSplitter)
			.Style(FEditorStyle::Get(), "DetailsView.Splitter")
			.PhysicalSplitterHandleSize(1.0f)
			.HitDetectionSplitterHandleSize(5.0f)
			+SSplitter::Slot()
			.Value(InNode->SharedColumnSizeData->NameColumnWidth)
			.OnSlotResized(InNode->SharedColumnSizeData->OnNameColumnResized)
			[
				SNew(SHorizontalBox)
				.Clipping(EWidgetClipping::OnDemand)
				+SHorizontalBox::Slot()
				.HAlign(HAlign_Left)
				.VAlign(VAlign_Fill)
				.AutoWidth()
				[
					SNew(PropertyInfoViewStyle::SIndent, SharedThis(this))
				]
				+SHorizontalBox::Slot()
				.HAlign(HAlign_Left)
				.VAlign(VAlign_Center)
				.Padding(8.0f, 0.0f, 0.0f, 0.0f)
				.AutoWidth()
				[
					SNew(PropertyInfoViewStyle::SExpanderArrow, SharedThis(this))
					.HasChildren(InNode->Children.Num() > 0)
					.Visibility(bShowExpanderArrows ? EVisibility::Visible : EVisibility::Collapsed)
				]
				+SHorizontalBox::Slot()
				.HAlign(HAlign_Left)
				.VAlign(VAlign_Center)
				.AutoWidth()
				[
					SNew(SImage)
					.Image(InNode->IconBrush)
					.ColorAndOpacity(InNode->IconColor)
				]
				+SHorizontalBox::Slot()
				.HAlign(HAlign_Left)
				.VAlign(VAlign_Center)
				.Padding(5.0f, 0.0f, 0.0f, 0.0f)
				[
					SNew(STextBlock)
					.Text(InNode->DisplayName)
				]
			]
			+SSplitter::Slot()
			.Value(InNode->SharedColumnSizeData->ValueColumnWidth)
			.OnSlotResized(InNode->SharedColumnSizeData->OnValueColumnResized)
			[
				SNew(SHorizontalBox)
				.Clipping(EWidgetClipping::OnDemand)
				+SHorizontalBox::Slot()
				.HAlign(HAlign_Left)
				.VAlign(VAlign_Center)
				[
					SNew(SPinValueInspector_ConstrainedBox)
					.MinWidth(125.0f)
					.MaxWidth(400.0f)
					[
						SNew(STextBlock)
						.Text(InNode->Description)
					]
				]
			];
		
		ChildSlot
		[
			SNew(SBorder)
			.Clipping(EWidgetClipping::ClipToBounds)
			[
				RowWidget
			]
		];
	}
};

void SPinValueInspector::Construct(const FArguments& InArgs, const FEdGraphPinReference& InPinRef)
{
	PinRef = InPinRef;

	// Locate the class property associated with the source pin and set it to the root node.
	const UEdGraphPin* GraphPin = PinRef.Get();
	if (GraphPin)
	{
		if (const UEdGraphNode* GraphNode = GraphPin->GetOwningNodeUnchecked())
		{
			if (UBlueprint* Blueprint = FBlueprintEditorUtils::FindBlueprintForNode(GraphNode))
			{
				FPinValueInspectorTreeViewNodePtr RootNode = MakeShared<FPinValueInspectorTreeViewNode>();

				if (FKismetDebugUtilities::FindClassPropertyForPin(Blueprint, GraphPin))
				{
					RootNode->DisplayName = GraphPin->GetDisplayName();
				}
				else
				{
					FFormatNamedArguments Args;
					Args.Add(TEXT("PinWatchName"), FText::FromString(GraphPin->GetName()));
					RootNode->DisplayName = FText::Format(LOCTEXT("DisplayNameNoProperty", "{PinWatchName} (no prop)"), Args);
				}

				RootNode->IconBrush = FBlueprintEditorUtils::GetIconFromPin(GraphPin->PinType);
				RootNode->IconColor = GraphPin->GetSchema()->GetPinTypeColor(GraphPin->PinType);

				TSharedPtr<FPropertyInstanceInfo> DebugInfo;
				const FKismetDebugUtilities::EWatchTextResult WatchStatus = FKismetDebugUtilities::GetDebugInfo(DebugInfo, Blueprint, Blueprint->GetObjectBeingDebugged(), GraphPin);
				switch (WatchStatus)
				{
				case FKismetDebugUtilities::EWTR_Valid:
				{
					check(DebugInfo.IsValid());
					const FString ValStr = DebugInfo->Value.ToString();
					RootNode->Description = FText::FromString(ValStr.Replace(TEXT("\n"), TEXT(" ")));
					break;
				}

				case FKismetDebugUtilities::EWTR_NotInScope:
					RootNode->Description = LOCTEXT("NotInScope", "Not in scope");
					break;

				case FKismetDebugUtilities::EWTR_NoProperty:
					RootNode->Description = LOCTEXT("UnknownProperty", "No debug data");
					break;

				default:
				case FKismetDebugUtilities::EWTR_NoDebugObject:
					RootNode->Description = LOCTEXT("NoDebugObject", "No debug object");
					break;
				}

				if (DebugInfo.IsValid())
				{
					RootNode->Children = MoveTemp(DebugInfo->Children);
				}

				RootNode->SharedColumnSizeData = MakeShared<FDetailColumnSizeData>();
				RootNode->SharedColumnSizeData->SetValueColumnWidth(0.5f);

				RootNodes.Add(MoveTemp(RootNode));
			}
		}
	}

	ChildSlot
	[
		SNew(SVerticalBox)
		+SVerticalBox::Slot()
		.AutoHeight()
		[
			SAssignNew(SearchBoxWidget, SSearchBox)
			.Visibility(this, &SPinValueInspector::GetSearchFilterVisibility)
		]
		+SVerticalBox::Slot()
		[
			SAssignNew(TreeViewWidget, STreeView<FPinValueInspectorTreeViewNodePtr>)
			.TreeItemsSource(&RootNodes)
			.OnGetChildren(this, &SPinValueInspector::OnGetTreeViewNodeChildren)
			.OnGenerateRow(this, &SPinValueInspector::OnGenerateRowForTreeViewNode)
			.TreeViewStyle(&FAppStyle::Get().GetWidgetStyle<FTableViewStyle>("PropertyTable.InViewport.ListView"))
		]
	];

	// Expand all root-level nodes that have children by default.
	for (const FPinValueInspectorTreeViewNodePtr& RootNode : RootNodes)
	{
		const bool bShouldExpandItem = RootNode->Children.Num() > 0;
		TreeViewWidget->SetItemExpansion(RootNode, bShouldExpandItem);
	}
}

bool SPinValueInspector::ShouldShowSearchFilter() const
{
	// Only expose the search filter if child nodes are present (e.g. struct/container types).
	for (const FPinValueInspectorTreeViewNodePtr& RootNode : RootNodes)
	{
		if (RootNode->Children.Num() > 0)
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

void SPinValueInspector::OnGetTreeViewNodeChildren(FPinValueInspectorTreeViewNodePtr InNode, TArray<FPinValueInspectorTreeViewNodePtr>& OutChildren)
{
	for (const TSharedPtr<FPropertyInstanceInfo>& ChildData : InNode->Children)
	{
		if (ChildData.IsValid())
		{
			FPinValueInspectorTreeViewNodePtr ChildNode = MakeShared<FPinValueInspectorTreeViewNode>();

			ChildNode->DisplayName = ChildData->DisplayName;

			const FString ValStr = ChildData->Value.ToString();
			ChildNode->Description = FText::FromString(ValStr.Replace(TEXT("\n"), TEXT(" ")));

			FEdGraphPinType PinType;
			const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();
			if (K2Schema->ConvertPropertyToPinType(ChildData->Property.Get(), PinType))
			{
				ChildNode->IconBrush = FBlueprintEditorUtils::GetIconFromPin(PinType);
				ChildNode->IconColor = K2Schema->GetPinTypeColor(PinType);
			}

			ChildNode->Children = ChildData->Children;
			ChildNode->SharedColumnSizeData = InNode->SharedColumnSizeData;

			OutChildren.Add(MoveTemp(ChildNode));
		}
	}
}

TSharedRef<ITableRow> SPinValueInspector::OnGenerateRowForTreeViewNode(FPinValueInspectorTreeViewNodePtr InNode, const TSharedRef<STableViewBase>& OwnerTable)
{
	// Only show expander arrows if we have at least one child node.
	bool bShowExpanderArrows = false;
	for (const FPinValueInspectorTreeViewNodePtr& RootNode : RootNodes)
	{
		if (RootNode->Children.Num() > 0)
		{
			bShowExpanderArrows = true;
			break;
		}
	}

	return SNew(SPinValueInspector_TreeViewRow, OwnerTable, InNode, bShowExpanderArrows);
}

#undef LOCTEXT_NAMESPACE