// Copyright Epic Games, Inc. All Rights Reserved.

#include "SMovieGraphActiveRenderSettingsTabContent.h"

#include "Graph/MovieGraphConfig.h"
#include "Graph/MovieGraphNode.h"
#include "Graph/Nodes/MovieGraphRenderPassNode.h"
#include "SPositiveActionButton.h"

#define LOCTEXT_NAMESPACE "SMoviePipelineActiveRenderSettingsTabContent"

const FName SMovieGraphActiveRenderSettingsTreeItem::ColumnID_Name(TEXT("Name"));
const FName SMovieGraphActiveRenderSettingsTreeItem::ColumnID_Value(TEXT("Value"));

const FName FActiveRenderSettingsTreeElement::RootName_Globals(TEXT("Globals"));
const FName FActiveRenderSettingsTreeElement::RootName_Branches(TEXT("Branches"));

FActiveRenderSettingsTreeElement::FActiveRenderSettingsTreeElement(const FName& InName, const EElementType InType)
	: Name(InName)
	, Type(InType)
{
}

FString FActiveRenderSettingsTreeElement::GetValue() const
{
	if (SettingsProperty && SettingsNode)
	{
		FString ValueString;
		SettingsProperty->ExportTextItem_InContainer(ValueString, SettingsNode, nullptr, nullptr, PPF_None);
		return ValueString;
	}

	return FString();
}

bool FActiveRenderSettingsTreeElement::IsBranchRenderable() const
{
	if (Type != EElementType::NamedBranch)
	{
		return false;
	}

	// The branch is renderable if there's a render pass node in the branch's children
	for (const TSharedPtr<FActiveRenderSettingsTreeElement>& Child : GetChildren())
	{
		if (Child.IsValid() && Child->SettingsNode && Child->SettingsNode->IsA<UMovieGraphRenderPassNode>())
		{
			return true;
		}
	}

	return false;
}

const TArray<TSharedPtr<FActiveRenderSettingsTreeElement>>& FActiveRenderSettingsTreeElement::GetChildren() const
{
	auto MakeElementFromNode = [this](const TObjectPtr<UMovieGraphNode>& Node) -> TSharedPtr<FActiveRenderSettingsTreeElement>
	{
		TSharedPtr<FActiveRenderSettingsTreeElement> Element =
			MakeShared<FActiveRenderSettingsTreeElement>(FName(*Node->GetNodeTitle().ToString()), EElementType::Node);
		Element->SettingsNode = Node;
		Element->FlattenedGraph = FlattenedGraph;

		return MoveTemp(Element);
	};

	// Returned the cached children if they are available
	if (!ChildrenCache.IsEmpty())
	{
		return ChildrenCache;
	}
	
	if (!FlattenedGraph.IsValid())
	{
		// This should be an empty array
		return ChildrenCache;
	}

	// For the root "Globals" element: all nodes which were found in the Globals branch should be children
	if ((Name == RootName_Globals) && (Type == EElementType::Root))
	{
		if (const FMovieGraphEvaluatedBranchConfig* BranchConfig = FlattenedGraph->BranchConfigMapping.Find(RootName_Globals))
		{
			for (const TObjectPtr<UMovieGraphNode>& Node : BranchConfig->GetNodes())
			{
				ChildrenCache.Add(MakeElementFromNode(Node));
			}
		}

		return ChildrenCache;
	}

	// For the root "Branches" element: all branches which were found should be children
	if ((Name == RootName_Branches) && (Type == EElementType::Root))
	{
		for (const auto& Pair : FlattenedGraph->BranchConfigMapping)
		{
			if (Pair.Key != RootName_Globals)
			{
				TSharedPtr<FActiveRenderSettingsTreeElement> Element =
					MakeShared<FActiveRenderSettingsTreeElement>(FName(Pair.Key), EElementType::NamedBranch);
				Element->FlattenedGraph = FlattenedGraph;
				
				ChildrenCache.Add(MoveTemp(Element));
			}
		}

		return ChildrenCache;
	}

	// For named branch elements: all the nodes under the branch should be children
	if (Type == EElementType::NamedBranch)
	{
		const FMovieGraphEvaluatedBranchConfig* BranchConfig = FlattenedGraph->BranchConfigMapping.Find(Name);
		if (!BranchConfig)
		{
			return ChildrenCache;
		}

		for (const TObjectPtr<UMovieGraphNode>& Node : BranchConfig->GetNodes())
		{
			ChildrenCache.Add(MakeElementFromNode(Node));
		}

		return ChildrenCache;
	}

	// For node elements: all overrideable properties on the node should be children
	if (Type == EElementType::Node)
	{
		for (TFieldIterator<FProperty> PropertyIterator(SettingsNode->GetClass()); PropertyIterator; ++PropertyIterator)
		{
			FProperty* NodeProperty = *PropertyIterator;
			if (UMovieGraphConfig::FindOverridePropertyForRealProperty(SettingsNode->GetClass(), NodeProperty))
			{
				TSharedPtr<FActiveRenderSettingsTreeElement> Element =
					MakeShared<FActiveRenderSettingsTreeElement>(NodeProperty->GetFName(), EElementType::Property);
				Element->SettingsNode = SettingsNode;
				Element->SettingsProperty = NodeProperty;
				Element->FlattenedGraph = FlattenedGraph;
				
				ChildrenCache.Add(MoveTemp(Element));
			}
		}
	}
	
	return ChildrenCache;
}

void FActiveRenderSettingsTreeElement::ClearCachedChildren() const
{
	ChildrenCache.Empty();
}

void SMovieGraphActiveRenderSettingsTreeItem::Construct(
	const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTable, const TSharedPtr<FActiveRenderSettingsTreeElement>& InTreeElement)
{
	WeakTreeElement = InTreeElement;

	SMultiColumnTableRow<TSharedPtr<FActiveRenderSettingsTreeElement>>::Construct(
		SMultiColumnTableRow<TSharedPtr<FActiveRenderSettingsTreeElement>>::FArguments()
			.Padding(0)	
		, InOwnerTable);
}

TSharedRef<SWidget> SMovieGraphActiveRenderSettingsTreeItem::GenerateWidgetForColumn(const FName& ColumnName)
{
	const TSharedPtr<FActiveRenderSettingsTreeElement> TreeElement = WeakTreeElement.Pin();
	if (!TreeElement.IsValid())
	{
		return SNullWidget::NullWidget;
	}
	
	if (ColumnName == ColumnID_Name)
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SExpanderArrow, SharedThis(this))
				.IndentAmount(16)
				.ShouldDrawWires(false)
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0, 0, 2.f, 0)
			[
				SNew(SImage)
				.Image(FSlateIcon(FAppStyle::GetAppStyleSetName(), "ClassIcon.Texture2D").GetIcon())
				.ToolTipText(LOCTEXT("RenderableBranchTooltip", "This branch outputs rendered images."))
				.Visibility_Lambda([TreeElement]()
				{
					return TreeElement->IsBranchRenderable() ? EVisibility::Visible : EVisibility::Collapsed;
				})
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Right)
			[
				SNew(STextBlock)
				.Text(FText::FromName(TreeElement->Name))
				.Font_Lambda([TreeElement]()
				{
					switch (TreeElement->Type)
					{
					case FActiveRenderSettingsTreeElement::EElementType::Root:
						return FCoreStyle::GetDefaultFontStyle("Bold", 12);
					case FActiveRenderSettingsTreeElement::EElementType::NamedBranch:
						return FCoreStyle::GetDefaultFontStyle("Bold", 10);
					case FActiveRenderSettingsTreeElement::EElementType::Property:
						return FCoreStyle::GetDefaultFontStyle("Italic", 9);
					default:
						return FCoreStyle::GetDefaultFontStyle("Regular", 9);
					}
				})
				.ColorAndOpacity_Lambda([TreeElement]()
				{
					return (TreeElement->Type == FActiveRenderSettingsTreeElement::EElementType::Property)
						? FSlateColor::UseSubduedForeground()
						: FSlateColor::UseForeground();
				})
			];
	}

	if (ColumnName == ColumnID_Value)
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Right)
			[
				SNew(STextBlock)
				.Text(FText::FromString(TreeElement->GetValue()))
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			];
	}

	return SNullWidget::NullWidget;
}

void SMovieGraphActiveRenderSettingsTabContent::Construct(const FArguments& InArgs)
{
	CurrentGraph = InArgs._Graph;

	// Add the two default root elements, "Globals" and "Branches", which are always visible. These elements will
	// generate the children which should be displayed under them in the tree.
	RootElements.Add(MakeShared<FActiveRenderSettingsTreeElement>(
		FActiveRenderSettingsTreeElement::RootName_Globals, FActiveRenderSettingsTreeElement::EElementType::Root));
	RootElements.Add(MakeShared<FActiveRenderSettingsTreeElement>(
		FActiveRenderSettingsTreeElement::RootName_Branches, FActiveRenderSettingsTreeElement::EElementType::Root));
	
	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(3.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SPositiveActionButton)
				.Text(FText::FromString("Evaluate Graph"))
				.Icon(FAppStyle::GetBrush("Icons.Refresh"))
				.OnClicked(this, &SMovieGraphActiveRenderSettingsTabContent::OnEvaluateGraphClicked)
			]
		]

		+ SVerticalBox::Slot()
		[
			SAssignNew(TreeView, STreeView<TSharedPtr<FActiveRenderSettingsTreeElement>>)
			.ItemHeight(28)
			.TreeItemsSource(&RootElements)
			.SelectionMode(ESelectionMode::Single)
			.OnGenerateRow(this, &SMovieGraphActiveRenderSettingsTabContent::GenerateTreeRow)
			.OnGetChildren(this, &SMovieGraphActiveRenderSettingsTabContent::GetChildrenForTree)
			.HeaderRow
			(
				SNew(SHeaderRow)

				+ SHeaderRow::Column(SMovieGraphActiveRenderSettingsTreeItem::ColumnID_Name)
				.DefaultLabel(LOCTEXT("NameColumnLabel", "Name"))
				.SortMode(EColumnSortMode::None)
				.FillWidth(0.75f)

				+ SHeaderRow::Column(SMovieGraphActiveRenderSettingsTreeItem::ColumnID_Value)
				.DefaultLabel(LOCTEXT("ValueColumnLabel", "Value"))
				.SortMode(EColumnSortMode::None)
				.FillWidth(0.25f)
			)
			.Visibility_Lambda([this]()
			{
				return FlattenedGraph.IsValid() ? EVisibility::Visible : EVisibility::Collapsed;
			})
		]

		+ SVerticalBox::Slot()
		.Padding(5.f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("NeedsEvaluationWarning", "Render settings have not been evaluated yet."))
			.Font(FCoreStyle::GetDefaultFontStyle("Italic", 10))
			.Visibility_Lambda([this]()
			{
				return FlattenedGraph.IsValid() ? EVisibility::Collapsed : EVisibility::Visible;
			})
		]
	];
}

void SMovieGraphActiveRenderSettingsTabContent::TraverseGraph()
{
	if (!CurrentGraph.IsValid())
	{
		return;
	}

	// Clear out the resolved cached children under the root nodes
	for (const TSharedPtr<FActiveRenderSettingsTreeElement>& RootElement : RootElements)
	{
		RootElement->ClearCachedChildren();
	}

	// TODO: Fill in the context via the UI (there are no widgets to provide this info yet)
	FMovieGraphTraversalContext Context;

	// Traverse the graph, and update the root elements
	FlattenedGraph = TStrongObjectPtr(CurrentGraph->CreateFlattenedGraph(Context));
	for (const TSharedPtr<FActiveRenderSettingsTreeElement>& RootElement : RootElements)
	{
		RootElement->FlattenedGraph = MakeWeakObjectPtr(FlattenedGraph.Get());
	}
	
	TreeView->RequestTreeRefresh();
}

FReply SMovieGraphActiveRenderSettingsTabContent::OnEvaluateGraphClicked()
{
	TraverseGraph();
	return FReply::Handled();
}

TSharedRef<ITableRow> SMovieGraphActiveRenderSettingsTabContent::GenerateTreeRow(
	TSharedPtr<FActiveRenderSettingsTreeElement> InTreeElement, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(SMovieGraphActiveRenderSettingsTreeItem, OwnerTable, InTreeElement)
		.Visibility_Lambda([InTreeElement]()
		{
			// If the root element has no children, hide it
			if (InTreeElement->Type == FActiveRenderSettingsTreeElement::EElementType::Root)
			{
				if (InTreeElement->GetChildren().IsEmpty())
				{
					return EVisibility::Hidden;
				}
			}

			return EVisibility::Visible;
		});
}

void SMovieGraphActiveRenderSettingsTabContent::GetChildrenForTree(
	TSharedPtr<FActiveRenderSettingsTreeElement> InItem, TArray<TSharedPtr<FActiveRenderSettingsTreeElement>>& OutChildren)
{
	OutChildren.Append(InItem->GetChildren());
}

#undef LOCTEXT_NAMESPACE
