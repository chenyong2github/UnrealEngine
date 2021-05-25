// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Framework/Commands/Commands.h"
#include "EditorStyleSet.h"
#include "LidarPointCloudStyle.h"

class FLidarPointCloudEditorCommands : public TCommands<FLidarPointCloudEditorCommands>
{
public:
	FLidarPointCloudEditorCommands()
		: TCommands<FLidarPointCloudEditorCommands>(
			TEXT("LidarPointCloudEditor"), // Context name for fast lookup
			NSLOCTEXT("Contexts", "LidarPointCloudEditor", "LiDAR Point Cloud Editor"), // Localized context name for displaying
			NAME_None, // Parent
			FLidarPointCloudStyle::GetStyleSetName() // Icon Style Set
			)
	{
	}

	// TCommand<> interface
	virtual void RegisterCommands() override;
	// End of TCommand<> interface

public:
	TSharedPtr< FUICommandInfo > SetShowGrid;
	TSharedPtr< FUICommandInfo > SetShowBounds;
	TSharedPtr< FUICommandInfo > SetShowCollision;
	TSharedPtr< FUICommandInfo > SetShowNodes;

	TSharedPtr< FUICommandInfo > ResetCamera;

	TSharedPtr< FUICommandInfo > Center;
	TSharedPtr< FUICommandInfo > BuildCollision;
	TSharedPtr< FUICommandInfo > RemoveCollision;

	TSharedPtr< FUICommandInfo > EditMode;

	TSharedPtr< FUICommandInfo > BoxSelection;
	TSharedPtr< FUICommandInfo > PolygonalSelection;
	TSharedPtr< FUICommandInfo > LassoSelection;
	TSharedPtr< FUICommandInfo > PaintSelection;

	TSharedPtr< FUICommandInfo > InvertSelection;

	TSharedPtr< FUICommandInfo > HideSelected;
	TSharedPtr< FUICommandInfo > UnhideAll;

	TSharedPtr< FUICommandInfo > DeleteSelected;
	TSharedPtr< FUICommandInfo > DeleteHidden;

	TSharedPtr< FUICommandInfo > CalculateNormals;
	TSharedPtr< FUICommandInfo > CalculateNormalsSelection;

	TSharedPtr< FUICommandInfo > Extract;
	TSharedPtr< FUICommandInfo > ExtractCopy;
	TSharedPtr< FUICommandInfo > Merge;
	TSharedPtr< FUICommandInfo > Align;
};