// Copyright Epic Games, Inc. All Rights Reserved.

#include "ContentBrowserMenuContexts.h"

#include "Containers/UnrealString.h"
#include "ContentBrowserDataSubsystem.h"
#include "SAssetView.h"
#include "SContentBrowser.h"
#include "UObject/UnrealNames.h"
#include "ToolMenuSection.h"

const TArray<FAssetData>& UContentBrowserAssetViewContextMenuContext::GetSelectedAssets() const
{
	if (!bHasInitSelectedAssets)
	{
		UContentBrowserAssetViewContextMenuContext* MutableThis = const_cast<UContentBrowserAssetViewContextMenuContext*>(this);
		MutableThis->bHasInitSelectedAssets = true;
		if (AssetView.IsValid())
		{
			MutableThis->SelectedAssets = AssetView.Pin()->GetSelectedAssets();
		}
	}

	return SelectedAssets;
}

FName UContentBrowserToolbarMenuContext::GetCurrentPath() const
{
	if (TSharedPtr<SContentBrowser> Browser = ContentBrowser.Pin())
	{
		return *Browser->GetCurrentPath(EContentBrowserPathType::Virtual);
	}

	return NAME_None;
}

bool UContentBrowserToolbarMenuContext::CanWriteToCurrentPath() const
{
	if (TSharedPtr<SContentBrowser> Browser = ContentBrowser.Pin())
	{
		return Browser->CanWriteToCurrentPath();
	}

	return false;
}