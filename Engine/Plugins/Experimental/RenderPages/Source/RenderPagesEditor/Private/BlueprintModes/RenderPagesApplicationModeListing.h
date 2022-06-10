// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "BlueprintModes/RenderPagesApplicationModeBase.h"


namespace UE::RenderPages::Private
{
	/**
	 * This is the application mode for the render page editor listing functionality (the list of pages, with the render previews, the page properties, etc).
	 */
	class FRenderPagesApplicationModeListing : public FRenderPagesApplicationModeBase
	{
	public:
		FRenderPagesApplicationModeListing(TSharedPtr<IRenderPageCollectionEditor> InRenderPagesEditor);

		//~ Begin FApplicationMode interface
		virtual void RegisterTabFactories(TSharedPtr<FTabManager> InTabManager) override;
		virtual void PreDeactivateMode() override;
		virtual void PostActivateMode() override;
		//~ End FApplicationMode interface
	};
}
