// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "DragAndDrop/CollectionDragDropOp.h"
#include "ICollectionManager.h"
#include "CollectionManagerModule.h"
#include "AssetRegistryModule.h"
#include "IAssetRegistry.h"
#include "SAssetTagItem.h"

TArray<FAssetData> FCollectionDragDropOp::GetAssets() const
{
	ICollectionManager& CollectionManager = FModuleManager::LoadModuleChecked<FCollectionManagerModule>("CollectionManager").Get();
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

	TArray<FName> AssetPaths;
	for (const FCollectionNameType& CollectionNameType : Collections)
	{
		TArray<FName> CollectionAssetPaths;
		CollectionManager.GetAssetsInCollection(CollectionNameType.Name, CollectionNameType.Type, CollectionAssetPaths);
		AssetPaths.Append(CollectionAssetPaths);
	}

	TArray<FAssetData> AssetDatas;
	AssetDatas.Reserve(AssetPaths.Num());
	for (const FName& AssetPath : AssetPaths)
	{
		FAssetData AssetData = AssetRegistry.GetAssetByObjectPath(AssetPath);
		if (AssetData.IsValid())
		{
			AssetDatas.AddUnique(AssetData);
		}
	}
	
	return AssetDatas;
}

TSharedPtr<SWidget> FCollectionDragDropOp::GetDefaultDecorator() const
{
	return SNew(SBorder)
		.Padding(0)
		.BorderImage(FEditorStyle::GetBrush("ContentBrowser.AssetDragDropTooltipBackground"))
		//.BorderImage(FEditorStyle::GetBrush("ToolPanel.GroupBorder"))
		[
			SNew(SAssetTagItem)
			.ViewMode(AssetTagViewMode)
			.DisplayName(this, &FCollectionDragDropOp::GetDecoratorText)
		];
}

FText FCollectionDragDropOp::GetDecoratorText() const
{
	if (CurrentHoverText.IsEmpty() && Collections.Num() > 0)
	{
		return (Collections.Num() == 1)
			? FText::FromName(Collections[0].Name)
			: FText::Format(NSLOCTEXT("ContentBrowser", "CollectionDragDropDescription", "{0} and {1} {1}|plural(one=other,other=others)"), FText::FromName(Collections[0].Name), Collections.Num() - 1);
	}

	return CurrentHoverText;
}
