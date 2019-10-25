// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/Docking/SDockTab.h"
#include "ViewportTabContent.h"

class FEditorViewportLayout;
/**
 * Represents the content in a viewport tab in an editor.
 * Each SDockTab holding viewports in an editor contains and owns one of these.
 */
class UNREALED_API FEditorViewportTabContent : public FViewportTabContent, public TSharedFromThis<FEditorViewportTabContent>
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
	
	const TSharedPtr< class FEditorViewportLayout > GetActiveViewportLayout() const
	{
		return ActiveViewportLayout;
	}
	   
	TSharedPtr< class FEditorViewportLayout > ConstructViewportLayoutByTypeName(const FName& TypeName, bool bSwitchingLayouts);

	void Initialize(TFunction<TSharedRef<SEditorViewport>(void)> Func, TSharedPtr<SDockTab> InParentTab, const FString& InLayoutString);

	/**
	* Sets the current layout by changing the contained layout object
	* 
	* @param ConfigurationName		The name of the layout (for the names in namespace EditorViewportConfigurationNames)
	*/
	void SetViewportConfiguration(TFunction<TSharedRef<SEditorViewport>(void)> &Func, const FName& ConfigurationName);

	void SetViewportConfiguration(const FName& ConfigurationName) override;
	TSharedPtr<SEditorViewport> GetFirstViewport();

	void UpdateViewportTabWidget(TFunction<TSharedRef<SEditorViewport>(void)> &Func);

	/** Save any configuration required to persist state for this viewport layout */
	void SaveConfig() const;


private:
	TFunction<TSharedRef<SEditorViewport>(void)> ViewportCreationFunc;
};
