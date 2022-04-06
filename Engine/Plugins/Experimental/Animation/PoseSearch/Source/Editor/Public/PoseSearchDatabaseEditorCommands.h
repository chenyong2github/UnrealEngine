// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Framework/Commands/Commands.h"
#include "EditorStyleSet.h"

class FPoseSearchDatabaseEditorCommands : public TCommands<FPoseSearchDatabaseEditorCommands>
{
public:
	FPoseSearchDatabaseEditorCommands()
		: TCommands<FPoseSearchDatabaseEditorCommands>(
			TEXT("PoseSearchDatabaseEditor"), 
			NSLOCTEXT("Contexts", "PoseSearchDatabase", "Pose Search Database"), 
			NAME_None, 
			FEditorStyle::GetStyleSetName())
	{
	}

	virtual void RegisterCommands() override;

public:

	TSharedPtr<FUICommandInfo> ResetPreviewScene;

	TSharedPtr<FUICommandInfo> BuildSearchIndex;
};
