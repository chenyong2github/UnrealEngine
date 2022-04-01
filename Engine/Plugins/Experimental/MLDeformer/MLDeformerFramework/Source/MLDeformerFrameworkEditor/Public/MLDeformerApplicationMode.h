// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "WorkflowOrientedApp/ApplicationMode.h"
#include "WorkflowOrientedApp/WorkflowTabManager.h"

class FWorkflowCentricApplication;
class IPersonaPreviewScene;

namespace UE::MLDeformer
{
	class FMLDeformerEditorToolkit;

	class MLDEFORMERFRAMEWORKEDITOR_API FMLDeformerApplicationMode
		: public FApplicationMode
	{
	public:
		static FName ModeName;

		FMLDeformerApplicationMode(TSharedRef<FWorkflowCentricApplication> InHostingApp, TSharedRef<IPersonaPreviewScene> InPreviewScene);

		// FApplicationMode overrides.
		virtual void RegisterTabFactories(TSharedPtr<FTabManager> InTabManager) override;
		// ~END FApplicationMode overrides.

	protected:
		/** The hosting app. */
		TWeakPtr<FMLDeformerEditorToolkit> EditorToolkit = nullptr;

		/** The tab factories we support. */
		FWorkflowAllowedTabSet TabFactories;
	};

}	// namespace UE::MLDeformer