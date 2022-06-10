// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "BlueprintEditorModes.h"
#include "WorkflowOrientedApp/WorkflowTabManager.h"


class URenderPagesBlueprint;

namespace UE::RenderPages
{
	class IRenderPageCollectionEditor;
}


namespace UE::RenderPages::Private
{
	/**
	 * This is the base class for the render page editor application modes.
	 * 
	 * It contains functionality that's shared between all the render page editor application modes.
	 */
	class FRenderPagesApplicationModeBase : public FBlueprintEditorApplicationMode
	{
	public:
		FRenderPagesApplicationModeBase(TSharedPtr<IRenderPageCollectionEditor> InRenderPagesEditor, FName InModeName);

	protected:
		/** Returns the RenderPagesBlueprint of the editor that was given to the constructor. */
		URenderPagesBlueprint* GetBlueprint() const;

		/** Returns the editor that was given to the constructor. */
		TSharedPtr<IRenderPageCollectionEditor> GetBlueprintEditor() const;

	protected:
		/** The editor that was given to the constructor. */
		TWeakPtr<IRenderPageCollectionEditor> BlueprintEditorWeakPtr;

		/** Set of spawnable tabs in the mode. */
		FWorkflowAllowedTabSet TabFactories;
	};
}
