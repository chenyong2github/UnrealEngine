// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SWidget.h"
#include "Widgets/Views/SHeaderRow.h"
#include "SceneOutlinerPublicTypes.h"
#include "ISceneOutlinerColumn.h"
#include "ISceneOutlinerTreeItem.h"

class ISceneOutliner;
template<typename ItemType> class STableRow;

/**
 * A gutter for the SceneOutliner which handles setting and visualizing item visibility
 */
class FSceneOutlinerGutter : public ISceneOutlinerColumn
{
public:

	/**	Constructor */
	FSceneOutlinerGutter(ISceneOutliner& Outliner);

	virtual ~FSceneOutlinerGutter() {}

	static FName GetID() { return FSceneOutlinerBuiltInColumnTypes::Gutter(); }
	
	// -----------------------------------------
	// ISceneOutlinerColumn Implementation
	virtual FName GetColumnID() override;

	virtual SHeaderRow::FColumn::FArguments ConstructHeaderRowColumn() override;

	virtual const TSharedRef< SWidget > ConstructRowWidget( FSceneOutlinerTreeItemRef TreeItem, const STableRow<FSceneOutlinerTreeItemPtr>& Row ) override;
	
	virtual void Tick(double InCurrentTime, float InDeltaTime) override;

	virtual bool SupportsSorting() const override { return true; }

	virtual void SortItems(TArray<FSceneOutlinerTreeItemPtr>& RootItems, const EColumnSortMode::Type SortMode) const override;
	// -----------------------------------------

	/** Check whether the specified item is visible */
	FORCEINLINE bool IsItemVisible(const ISceneOutlinerTreeItem& Item)
	{
		return VisibilityCache.GetVisibility(Item);
	}

private:

	/** Weak pointer back to the scene outliner - required for setting visibility on current selection. */
	TWeakPtr<ISceneOutliner> WeakOutliner;

	/** Get and cache visibility for items. Cached per-frame to avoid expensive recursion. */
	FSceneOutlinerVisibilityCache VisibilityCache;
};
