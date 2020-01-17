// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/SCompoundWidget.h"
#include "GraphEditor.h"
#include "ContentBrowserDelegates.h"

class FNiagaraOverviewGraphViewModel;
struct FActionMenuContent;
class FMenuBuilder;
class UEdGraph;
class UEdGraphNode;

class SNiagaraOverviewGraph : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SNiagaraOverviewGraph) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, TSharedRef<FNiagaraOverviewGraphViewModel> InViewModel);

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

public:
	FRefreshAssetViewDelegate RefreshAssetView;

private:
	void ViewModelSelectionChanged();

	void GraphSelectionChanged(const TSet<UObject*>& SelectedNodes);

	void PreClose();

	/** Called to create context menu when right-clicking on graph */
	FActionMenuContent OnCreateGraphActionMenu(UEdGraph* InGraph, const FVector2D& InNodePosition, const TArray<UEdGraphPin*>& InDraggedPins, bool bAutoExpand, SGraphEditor::FActionMenuClosed InOnMenuClosed);

	void OnCreateComment();
	void OnClearIsolated();

	void OnNodeTitleCommitted(const FText& NewText, ETextCommit::Type CommitInfo, UEdGraphNode* NodeBeingChanged);

	void CreateAddEmitterMenuContent(FMenuBuilder& MenuBuilder, UEdGraph* InGraph);

	void ZoomToFit();
	void ZoomToFitAll();

	void OnAlignTop();
	void OnAlignMiddle();
	void OnAlignBottom();

	void OnDistributeNodesH();
	void OnDistributeNodesV();

	void LibraryCheckBoxStateChanged(ECheckBoxState InCheckbox);
	ECheckBoxState GetLibraryCheckBoxState() const;
	void TemplateCheckBoxStateChanged(ECheckBoxState InCheckbox);
	ECheckBoxState GetTemplateCheckBoxState() const;

	bool ShouldFilterEmitter(const FAssetData& AssetData);
private:
	TSharedPtr<FNiagaraOverviewGraphViewModel> ViewModel;
	TSharedPtr<SGraphEditor> GraphEditor;

	bool bUpdatingViewModelSelectionFromGraph;
	bool bUpdatingGraphSelectionFromViewModel;

	int32 ZoomToFitFrameDelay;

	static bool bShowLibraryOnly;
	static bool bShowTemplateOnly;
};