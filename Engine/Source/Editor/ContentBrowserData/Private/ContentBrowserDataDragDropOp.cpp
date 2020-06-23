// Copyright Epic Games, Inc. All Rights Reserved.

#include "ContentBrowserDataDragDropOp.h"
#include "AssetThumbnail.h"

TSharedRef<FContentBrowserDataDragDropOp> FContentBrowserDataDragDropOp::New(TArrayView<const FContentBrowserItem> InDraggedItems)
{
	TSharedRef<FContentBrowserDataDragDropOp> Operation = MakeShared<FContentBrowserDataDragDropOp>();

	Operation->Init(MoveTemp(InDraggedItems));

	Operation->Construct();
	return Operation;
}

void FContentBrowserDataDragDropOp::Init(TArrayView<const FContentBrowserItem> InDraggedItems)
{
	DraggedItems.Append(InDraggedItems.GetData(), InDraggedItems.Num());

	TArray<FAssetData> DraggedAssets;
	TArray<FString> DraggedPackagePaths;

	for (const FContentBrowserItem& DraggedItem : DraggedItems)
	{
		if (DraggedItem.IsFile())
		{
			DraggedFiles.Add(DraggedItem);

			FAssetData ItemAssetData;
			if (DraggedItem.Legacy_TryGetAssetData(ItemAssetData) && !ItemAssetData.IsRedirector())
			{
				DraggedAssets.Add(MoveTemp(ItemAssetData));
			}
		}

		if (DraggedItem.IsFolder())
		{
			DraggedFolders.Add(DraggedItem);

			FName ItemPackagePath;
			if (DraggedItem.Legacy_TryGetPackagePath(ItemPackagePath))
			{
				DraggedPackagePaths.Add(ItemPackagePath.ToString());
			}
		}
	}

	FAssetDragDropOp::Init(MoveTemp(DraggedAssets), MoveTemp(DraggedPackagePaths), nullptr);
}

void FContentBrowserDataDragDropOp::InitThumbnail()
{
	if (DraggedFiles.Num() > 0 && ThumbnailSize > 0)
	{
		// Create a thumbnail pool to hold the single thumbnail rendered
		ThumbnailPool = MakeShared<FAssetThumbnailPool>(1, /*InAreRealTileThumbnailsAllowed=*/false);

		// Create the thumbnail handle
		AssetThumbnail = MakeShared<FAssetThumbnail>(FAssetData(), ThumbnailSize, ThumbnailSize, ThumbnailPool);
		if (DraggedFiles[0].UpdateThumbnail(*AssetThumbnail))
		{
			// Request the texture then tick the pool once to render the thumbnail
			AssetThumbnail->GetViewportRenderTargetTexture();
			ThumbnailPool->Tick(0);
		}
		else
		{
			AssetThumbnail.Reset();
		}
	}
}

bool FContentBrowserDataDragDropOp::HasFiles() const
{
	return DraggedFiles.Num() > 0;
}

bool FContentBrowserDataDragDropOp::HasFolders() const
{
	return DraggedFolders.Num() > 0;
}

int32 FContentBrowserDataDragDropOp::GetTotalCount() const
{
	return DraggedItems.Num();
}

FText FContentBrowserDataDragDropOp::GetFirstItemText() const
{
	if (DraggedFiles.Num() > 0)
	{
		return DraggedFiles[0].GetDisplayName();
	}

	if (DraggedFolders.Num() > 0)
	{
		return FText::FromName(DraggedFolders[0].GetVirtualPath());
	}

	return FText::GetEmpty();
}
