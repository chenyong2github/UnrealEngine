// Copyright Epic Games, Inc. All Rights Reserved.

#include "CurveEditorCommands.h"

#define LOCTEXT_NAMESPACE "CurveEditorCommands"

void FCurveEditorCommands::RegisterCommands()
{
	UI_COMMAND(ZoomToFitHorizontal, "Fit Horizontal", "Zoom to Fit - Horizontal", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(ZoomToFitVertical, "Fit Vertical", "Zoom to Fit - Vertical", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(ZoomToFit, "Fit", "Zoom to Fit", EUserInterfaceActionType::Button, FInputChord(EKeys::F));
	UI_COMMAND(ZoomToFitAll, "FitAll", "Zoom to Fit All", EUserInterfaceActionType::Button, FInputChord(EKeys::A));

	UI_COMMAND(ToggleInputSnapping, "Input Snapping", "Toggle Time Snapping", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(ToggleOutputSnapping, "Output Snapping", "Toggle Value Snapping", EUserInterfaceActionType::ToggleButton, FInputChord());

	UI_COMMAND(ToggleExpandCollapseNodes, "Expand/Collapse Nodes", "Toggle expand or collapse selected nodes", EUserInterfaceActionType::Button, FInputChord(EKeys::V) );
	UI_COMMAND(ToggleExpandCollapseNodesAndDescendants, "Expand/Collapse Nodes and Descendants", "Toggle expand or collapse selected nodes and descendants", EUserInterfaceActionType::Button, FInputChord(EModifierKey::Shift, EKeys::V) );

	UI_COMMAND(InterpolationConstant, "Constant", "Constant interpolation", EUserInterfaceActionType::ToggleButton, FInputChord(EKeys::Five));
	UI_COMMAND(InterpolationLinear, "Linear", "Linear interpolation", EUserInterfaceActionType::ToggleButton, FInputChord(EKeys::Four));
	UI_COMMAND(InterpolationCubicAuto, "Auto", "Cubic interpolation - Automatic tangents", EUserInterfaceActionType::ToggleButton, FInputChord(EKeys::One));
	UI_COMMAND(InterpolationCubicUser, "User", "Cubic interpolation - User flat tangents", EUserInterfaceActionType::ToggleButton, FInputChord(EKeys::Two));
	UI_COMMAND(InterpolationCubicBreak, "Break", "Cubic interpolation - User broken tangents", EUserInterfaceActionType::ToggleButton, FInputChord(EKeys::Three));
	UI_COMMAND(InterpolationToggleWeighted, "Weighted Tangents", "Toggle weighted tangents for cubic interpolation modes. Only supported on some curve types", EUserInterfaceActionType::ToggleButton, FInputChord(EModifierKey::Control, EKeys::W));

	UI_COMMAND(FlattenTangents, "Flatten", "Flatten Tangents", EUserInterfaceActionType::Button, FInputChord(EKeys::Six));
	UI_COMMAND(StraightenTangents, "Straighten", "Straighten Tangents", EUserInterfaceActionType::Button, FInputChord());

	UI_COMMAND(BakeCurve, "Bake", "Bake curve", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(ReduceCurve, "Reduce", "Reduce curve", EUserInterfaceActionType::Button, FInputChord());

	// Pre and Post Infinity
	UI_COMMAND(SetPreInfinityExtrapCycle, "Cycle", "Cycle creates a repeating cycle from the first to last key, effectively modulating the input time. This can create jumps if the terminus values are not the same value.", EUserInterfaceActionType::RadioButton, FInputChord());
	UI_COMMAND(SetPreInfinityExtrapCycleWithOffset, "Cycle with Offset", "Creates a repeating cycle where the value is added to the last value of the previous Cycle. This will avoid jumps but will cause drift over time if there's a net change in value.", EUserInterfaceActionType::RadioButton, FInputChord());
	UI_COMMAND(SetPreInfinityExtrapOscillate, "Oscillate (Ping Pong)", "Creates a repeating cycle which ping pongs and will play from beginning to end, then end to beginning.", EUserInterfaceActionType::RadioButton, FInputChord());
	UI_COMMAND(SetPreInfinityExtrapLinear, "Linear", "Linearly interpolates based on the in tangent of the first key.", EUserInterfaceActionType::RadioButton, FInputChord());
	UI_COMMAND(SetPreInfinityExtrapConstant, "Constant", "Extrapolation will always return the value of the first key.", EUserInterfaceActionType::RadioButton, FInputChord());

	UI_COMMAND(SetPostInfinityExtrapCycle, "Cycle", "Cycle creates a repeating cycle from the first to last key, effectively modulating the input time. This can create jumps if the terminus values are not the same value", EUserInterfaceActionType::RadioButton, FInputChord());
	UI_COMMAND(SetPostInfinityExtrapCycleWithOffset, "Cycle with Offset", "Creates a repeating cycle where the value is added to the last value of the previous Cycle. This will avoid jumps but will cause drift over time if there's a net change in value.", EUserInterfaceActionType::RadioButton, FInputChord());
	UI_COMMAND(SetPostInfinityExtrapOscillate, "Oscillate (Ping Pong)", "Creates a repeating cycle which ping pongs and will play from beginning to end, then end to beginning.", EUserInterfaceActionType::RadioButton, FInputChord());
	UI_COMMAND(SetPostInfinityExtrapLinear, "Linear", "Linearly interpolates based on the in tangent of the last key.", EUserInterfaceActionType::RadioButton, FInputChord());
	UI_COMMAND(SetPostInfinityExtrapConstant, "Constant", "Extrapolation will always return the value of the last key.", EUserInterfaceActionType::RadioButton, FInputChord());


	// Tangent Visibility
	UI_COMMAND(SetAllTangentsVisibility, "All Tangents", "Show all tangents in the curve editor.", EUserInterfaceActionType::RadioButton, FInputChord());
	UI_COMMAND(SetSelectedKeysTangentVisibility, "Selected Keys", "Show tangents for selected keys in the curve editor.", EUserInterfaceActionType::RadioButton, FInputChord());
	UI_COMMAND(SetNoTangentsVisibility, "No Tangents", "Show no tangents in the curve editor.", EUserInterfaceActionType::RadioButton, FInputChord());

	UI_COMMAND(ToggleAutoFrameCurveEditor, "Auto Frame Curves", "Auto frame curves when they are selected.", EUserInterfaceActionType::ToggleButton, FInputChord() );
	UI_COMMAND(ToggleShowCurveEditorCurveToolTips, "Curve Tool Tips", "Show a tool tip with name and values when hovering over a curve.", EUserInterfaceActionType::ToggleButton, FInputChord() );

	UI_COMMAND(AddKeyHovered, "Add Key", "Add a new key to this curve at the current position.", EUserInterfaceActionType::Button, FInputChord(EKeys::MiddleMouseButton) );
	UI_COMMAND(PasteKeysHovered, "Paste", "Paste clipboard contents", EUserInterfaceActionType::Button, FInputChord(EModifierKey::Control, EKeys::V) );

	UI_COMMAND(AddKeyToAllCurves, "Add Key", "Add a new key to all curves at the current time.", EUserInterfaceActionType::Button, FInputChord(EKeys::Enter) );

	// Graph Viewing Modes
	UI_COMMAND(SetViewModeAbsolute, "Absolute View Mode", "Absolute view displays all curves overlapping with the Y axis proportionally scaled.", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(SetViewModeStacked, "Stacked View Mode", "Stacked view displays each curve in its own graph with the Y axis normalized [-1, 1].", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(SetViewModeNormalized, "Normalized View Mode", "Normalized view displays all curves overlapping with the Y axis normalized [-1, 1].", EUserInterfaceActionType::ToggleButton, FInputChord());

	// Deactivate the currently active tool
	UI_COMMAND(DeactivateCurrentTool, "Deactivate Tool", "Deactivates the current tool and returns to just supporting selection.", EUserInterfaceActionType::ToggleButton, FInputChord(EKeys::Q));

	// User Implementable Filter window
	UI_COMMAND(OpenUserImplementableFilterWindow, "Filter...", "Opens a window which lets you choose from user implementable filter classes with advanced settings.", EUserInterfaceActionType::Button, FInputChord());
	
	// Deselect any keys that the user has selected.
	UI_COMMAND(DeselectAllKeys, "Deselect Keys", "Clears your current key selection.", EUserInterfaceActionType::Button, FInputChord(EModifierKey::Control, EKeys::D));

	// Buffer and Apply Curves. Like copy and paste, but with multiple curve support.
	UI_COMMAND(BufferVisibleCurves, "Store Curves", "Stores a copy of the visible curves which can be applied onto other curve sets.", EUserInterfaceActionType::Button, FInputChord());
	// This name is overwritten in CurveEditorContextMenu to show the number of stashed curves.
	UI_COMMAND(ApplyBufferedCurves, "Apply Stored Curves", "Applies the stored curves to the visible set.", EUserInterfaceActionType::Button, FInputChord());

	// Axis Snapping
	UI_COMMAND(SetAxisSnappingNone, "Both", "Disable axis snapping and allow movement on both the X and Y directions.", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(SetAxisSnappingHorizontal, "X Only", "Snap transform tool axis movement to X direction.", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(SetAxisSnappingVertical, "Y Only", "Snap transform tool axis movement to Y direction.", EUserInterfaceActionType::Button, FInputChord());	
	
	// Time Management
	UI_COMMAND(StepToNextKey, "Step to Next Key", "Step to the next key", EUserInterfaceActionType::Button, FInputChord(EKeys::Period));
	UI_COMMAND(StepToPreviousKey, "Step to Previous Key", "Step to the previous key", EUserInterfaceActionType::Button, FInputChord(EKeys::Comma));
	UI_COMMAND(StepForward, "Step Forward", "Step the timeline forward", EUserInterfaceActionType::Button, FInputChord(EKeys::Right));
	UI_COMMAND(StepBackward, "Step Backward", "Step the timeline backward", EUserInterfaceActionType::Button, FInputChord(EKeys::Left));
	UI_COMMAND(JumpToStart, "Jump to Start", "Jump to the start of the playback range", EUserInterfaceActionType::Button, FInputChord(EKeys::Up));
	UI_COMMAND(JumpToEnd, "Jump to End", "Jump to the end of the playback range", EUserInterfaceActionType::Button, FInputChord(EModifierKey::Control, EKeys::Up));

	// Selection Range
	UI_COMMAND(SetSelectionRangeStart, "Set Selection Start", "Sets the start of the selection range", EUserInterfaceActionType::Button, FInputChord(EKeys::I) );
	UI_COMMAND(SetSelectionRangeEnd, "Set Selection End", "Sets the end of the selection range", EUserInterfaceActionType::Button, FInputChord(EKeys::O) );
	UI_COMMAND(ClearSelectionRange, "Clear Selection Range", "Clear the selection range", EUserInterfaceActionType::Button, FInputChord(EModifierKey::Control|EModifierKey::Shift, EKeys::X) );
}

#undef LOCTEXT_NAMESPACE
