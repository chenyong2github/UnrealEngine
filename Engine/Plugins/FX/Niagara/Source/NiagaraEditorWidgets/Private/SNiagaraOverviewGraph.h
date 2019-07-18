// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/SCompoundWidget.h"
#include "GraphEditor.h"

class FNiagaraSystemViewModel;
struct FActionMenuContent;
class FMenuBuilder;
class UEdGraph;
class UEdGraphNode;

class SNiagaraOverviewGraph : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SNiagaraOverviewGraph) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, TSharedRef<FNiagaraSystemViewModel> InSystemViewModel);

private:
	void GraphSelectionChanged(const TSet<UObject*>& SelectedNodes);

	void OverviewSelectionChanged();

	/** Called to create context menu when right-clicking on graph */
	FActionMenuContent OnCreateGraphActionMenu(UEdGraph* InGraph, const FVector2D& InNodePosition, const TArray<UEdGraphPin*>& InDraggedPins, bool bAutoExpand, SGraphEditor::FActionMenuClosed InOnMenuClosed);

	void OnCreateComment();

	void OnNodeTitleCommitted(const FText& NewText, ETextCommit::Type CommitInfo, UEdGraphNode* NodeBeingChanged);

	void CreateAddEmitterMenuContent(FMenuBuilder& MenuBuilder, UEdGraph* InGraph);
	
private:
	TSharedPtr<FNiagaraSystemViewModel> SystemViewModel;
	TSharedPtr<SGraphEditor> GraphEditor;

	bool bUpdatingOverviewSelectionFromGraph;
	bool bUpdatingGraphSelectionFromOverview;
};
