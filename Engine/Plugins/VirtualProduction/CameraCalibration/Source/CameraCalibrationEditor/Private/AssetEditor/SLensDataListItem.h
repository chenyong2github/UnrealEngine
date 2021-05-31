// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/SCompoundWidget.h"

#include "CameraCalibrationEditorCommon.h"
#include "LensFile.h"
#include "SLensFilePanel.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Views/STreeView.h"



DECLARE_DELEGATE_TwoParams(FOnDataRemoved, float /** Focus */, TOptional<float> /** Possible Zoom */);

/**
* Data entry item
*/
class FLensDataListItem : public TSharedFromThis<FLensDataListItem>
{
public:
	FLensDataListItem(ULensFile* InLensFile, ELensDataCategory InCategory, int32 InSubCategoryIndex, FOnDataRemoved InOnDataRemovedCallback);

	virtual ~FLensDataListItem() = default;
	
	virtual void OnRemoveRequested() const = 0;
	virtual TSharedRef<ITableRow> MakeTreeRowWidget(const TSharedRef<STableViewBase>& InOwnerTable) = 0;
	virtual TOptional<float> GetFocus() const { return TOptional<float>(); }
	virtual int32 GetIndex() const { return INDEX_NONE; }

	/** Lens data category of that entry */
	ELensDataCategory Category;

	/** Used to know if it's a root category or not */
	int32 SubCategoryIndex;

	/** LensFile we're editing */
	TWeakObjectPtr<ULensFile> WeakLensFile;

	/** Children of this item */
	TArray<TSharedPtr<FLensDataListItem>> Children;

	/** Delegate to call when data is removed */
	FOnDataRemoved OnDataRemovedCallback;
};

/**
 * Encoder item
 */
class FEncoderDataListItem : public FLensDataListItem
{
public:
	FEncoderDataListItem(ULensFile* InLensFile, ELensDataCategory InCategory, float InInput, int32 InIndex);

	virtual void OnRemoveRequested() const override;
	virtual TSharedRef<ITableRow> MakeTreeRowWidget(const TSharedRef<STableViewBase>& InOwnerTable) override;

	/** Encoder input */
	float InputValue;

	/** Identifier for this focus point */
	int32 EntryIndex;
};

/**
 * Data entry item
 */
class FFocusDataListItem : public FLensDataListItem
{
public:
	FFocusDataListItem(ULensFile* InLensFile, ELensDataCategory InCategory, int32 InSubCategoryIndex, float InFocus, FOnDataRemoved InOnDataRemovedCallback);

	virtual TSharedRef<ITableRow> MakeTreeRowWidget(const TSharedRef<STableViewBase>& InOwnerTable) override;
	virtual void OnRemoveRequested() const override;
	virtual TOptional<float> GetFocus() const override { return Focus; }

	/** Focus value of this item */
	float Focus;
};

/**
 * Zoom data entry item
 */
class FZoomDataListItem : public FLensDataListItem
{
public:
	FZoomDataListItem(ULensFile* InLensFile, ELensDataCategory InCategory, int32 InSubCategoryIndex, const TSharedRef<FFocusDataListItem> InParent, float InZoom, FOnDataRemoved InOnDataRemovedCallback);

	//~ Begin FLensDataListItem interface
	virtual TSharedRef<ITableRow> MakeTreeRowWidget(const TSharedRef<STableViewBase>& InOwnerTable) override;
	virtual void OnRemoveRequested() const override;
	virtual TOptional<float> GetFocus() const override;
	//~ End FLensDataListItem interface

	/** Zoom value of this item */
	float Zoom = 0.0f;

	/** Focus this zoom point is associated with */
	TWeakPtr<FFocusDataListItem> WeakParent;
};

/**
 * Widget a focus point entry
 */
class SLensDataItem : public STableRow<TSharedPtr<FLensDataListItem>>
{
	SLATE_BEGIN_ARGS(SLensDataItem) {}

		SLATE_ARGUMENT(FText, EntryLabel)

		SLATE_ARGUMENT(float, EntryValue)

		SLATE_ARGUMENT(bool, AllowRemoval)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& OwnerTable, const TSharedRef<FLensDataListItem> InItemData);

private:
	FReply OnRemovePointClicked() const;

private:

	/** WeakPtr to source data item */
	TWeakPtr<FLensDataListItem> WeakItem;
};

/**
 * Widget representing a zoom data entry row
 */
class SZoomDataItem : public STableRow<TSharedPtr<FZoomDataListItem>>
{
	SLATE_BEGIN_ARGS(SZoomDataItem) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& OwnerTable, const TSharedRef<FZoomDataListItem> InItemData);

private:
	FReply OnRemovePointClicked();

private:

	/** WeakPtr to source data item */
	TWeakPtr<FZoomDataListItem> WeakItem;
};


