// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/STreeView.h"
#include "TraceServices/Model/Frames.h"

namespace Trace { class IAnalysisSession; }
struct FVariantTreeNode;

// Delegate called to get variant values to display
DECLARE_DELEGATE_TwoParams(FOnGetVariantValues, const Trace::FFrame& /*InFrame*/, TArray<TSharedRef<FVariantTreeNode>>& /*OutValues*/);

class SVariantValueView : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SVariantValueView) {}

	SLATE_EVENT(FOnGetVariantValues, OnGetVariantValues)

	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const Trace::IAnalysisSession& InAnalysisSession);

	/** Refresh the displayed variants. */
	void RequestRefresh(const Trace::FFrame& InFrame) { Frame = InFrame; bNeedsRefresh = true; }

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

	Trace::FFrame Frame;

	FOnGetVariantValues OnGetVariantValues;

	bool bNeedsRefresh;
};
