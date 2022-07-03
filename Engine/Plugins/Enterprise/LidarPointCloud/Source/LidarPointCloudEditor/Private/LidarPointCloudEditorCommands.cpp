// Copyright Epic Games, Inc. All Rights Reserved.

#include "LidarPointCloudEditorCommands.h"

#include "LidarPointCloudEdModeToolkit.h"

#define LOCTEXT_NAMESPACE "LidarPointCloudEditor"

//////////////////////////////////////////////////////////////////////////
// FLidarPointCloudEditorCommands

void FLidarPointCloudEditorCommands::RegisterCommands()
{
	UI_COMMAND(SetShowGrid, "Grid", "Displays the viewport grid.", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(SetShowBounds, "Bounds", "Toggles display of the bounds of the point cloud.", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(SetShowCollision, "Collision", "Toggles display of the collision of the point cloud.", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(SetShowNodes, "Nodes", "Toggles display of the nodes of the point cloud.", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(ResetCamera, "Reset Camera", "Resets the camera to focus on the point cloud.", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(Center, "Center", "Enable, to center the point cloud asset\nDisable, to use original coordinates.", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BuildCollision, "Build Collision", "Builds collision for this point cloud.", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(RemoveCollision, "Remove Collision", "Removes collision from this point cloud.", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(EditMode, "Edit Mode", "Enables editing of the point cloud.", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BoxSelection, "Box Selection", "Uses box to select points.", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(PolygonalSelection, "Polygonal Selection", "Uses custom polygon to select points.", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(LassoSelection, "Lasso Selection", "Uses custom drawn shape to select points.", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(PaintSelection, "Paint Selection", "Uses adjustable paint brush to select points.", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(InvertSelection, "Invert Selection", "Inverts point selection.", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(HideSelected, "Hide Selected", "Hide selected points.", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(UnhideAll, "Unhide All", "Resets the visibility of all points in the point cloud.", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(DeleteSelected, "Delete Selected", "Permanently remove selected points from the point cloud.", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(DeleteHidden, "Delete Hidden", "Permanently remove hidden points from the point cloud.", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(CalculateNormals, "Calculate Normals", "Calculates normals for the point cloud.", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(CalculateNormalsSelection, "Calculate Normals (Selection)", "Calculates normals for the selected points.", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(Extract, "Extract", "Extracts the selected points as a separate point cloud asset.", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(ExtractCopy, "Extract as Copy", "Duplicates the selected points as a separate point cloud asset.", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(Merge, "Merge", "Merges selected point cloud assets with this one.", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(Align, "Align", "Aligns selected point cloud assets with this one while retaining overall centering.", EUserInterfaceActionType::Button, FInputChord());

	// TOOLKIT COMMANDS

	TArray<TSharedPtr<FUICommandInfo, ESPMode::ThreadSafe>>& ManageCommands = Commands.FindOrAdd(LidarEditorPalletes::Manage);
	UI_COMMAND(ToolkitSelect, "Select", "Select point cloud assets", EUserInterfaceActionType::ToggleButton, FInputChord());
	ManageCommands.Add(ToolkitSelect);
	UI_COMMAND(ToolkitAlign, "Align", "Aligns selected point cloud assets", EUserInterfaceActionType::ToggleButton, FInputChord());
	ManageCommands.Add(ToolkitAlign);
	UI_COMMAND(ToolkitMerge, "Merge", "Merges selected point cloud assets", EUserInterfaceActionType::ToggleButton, FInputChord());
	ManageCommands.Add(ToolkitMerge);
	UI_COMMAND(ToolkitCollision, "Collision", "Create and Destroy point cloud asset collisions", EUserInterfaceActionType::ToggleButton, FInputChord());
	ManageCommands.Add(ToolkitCollision);
	UI_COMMAND(ToolkitNormals, "Normals", "Calculates normals", EUserInterfaceActionType::ToggleButton, FInputChord());
	ManageCommands.Add(ToolkitNormals);
	UI_COMMAND(ToolkitMeshing, "Meshing", "Build Static Meshes", EUserInterfaceActionType::ToggleButton, FInputChord());
	ManageCommands.Add(ToolkitMeshing);
	
	TArray<TSharedPtr<FUICommandInfo, ESPMode::ThreadSafe>>& EditCommands = Commands.FindOrAdd(LidarEditorPalletes::Edit);
	EditCommands.Add(ToolkitSelect);
	UI_COMMAND(ToolkitBoxSelection, "Box", "Uses box to select points.", EUserInterfaceActionType::ToggleButton, FInputChord());
	EditCommands.Add(ToolkitBoxSelection);
	UI_COMMAND(ToolkitPolygonalSelection, "Polygon", "Uses custom polygon to select points.", EUserInterfaceActionType::ToggleButton, FInputChord());
	EditCommands.Add(ToolkitPolygonalSelection);
	UI_COMMAND(ToolkitLassoSelection, "Lasso", "Uses custom drawn shape to select points.", EUserInterfaceActionType::ToggleButton, FInputChord());
	EditCommands.Add(ToolkitLassoSelection);
	UI_COMMAND(ToolkitPaintSelection, "Paint", "Uses adjustable paint brush to select points.", EUserInterfaceActionType::ToggleButton, FInputChord());
	EditCommands.Add(ToolkitPaintSelection);

	UI_COMMAND(ToolktitCancelSelection, "Cancel Selection", "Cancel the active tool", EUserInterfaceActionType::Button, FInputChord(EKeys::Escape));
}

//////////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
