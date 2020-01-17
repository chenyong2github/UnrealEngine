// Copyright Epic Games, Inc. All Rights Reserved.


#pragma once

#include "CoreMinimal.h"
#include "Textures/SlateIcon.h"
#include "Layout/Visibility.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"
#include "Editor.h"
#include "ILevelEditor.h"
#include "Misc/NotifyHook.h"

class FExtender;
class SBorder;
class IToolkit;
class SDockTab;
class ILevelEditor;

/**
 * Tools for the level editor                   
 */
class SLevelEditorToolBox : public SCompoundWidget, public FNotifyHook
{
public:
	SLATE_BEGIN_ARGS( SLevelEditorToolBox ){}
	SLATE_END_ARGS()

	~SLevelEditorToolBox();

	void Construct(const FArguments& InArgs, const TSharedRef<ILevelEditor>& OwningLevelEditor);

	/** Called by SLevelEditor to notify the toolbox about a new toolkit being hosted */
	void OnToolkitHostingStarted( const TSharedRef<IToolkit>& Toolkit);

	/** Called by SLevelEditor to notify the toolbox about an existing toolkit no longer being hosted */
	void OnToolkitHostingFinished(const TSharedRef<IToolkit>& Toolkit);

	/** Handles updating the mode toolbar when the registered mode commands change */
	void OnEditorModeCommandsChanged();

	/** Sets the parent tab of this toolbox */
	void SetParentTab(TSharedRef<SDockTab>& InDockTab);
private:

	/** Gets the visibility for the SBorder showing toolbox editor mode inline content */
	EVisibility GetInlineContentHolderVisibility() const;

	/** Gets the visibility for the message suggesting the user select a tool */
	EVisibility GetNoToolSelectedTextVisibility() const;

	/** Updates the widget for showing toolbox editor mode inline content */
	void UpdateInlineContent(const TSharedPtr<IToolkit>& Toolkit, TSharedPtr<SWidget> InlineContent);

	/** Creates and sets the mode toolbar */
	void UpdateModeLegacyToolBar();

	/** Handles updating the mode toolbar when the user settings change */
	void HandleUserSettingsChange( FName PropertyName );

private:
	/** Parent tab where this toolbox is hosted */
	TWeakPtr<SDockTab> ParentTab;

	/** Level editor that we're associated with */
	TWeakPtr<ILevelEditor> LevelEditor;

	/** Inline content area for editor modes */
	TSharedPtr<SBorder> InlineContentHolder;

	/** The container holding the mode toolbar */
	TSharedPtr<SBorder> ModeToolBarContainer;

	/** The display name that the parent tab should have as its label */
	FText TabName;

	/** The icon that should be displayed in the parent tab */
	const FSlateBrush* TabIcon;
};
