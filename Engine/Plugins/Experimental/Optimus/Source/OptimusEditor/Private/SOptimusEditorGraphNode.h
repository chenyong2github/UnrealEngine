// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "SGraphNode.h"
#include "Widgets/DeclarativeSyntaxSupport.h"


class SScrollBar;
class UOptimusEditorGraphNode;
class UOptimusNode;
class UOptimusNodePin;
enum class EOptimusNodePinDirection : uint8;
template<typename ItemType> class STreeView;

class SOptimusEditorGraphNode : 
	public SGraphNode
{
public:
	SLATE_BEGIN_ARGS(SOptimusEditorGraphNode)
		: _GraphNode(nullptr)
	{}
		SLATE_ARGUMENT(UOptimusEditorGraphNode*, GraphNode)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	// SGraphNode overrides
	void EndUserInteraction() const override;
	void AddPin( const TSharedRef<SGraphPin>& PinToAdd ) override;
	// const FSlateBrush* GetNodeBodyBrush() const override;
	TSharedPtr<SGraphPin> GetHoveredPin(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) const override;

private:
	UOptimusEditorGraphNode *GetEditorGraphNode() const;
	UOptimusNode* GetModelNode() const;

	// STreeView helper functions
	EVisibility GetInputTreeVisibility() const;
	EVisibility GetOutputTreeVisibility() const;
	TSharedRef<ITableRow> MakeTableRowWidget(UOptimusNodePin* InItem, const TSharedRef<STableViewBase>& OwnerTable);
	void HandleGetChildrenForTree(UOptimusNodePin* InItem, TArray<UOptimusNodePin*>& OutChildren);
	void HandleExpansionChanged(UOptimusNodePin* InItem, bool bExpanded);

	FText GetPinLabel(TWeakPtr<SGraphPin> InWeakGraphPin) const;

	// Collapsible input pins
	TSharedPtr<STreeView<UOptimusNodePin*>> InputTree;

	// Collapsible input pins
	TSharedPtr<STreeView<UOptimusNodePin*>> OutputTree;

	TSharedPtr<SScrollBar> TreeScrollBar;

	TMap<const UEdGraphPin*, TSharedPtr<SGraphPin>> PinWidgetMap;

	// A paired list of widgets to map from labels to pin to support labels participating in
	// pin hovering.
	TArray<TSharedRef<SWidget>> HoverWidgetLabels;
	TArray<TSharedRef<SGraphPin>> HoverWidgetPins;
};
