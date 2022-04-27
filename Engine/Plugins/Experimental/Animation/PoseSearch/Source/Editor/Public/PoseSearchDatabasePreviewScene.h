// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AdvancedPreviewScene.h"

namespace UE::PoseSearch
{
	class FDatabaseEditorToolkit;

	class FDatabasePreviewScene : public FAdvancedPreviewScene
	{
	public:

		FDatabasePreviewScene(
			ConstructionValues CVs,
			const TSharedRef<FDatabaseEditorToolkit>& EditorToolkit);
		~FDatabasePreviewScene() {}

		virtual void Tick(float InDeltaTime) override;

		TSharedRef<FDatabaseEditorToolkit> GetEditorToolkit() const
		{
			return EditorToolkitPtr.Pin().ToSharedRef();
		}

	private:

		/** The asset editor toolkit we are embedded in */
		TWeakPtr<FDatabaseEditorToolkit> EditorToolkitPtr;
	};
}

