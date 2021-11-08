// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SCompoundWidget.h"


struct FAssetData;
class SSequencerPlaylistItemWidget;
class USequencerPlaylist;
class USequencerPlaylistItem;
class USequencerPlaylistPlayer;


class SSequencerPlaylistPanel : public SCompoundWidget
{
	SLATE_BEGIN_ARGS(SSequencerPlaylistPanel) {}
	SLATE_END_ARGS()

public:
	static const float DefaultWidth;

	void Construct(const FArguments& InArgs, USequencerPlaylistPlayer* InPlayer);

private:
	void RegenerateSequenceList();

	TSharedRef<SWidget> OnPresetGeneratePresetsMenu();
	void OnSaveAsPreset();
	void OnLoadPreset(const FAssetData& InPreset);

	FReply OnClicked_PlayAll();
	FReply OnClicked_StopAll();
	FReply OnClicked_ResetAll();
	FReply OnClicked_AddSequence();

	FReply OnClicked_Item_Play(TSharedPtr<SSequencerPlaylistItemWidget> ItemWidget);
	FReply OnClicked_Item_Stop(TSharedPtr<SSequencerPlaylistItemWidget> ItemWidget);
	FReply OnClicked_Item_Reset(TSharedPtr<SSequencerPlaylistItemWidget> ItemWidget);
	FReply OnClicked_Item_Close(TSharedPtr<SSequencerPlaylistItemWidget> ItemWidget);

	USequencerPlaylist* GetCheckedPlaylist();

	// Drag and drop handling
	FReply HandleDragDetected(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent, int32 SlotIndex, SVerticalBox::FSlot* Slot);
	TOptional<SDragAndDropVerticalBox::EItemDropZone> HandleCanAcceptDrop(const FDragDropEvent& DragDropEvent, SDragAndDropVerticalBox::EItemDropZone DropZone, SVerticalBox::FSlot* Slot);
	FReply HandleAcceptDrop(const FDragDropEvent& DragDropEvent, SDragAndDropVerticalBox::EItemDropZone DropZone, int32 SlotIndex, SVerticalBox::FSlot* Slot);

private:
	TWeakObjectPtr<USequencerPlaylistPlayer> WeakPlayer;
	TSharedPtr<SDragAndDropVerticalBox> ItemList;
};


class FSequencerPlaylistItemDragDropOp : public FDragAndDropVerticalBoxOp
{
public:
	DRAG_DROP_OPERATOR_TYPE(FSequencerPlaylistItemDragDropOp, FDragAndDropVerticalBoxOp)

	TSharedPtr<SWidget> WidgetToShow;

	static TSharedRef<FSequencerPlaylistItemDragDropOp> New(int32 InSlotIndexBeingDragged, SVerticalBox::FSlot* InSlotBeingDragged, TSharedPtr<SWidget> InWidgetToShow);

public:
	virtual ~FSequencerPlaylistItemDragDropOp();

	virtual TSharedPtr<SWidget> GetDefaultDecorator() const override;
};


DECLARE_DELEGATE_RetVal_OneParam(FReply, FOnClickedSequencerPlaylistItem, TSharedPtr<SSequencerPlaylistItemWidget> /*ItemWidget*/);


class SSequencerPlaylistItemWidget : public SCompoundWidget
{
	SLATE_BEGIN_ARGS(SSequencerPlaylistItemWidget) {}
		SLATE_EVENT(FOnClickedSequencerPlaylistItem, OnPlayClicked)
		SLATE_EVENT(FOnClickedSequencerPlaylistItem, OnStopClicked)
		SLATE_EVENT(FOnClickedSequencerPlaylistItem, OnResetClicked)
		SLATE_EVENT(FOnClickedSequencerPlaylistItem, OnCloseClicked)
	SLATE_END_ARGS()

public:
	void Construct(const FArguments& InArgs, USequencerPlaylistItem* InItem);

	USequencerPlaylistItem* GetItem() { return WeakItem.Get(); }

private:
	TWeakObjectPtr<USequencerPlaylistItem> WeakItem;
};