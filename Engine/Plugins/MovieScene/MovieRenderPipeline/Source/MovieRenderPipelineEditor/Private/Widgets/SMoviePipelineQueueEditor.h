// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/Views/SListView.h"

struct FAssetData;
struct IMoviePipelineQueueTreeItem;
template<typename> class STreeView;
class FUICommandList;
class ITableRow;
class STableViewBase;
class UMoviePipelineExecutorJob;
class SWindow;
class UMoviePipelineQueue;
class SMoviePipelineQueueEditor;
class UMovieSceneCinematicShotSection;
struct FMoviePipelineQueueJobTreeItem;

DECLARE_DELEGATE_TwoParams(FOnMoviePipelineEditConfig, TWeakObjectPtr<UMoviePipelineExecutorJob>, TWeakObjectPtr<UMovieSceneCinematicShotSection>)

/**
 * Widget used to edit a Movie Pipeline Queue
 */
class SMoviePipelineQueueEditor : public SCompoundWidget
{
public:
	

	SLATE_BEGIN_ARGS(SMoviePipelineQueueEditor)
		{}
		SLATE_EVENT(FOnMoviePipelineEditConfig, OnEditConfigRequested)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	TSharedRef<SWidget> MakeAddSequenceJobButton();
	TSharedRef<SWidget> RemoveSelectedJobButton();
	TSharedRef<SWidget> OnGenerateNewJobFromAssetMenu();
private:
	// SWidget Interface
	FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent);
	void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime);
	// ~SWidget Interface
private:
	void OnCreateJobFromAsset(const FAssetData& InAsset);

	TSharedRef<ITableRow> OnGenerateRow(TSharedPtr<IMoviePipelineQueueTreeItem> Item, const TSharedRef<STableViewBase>& Tree);

	void OnGetChildren(TSharedPtr<IMoviePipelineQueueTreeItem> Item, TArray<TSharedPtr<IMoviePipelineQueueTreeItem>>& OutChildItems);

	FReply OnDragDropTarget(TSharedPtr<FDragDropOperation> InOperation);

	bool CanDragDropTarget(TSharedPtr<FDragDropOperation> InOperation);

	void OnDeleteSelected();
	bool CanDeleteSelected() const;
	FReply DeleteSelected();

	void ReconstructTree();

private:
	TArray<TSharedPtr<IMoviePipelineQueueTreeItem>> RootNodes;
	TSharedPtr<STreeView<TSharedPtr<IMoviePipelineQueueTreeItem>>> TreeView;
	TSharedPtr<FUICommandList> CommandList;
	uint32 CachedQueueSerialNumber;

	FOnMoviePipelineEditConfig OnEditConfigRequested;
};