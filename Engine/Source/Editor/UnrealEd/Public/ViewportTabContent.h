// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Represents the content in a viewport tab in an editor.
 * Each SDockTab holding viewports in an editor contains and owns one of these.
 */
class UNREALED_API FViewportTabContent : public TSharedFromThis<FViewportTabContent>
{
public:
	/** Returns whether the tab is currently shown */
	bool IsVisible() const;

	/** @return True if this viewport belongs to the tab given */
	bool BelongsToTab(TSharedRef<class SDockTab> InParentTab) const;

	/** @return The string used to identify the layout of this viewport tab */
	const FString& GetLayoutString() const
	{
		return LayoutString;
	}

protected:
	TWeakPtr<class SDockTab> ParentTab;
	FString LayoutString;

	/** Current layout */
	TSharedPtr< class FAssetViewportLayout > ActiveViewportLayout;

	TOptional<FName> PreviouslyFocusedViewport;
};
