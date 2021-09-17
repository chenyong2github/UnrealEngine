// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "WorkflowOrientedApp/ApplicationMode.h"
#include "WorkflowOrientedApp/WorkflowCentricApplication.h"
#include "WorkflowOrientedApp/WorkflowTabManager.h"

class FMLDeformerEditorToolkit;

class FMLDeformerApplicationMode : public FApplicationMode
{
public:
	static FName ModeName;

	FMLDeformerApplicationMode(TSharedRef<class FWorkflowCentricApplication> InHostingApp, TSharedRef<class IPersonaPreviewScene> InPreviewScene);

	/** FApplicationMode interface. */
	virtual void RegisterTabFactories(TSharedPtr<FTabManager> InTabManager) override;

protected:
	/** The hosting app. */
	TWeakPtr<FMLDeformerEditorToolkit> EditorToolkit;

	/** The tab factories we support. */
	FWorkflowAllowedTabSet TabFactories;
};
