// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/STreeView.h"

namespace Insights { class ITimingViewSession; }
namespace Insights { enum class ETimeChangedFlags; }
namespace Trace { class IAnalysisSession; }
class FAnimationSharedData;
class IInsightsManager;
class FAnimGraphSchematicNode;
class FAnimGraphSchematicPropertyNode;
class SSearchBox;
class SHeaderRow;
class SComboButton;
class SSplitter;
enum class EAnimGraphSchematicFilterState;

class SAnimGraphSchematicView : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SAnimGraphSchematicView) {}

	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, uint64 InAnimInstanceId, Insights::ITimingViewSession& InTimingViewSession, const Trace::IAnalysisSession& InAnalysisSession);

	uint64 GetAnimInstanceId() const { return AnimInstanceId; }

private:
	// Generate a row widget for an item
	TSharedRef<ITableRow> HandleGenerateRow(TSharedRef<FAnimGraphSchematicNode> Item, const TSharedRef<STableViewBase>& OwnerTable);

	// Get the children of an item
	void HandleGetChildren(TSharedRef<FAnimGraphSchematicNode> InItem, TArray<TSharedRef<FAnimGraphSchematicNode>>& OutChildren);

	// Generate a row widget for a property item
	TSharedRef<ITableRow> HandleGeneratePropertyRow(TSharedRef<FAnimGraphSchematicPropertyNode> Item, const TSharedRef<STableViewBase>& OwnerTable);

	// Get the children of a property item
	void HandleGetPropertyChildren(TSharedRef<FAnimGraphSchematicPropertyNode> InItem, TArray<TSharedRef<FAnimGraphSchematicPropertyNode>>& OutChildren);

	// Handle the time marker being scrubbed
	void HandleTimeMarkerChanged(Insights::ETimeChangedFlags InFlags, double InTimeMarker);

	// Handle tree selection changing
	void HandleSelectionChanged(TSharedPtr<FAnimGraphSchematicNode> InNode, ESelectInfo::Type InSelectInfo);

	// Refresh the unfiltered and filtered nodes
	void RefreshNodes();

	// Recursive helper to filter tree
	EAnimGraphSchematicFilterState RefreshFilter_Helper(const TSharedRef<FAnimGraphSchematicNode>& InNode);

	// Refresh the displayed columns
	void RefreshColumns();

	// Refresh the filtered nodes
	void RefreshFilter();

	// Refresh the details panel
	void RefreshDetails(const TArray<TSharedRef<FAnimGraphSchematicNode>>& InNodes);

	// Handle getting content for the view menu
	TSharedRef<SWidget> HandleGetViewMenuContent();

	// Handle inverting the color of the button text when hovered
	FSlateColor GetViewButtonForegroundColor() const;

private:
	const Trace::IAnalysisSession* AnalysisSession;

	Insights::ITimingViewSession* TimingViewSession;

	TSharedPtr<SSearchBox> SearchBox;

	TSharedPtr<SHeaderRow> HeaderRow;

	TSharedPtr<SSplitter> Splitter;

	TSharedPtr<STreeView<TSharedRef<FAnimGraphSchematicNode>>> TreeView;

	TSharedPtr<STreeView<TSharedRef<FAnimGraphSchematicPropertyNode>>> PropertyTreeView;

	TSharedPtr<SComboButton> ViewButton;

	TWeakPtr<SVerticalBox> DetailsContentBox;

	TArray<TSharedRef<FAnimGraphSchematicNode>> UnfilteredNodes;

	TArray<TSharedRef<FAnimGraphSchematicNode>> FilteredNodes;

	TArray<TSharedRef<FAnimGraphSchematicNode>> LinearNodes;

	TArray<TSharedRef<FAnimGraphSchematicPropertyNode>> PropertyNodes;

	TSet<int32> SelectedNodeIds;

	struct FColumnState
	{
		int32 SortIndex = 0;
		bool bEnabled = false;
	};

	TMap<FName, FColumnState> Columns;

	FText FilterText;

	double TimeMarker;

	uint64 AnimInstanceId;
};
