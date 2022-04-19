// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AdvancedPreviewScene.h"

class FPoseSearchDatabaseEditorToolkit;

class FPoseSearchDatabasePreviewScene : public FAdvancedPreviewScene
{
public:

	FPoseSearchDatabasePreviewScene(
		ConstructionValues CVs, 
		const TSharedRef<FPoseSearchDatabaseEditorToolkit>& EditorToolkit);
	~FPoseSearchDatabasePreviewScene(){}

	virtual void Tick(float InDeltaTime) override;

	TSharedRef<FPoseSearchDatabaseEditorToolkit> GetEditorToolkit() const
	{
		return EditorToolkitPtr.Pin().ToSharedRef(); 
	}

private:

	/** The asset editor toolkit we are embedded in */
	TWeakPtr<FPoseSearchDatabaseEditorToolkit> EditorToolkitPtr;
};