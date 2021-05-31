// Copyright Epic Games, Inc. All Rights Reserved.

#include "SLensDataListItem.h"

#include "IStructureDetailsView.h"
#include "LensFile.h"
#include "ScopedTransaction.h"
#include "Widgets/Input/SButton.h"


#define LOCTEXT_NAMESPACE "LensDataListItem"


FLensDataListItem::FLensDataListItem(ULensFile* InLensFile, ELensDataCategory InCategory, int32 InSubCategoryIndex, FOnDataRemoved InOnDataRemovedCallback)
	: Category(InCategory)
	, SubCategoryIndex(InSubCategoryIndex)
	, WeakLensFile(InLensFile)
	, OnDataRemovedCallback(InOnDataRemovedCallback)
{
}


FEncoderDataListItem::FEncoderDataListItem(ULensFile* InLensFile, ELensDataCategory InCategory, float InInputValue, int32 InIndex)
	: FLensDataListItem(InLensFile, InCategory, INDEX_NONE, nullptr)
	, InputValue(InInputValue)
	, EntryIndex()
{
}

void FEncoderDataListItem::OnRemoveRequested() const
{
}

TSharedRef<ITableRow> FEncoderDataListItem::MakeTreeRowWidget(const TSharedRef<STableViewBase>& InOwnerTable)
{
	return SNew(SLensDataItem, InOwnerTable, AsShared())
		.EntryLabel(LOCTEXT("EncoderLabel", "Input:"))
		.EntryValue(InputValue)
		.AllowRemoval(true);
}

FFocusDataListItem::FFocusDataListItem(ULensFile* InLensFile, ELensDataCategory InCategory, int32 InSubCategoryIndex, float InFocus, FOnDataRemoved InOnDataRemovedCallback)
	: FLensDataListItem(InLensFile, InCategory, InSubCategoryIndex, InOnDataRemovedCallback)
	, Focus(InFocus)
{
}

TSharedRef<ITableRow> FFocusDataListItem::MakeTreeRowWidget(const TSharedRef<STableViewBase>& InOwnerTable)
{
	return SNew(SLensDataItem, InOwnerTable, AsShared())
		.EntryLabel(LOCTEXT("FocusLabel", "Focus: "))
		.EntryValue(Focus)
		.AllowRemoval(SubCategoryIndex == INDEX_NONE);	
}

void FFocusDataListItem::OnRemoveRequested() const
{
	if (ULensFile* LensFilePtr = WeakLensFile.Get())
	{
		FScopedTransaction Transaction(LOCTEXT("RemoveFocusPointsTransaction", "Remove Focus Points"));
		LensFilePtr->Modify();

		LensFilePtr->RemoveFocusPoint(Category, Focus);
		OnDataRemovedCallback.ExecuteIfBound(Focus, TOptional<float>());
	}
}

FZoomDataListItem::FZoomDataListItem(ULensFile* InLensFile, ELensDataCategory InCategory, int32 InSubCategoryIndex, const TSharedRef<FFocusDataListItem> InParent, float InZoom, FOnDataRemoved InOnDataRemovedCallback)
	: FLensDataListItem(InLensFile, InCategory, InSubCategoryIndex, InOnDataRemovedCallback)
	, Zoom(InZoom)
	, WeakParent(InParent)
{
}

TSharedRef<ITableRow> FZoomDataListItem::MakeTreeRowWidget(const TSharedRef<STableViewBase>& InOwnerTable)
{
	return SNew(SLensDataItem, InOwnerTable, AsShared())
		.EntryLabel(LOCTEXT("ZoomLabel", "Zoom: "))
		.EntryValue(Zoom)
		.AllowRemoval(SubCategoryIndex == INDEX_NONE);	
}

void FZoomDataListItem::OnRemoveRequested() const
{
	if (ULensFile* LensFilePtr = WeakLensFile.Get())
	{
		if(TSharedPtr<FFocusDataListItem> ParentItem = WeakParent.Pin())
		{
			FScopedTransaction Transaction(LOCTEXT("RemoveZoomPointTransaction", "Remove Zoom Point"));
			LensFilePtr->Modify();
			LensFilePtr->RemoveZoomPoint(Category, ParentItem->Focus, Zoom);
			OnDataRemovedCallback.ExecuteIfBound(ParentItem->Focus, Zoom);
		}
	}
}

TOptional<float> FZoomDataListItem::GetFocus() const
{
	if (TSharedPtr<FFocusDataListItem> ParentItem = WeakParent.Pin())
	{
		return ParentItem->GetFocus();
	}

	return TOptional<float>();
}

void SLensDataItem::Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& OwnerTable, const TSharedRef<FLensDataListItem> InItemData)
{
	WeakItem = InItemData;

	const float EntryValue = InArgs._EntryValue;

	STableRow<TSharedPtr<FLensDataListItem>>::Construct(
		STableRow<TSharedPtr<FLensDataListItem>>::FArguments()
		.Content()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.Padding(5.0f, 5.0f)
			.AutoWidth()
			[
				SNew(STextBlock)
				.Text(InArgs._EntryLabel)
			]
			+ SHorizontalBox::Slot()
			.Padding(5.0f, 5.0f)
			.FillWidth(1.0f)
			.HAlign(HAlign_Left)
			[
				SNew(STextBlock)
				.Text_Lambda([EntryValue](){ return FText::AsNumber(EntryValue); })
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SButton)
				.IsEnabled(InArgs._AllowRemoval)
				.OnClicked(this, &SLensDataItem::OnRemovePointClicked)
				[
					SNew(STextBlock)
					.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.10"))
					.Text(FText::FromString(FString(TEXT("\xf00d"))) /*fa-times*/)
				]
			]
		], OwnerTable);
}

FReply SLensDataItem::OnRemovePointClicked() const
{
	if (TSharedPtr<FLensDataListItem> Item = WeakItem.Pin())
	{
		Item->OnRemoveRequested();
	}

	return FReply::Handled();
}


#undef LOCTEXT_NAMESPACE /* LensDataListItem */



