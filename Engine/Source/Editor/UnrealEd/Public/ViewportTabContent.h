// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EditorViewportLayout.h"
#include "EditorViewportLayoutTwoPanes.h"
#include "Widgets/Docking/SDockTab.h"

/**
 * Represents the content in a viewport tab in an editor.
 * Each SDockTab holding viewports in an editor contains and owns one of these.
 */


class UNREALED_API FViewportTabContent
{
public:

	virtual ~FViewportTabContent() {}
	/** Returns whether the tab is currently shown */
	bool IsVisible() const;

	/** @return True if this viewport belongs to the tab given */
	bool BelongsToTab(TSharedRef<class SDockTab> InParentTab) const;


	/**
	* Returns whether the named layout is currently selected
	*
	* @param ConfigurationName		The name of the layout (for the names in namespace LevelViewportConfigurationNames)
	* @return						True, if the named layout is currently active
	*/
	bool IsViewportConfigurationSet(const FName& ConfigurationName) const;

	virtual void SetViewportConfiguration(const FName& ConfigurationName) {}

	void PerformActionOnViewports(TFunction<void(FName Name, TSharedPtr<IEditorViewportLayoutEntity>)> &TFuncPtr);

	DECLARE_EVENT(FViewportTabContent, FViewportTabContentLayoutChangedEvent);
	virtual FViewportTabContentLayoutChangedEvent& OnViewportTabContentLayoutChanged() { return OnViewportTabContentLayoutChangedEvent; };

protected:
	FViewportTabContentLayoutChangedEvent OnViewportTabContentLayoutChangedEvent;

	TWeakPtr<class SDockTab> ParentTab;

	FString LayoutString;

	/** Current layout */
	TSharedPtr< class FEditorViewportLayout > ActiveViewportLayout;

	TOptional<FName> PreviouslyFocusedViewport;
};
