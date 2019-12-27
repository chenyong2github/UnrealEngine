// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Input/DragAndDrop.h"
#include "DragAndDrop/DecoratedDragDropOp.h"
#include "CollectionManagerTypes.h"
#include "AssetData.h"
#include "AssetTagItemTypes.h"

class UNREALED_API FCollectionDragDropOp : public FDecoratedDragDropOp
{
public:
	DRAG_DROP_OPERATOR_TYPE(FCollectionDragDropOp, FDecoratedDragDropOp)

	/** Data for the collections this item represents */
	TArray<FCollectionNameType> Collections;

	static TSharedRef<FCollectionDragDropOp> New(TArray<FCollectionNameType> InCollections, const EAssetTagItemViewMode InAssetTagViewMode = EAssetTagItemViewMode::Standard)
	{
		TSharedRef<FCollectionDragDropOp> Operation = MakeShareable(new FCollectionDragDropOp);
		
		Operation->AssetTagViewMode = InAssetTagViewMode;
		Operation->MouseCursor = EMouseCursor::GrabHandClosed;
		Operation->Collections = MoveTemp(InCollections);
		Operation->Construct();

		return Operation;
	}
	
public:
	/** @return The assets from this drag operation */
	TArray<FAssetData> GetAssets() const;

	virtual TSharedPtr<SWidget> GetDefaultDecorator() const override;

private:
	FText GetDecoratorText() const;

	EAssetTagItemViewMode AssetTagViewMode = EAssetTagItemViewMode::Standard;
};
