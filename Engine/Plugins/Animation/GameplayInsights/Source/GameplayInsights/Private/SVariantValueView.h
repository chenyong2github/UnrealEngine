// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/STreeView.h"

namespace Trace { class IAnalysisSession; }
struct FVariantTreeNode;

// Delegate called to get variant values to display
DECLARE_DELEGATE_TwoParams(FOnGetVariantValues, double /*InTime*/, TArray<TSharedRef<FVariantTreeNode>>& /*OutValues*/);

class SVariantValueView : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SVariantValueView) {}

	SLATE_EVENT(FOnGetVariantValues, OnGetVariantValues)

	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const Trace::IAnalysisSession& InAnalysisSession);

	/** Refresh the displayed variants. */
	void RequestRefresh(double InTimeMarker) { TimeMarker = InTimeMarker; bNeedsRefresh = true; }

private:
	// Generate a row widget for a property item
	TSharedRef<ITableRow> HandleGeneratePropertyRow(TSharedRef<FVariantTreeNode> Item, const TSharedRef<STableViewBase>& OwnerTable);

	// Get the children of a property item
	void HandleGetPropertyChildren(TSharedRef<FVariantTreeNode> InItem, TArray<TSharedRef<FVariantTreeNode>>& OutChildren);

	// Refresh the nodes
	void RefreshNodes();

private:
	const Trace::IAnalysisSession* AnalysisSession;

	TSharedPtr<STreeView<TSharedRef<FVariantTreeNode>>> VariantTreeView;

	TArray<TSharedRef<FVariantTreeNode>> VariantTreeNodes;

	double TimeMarker;

	FOnGetVariantValues OnGetVariantValues;

	bool bNeedsRefresh;
};
