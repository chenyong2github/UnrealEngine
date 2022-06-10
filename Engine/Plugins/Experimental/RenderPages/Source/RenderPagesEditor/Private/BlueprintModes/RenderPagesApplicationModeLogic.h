// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "BlueprintModes/RenderPagesApplicationModeBase.h"
#include "WorkflowOrientedApp/WorkflowTabManager.h"


namespace UE::RenderPages::Private
{
	/**
	 * This is the application mode for the render page editor 'logic' functionality (the blueprint graph).
	 */
	class FRenderPagesApplicationModeLogic : public FRenderPagesApplicationModeBase
	{
	public:
		FRenderPagesApplicationModeLogic(TSharedPtr<IRenderPageCollectionEditor> InRenderPagesEditor);

		//~ Begin FApplicationMode interface
		virtual void RegisterTabFactories(TSharedPtr<FTabManager> InTabManager) override;
		virtual void PreDeactivateMode() override;
		virtual void PostActivateMode() override;
		//~ End FApplicationMode interface
	};
}
