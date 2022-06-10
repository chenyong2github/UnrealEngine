// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"


class FExtender;
class FToolBarBuilder;
class UToolMenu;

namespace UE::RenderPages
{
	class IRenderPageCollectionEditor;
}


namespace UE::RenderPages::Private
{
	/**
	 * Handles all of the toolbar related construction for the render pages blueprint editor.
	 */
	class FRenderPagesBlueprintEditorToolbar : public TSharedFromThis<FRenderPagesBlueprintEditorToolbar>
	{
	public:
		FRenderPagesBlueprintEditorToolbar(TSharedPtr<IRenderPageCollectionEditor>& InRenderPagesEditor);

		/** Adds the mode-switch UI to the editor. */
		void AddRenderPagesBlueprintEditorModesToolbar(TSharedPtr<FExtender> Extender);

		/** Adds the toolbar for the listing mode to the editor. */
		void AddListingModeToolbar(UToolMenu* InMenu);

		/** Adds the toolbar for the logic mode to the editor. */
		void AddLogicModeToolbar(UToolMenu* InMenu);

	public:
		/** Creates the mode-switch UI. */
		void FillRenderPagesBlueprintEditorModesToolbar(FToolBarBuilder& ToolbarBuilder);
		TWeakPtr<IRenderPageCollectionEditor> BlueprintEditorWeakPtr;
	};
}
