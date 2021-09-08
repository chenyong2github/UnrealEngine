// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterConfiguratorCommands.h"

#define LOCTEXT_NAMESPACE "DisplayClusterConfiguratorCommands"


void FDisplayClusterConfiguratorCommands::RegisterCommands()
{
	UI_COMMAND(Import, "Import", "Import an nDisplay config. This may overwrite components. When upgrading from the deprecated .cfg file format the recommended import workflow is to drag & drop your .cfg file into the content browser.\
 If you need to reimport it is recommended to use the reimport options of the asset context menu", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(Export, "Export", "Export to an nDisplay config. Requires a master cluster node set. This does not export all components and is meant for launching the cluster from the command line", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(EditConfig, "EditConfig", "Edit config file with text editor", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(ExportConfigOnSave, "Export on Save", "Export to nDisplay config automatically on save. Requires a config previously exported", EUserInterfaceActionType::ToggleButton, FInputChord());
	
	UI_COMMAND(ToggleWindowInfo, "Show Window Info", "Enables or disables showing the window information", EUserInterfaceActionType::ToggleButton, FInputChord(EKeys::W));
	UI_COMMAND(ToggleWindowCornerImage, "Show Window Corner Image", "Enables or disables showing the window corner image", EUserInterfaceActionType::ToggleButton, FInputChord(EKeys::E));
	UI_COMMAND(ToggleOutsideViewports, "Show Viewports outside the Window", "Enables or disables showing the viewport which is compleatly outside window", EUserInterfaceActionType::ToggleButton, FInputChord(EKeys::R));
	UI_COMMAND(ToggleClusterItemOverlap, "Allow Cluster Item Overlap", "Enables or disables allowing cluster items to overlap when being manipulated", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(ToggleLockClusterNodesInHosts, "Keep Cluster Nodes inside Hosts", "Prevents cluster nodes from being moved outside of hosts when being manipulated", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(ToggleLockViewports, "Lock Viewports in place", "Locks viewports in place, preventing them from being selected or dragged", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(ToggleLockClusterNodes, "Lock Cluster Nodes in place", "Locks cluster nodes in place, preventing them from being selected or dragged", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(ToggleTintViewports, "Tint Selected Viewports", "Toggles tinting selected viewports orange to better indicate that they are selected", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(ZoomToFit, "Zoom To Fit", "Zoom To Fit In Graph", EUserInterfaceActionType::Button, FInputChord(EKeys::Z));

	UI_COMMAND(BrowseDocumentation, "Documentation", "Opens the documentation reference documentation", EUserInterfaceActionType::Button, FInputChord());

	UI_COMMAND(ToggleAdjacentEdgeSnapping, "Toggle Adjacent Edge Snapping", "Enables or disables snapping adjacent viewport edges together", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(ToggleSameEdgeSnapping, "Toggle Same Edge Snapping", "Enables or disables snapping equivalent viewport edges together", EUserInterfaceActionType::ToggleButton, FInputChord());

	UI_COMMAND(FillParentNode, "Fill Parent", "Resizes and positions this node to fill its parent", EUserInterfaceActionType::Button, FInputChord(EKeys::F, EModifierKey::Shift));
	UI_COMMAND(SizeToChildNodes, "Size to Children", "Resizes this node to completely wrap its children", EUserInterfaceActionType::Button, FInputChord(EKeys::C, EModifierKey::Shift));

	UI_COMMAND(AddNewClusterNode, "Add New Cluster Node", "Adds a new cluster node to the cluster config", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(AddNewViewport, "Add New Viewport", "Adds a new viewport to the cluster node", EUserInterfaceActionType::Button, FInputChord());

	UI_COMMAND(ResetCamera, "Reset Camera", "Resets the camera to focus on the mesh", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(ShowFloor, "Show Floor", "Toggles a ground mesh for collision", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(ShowGrid, "Show Grid", "Toggles the grid", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(ShowOrigin, "Show World Origin", "Display the exact world origin for nDisplay", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(EnableAA, "Enable AA", "Enable anti aliasing in the preview window", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(ShowPreview, "Show Projection Preview", "Show a projection preview when applicable", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(Show3DViewportNames, "Show Viewport Names", "Shows the viewport names in 3d space", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(ToggleShowXformGizmos, "Show Xform Gizmos", "Shows the Xform component gizmos", EUserInterfaceActionType::ToggleButton, FInputChord());
}

#undef LOCTEXT_NAMESPACE
