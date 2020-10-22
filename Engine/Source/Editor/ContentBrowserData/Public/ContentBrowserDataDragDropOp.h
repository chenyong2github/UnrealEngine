// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Input/DragAndDrop.h"
#include "DragAndDrop/AssetDragDropOp.h"
#include "ContentBrowserItem.h"

class CONTENTBROWSERDATA_API FContentBrowserDataDragDropOp : public FAssetDragDropOp
{
public:
	DRAG_DROP_OPERATOR_TYPE(FContentBrowserDataDragDropOp, FAssetDragDropOp)

	static TSharedRef<FContentBrowserDataDragDropOp> New(TArrayView<const FContentBrowserItem> InDraggedItems);

	static TSharedRef<FContentBrowserDataDragDropOp> Legacy_New(TArrayView<const FAssetData> InAssetData, TArrayView<const FString> InAssetPaths = TArrayView<const FString>(), UActorFactory* InActorFactory = nullptr);

	const TArray<FContentBrowserItem>& GetDraggedItems() const
	{
		return DraggedItems;
	}

	const TArray<FContentBrowserItem>& GetDraggedFiles() const
	{
		return DraggedFiles;
	}

	const TArray<FContentBrowserItem>& GetDraggedFolders() const
	{
		return DraggedFolders;
	}

private:
	void Init(TArrayView<const FContentBrowserItem> InDraggedItems);
	void LegacyInit(TArrayView<const FAssetData> InAssetData, TArrayView<const FString> InAssetPaths, UActorFactory* ActorFactory);
	virtual void InitThumbnail() override;

	virtual bool HasFiles() const override;
	virtual bool HasFolders() const override;

	virtual int32 GetTotalCount() const override;
	virtual FText GetFirstItemText() const override;

private:
	TArray<FContentBrowserItem> DraggedItems;
	TArray<FContentBrowserItem> DraggedFiles;
	TArray<FContentBrowserItem> DraggedFolders;
};
